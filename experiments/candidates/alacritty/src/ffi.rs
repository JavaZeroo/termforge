#[allow(dead_code)]
#[path = "main.rs"]
mod trace;

use std::collections::VecDeque;
use std::ffi::{c_int, c_uchar, c_void};
use std::panic::{AssertUnwindSafe, catch_unwind};

use alacritty_terminal::event::Event;
use alacritty_terminal::grid::{Dimensions, GridCell, GridResizeMap, ResizeMappedRow, ResizeRemovalReason};
use alacritty_terminal::index::{Column, Line};
use alacritty_terminal::term::{Config, Term, TermMode, TerminalResizeMap, cell::{Cell, Flags}};
use alacritty_terminal::vte::ansi::{Color, NamedColor, Processor};
use trace::{Listener, Size};

#[derive(Clone, Copy, PartialEq, Eq)]
enum OriginOperation { Resize, Feed }

struct Terminal {
    term: Term<Listener>,
    parser: Processor,
    listener: Listener,
    events: VecDeque<Event>,
    origin_epoch: u64,
    origin_map: Option<(OriginOperation, TerminalResizeMap)>,
}

#[repr(C)]
pub struct TerminalMeta {
    rows: usize,
    columns: usize,
    history: usize,
    discarded: u64,
    row: i32,
    column: usize,
    wrap_pending: bool,
    fg: i32,
    bg: i32,
    bold: bool,
    mode_bits: u64,
    grapheme_overflows: u64,
    history_clear_epoch: u64,
}

#[repr(C)]
pub struct RowMeta {
    id: u64,
    hard_break: bool,
    wrap: bool,
    wrap_columns: usize,
}

#[repr(C)]
pub struct CellMeta {
    width: i32,
    fg: i32,
    bg: i32,
    bold: bool,
    marks: usize,
    written: bool,
    text_len: usize,
    clipped: bool,
    grapheme_fragment: bool,
    leading_spacer: bool,
}

#[repr(C)]
pub struct ResizeMapMeta {
    epoch: u64,
    before_rows: usize, before_columns: usize, before_history: usize,
    after_rows: usize, after_columns: usize, after_history: usize,
    discarded_before: u64, discarded_after: u64,
    source_rows: usize, target_rows: usize, removed_rows: usize,
}

#[repr(C)]
pub struct ResizeSourceMeta {
    id: u64,
    line: i32,
    columns: usize,
    hard_break: bool,
    wrap: bool,
}

#[repr(C)]
pub struct ResizeRowMeta {
    id: u64,
    hard_boundary: u64,
    line: i32,
    removal_reason: u32,
    columns: usize,
    spans: usize,
}

#[repr(C)]
pub struct ResizeSpan {
    source_row: u64,
    source_column: usize,
    column: usize,
    len: usize,
}

fn color(value: Color) -> i32 {
    match value {
        Color::Named(NamedColor::Foreground | NamedColor::Background) => -1,
        Color::Named(named) if (named as usize) < 16 => named as i32,
        Color::Indexed(index) if index < 16 => index as i32,
        _ => -2,
    }
}

fn write_error(error: *mut c_uchar, capacity: usize, message: &str) {
    if error.is_null() || capacity == 0 {
        return;
    }
    let bytes = message.as_bytes();
    let count = bytes.len().min(capacity - 1);
    unsafe {
        std::ptr::copy_nonoverlapping(bytes.as_ptr(), error, count);
        *error.add(count) = 0;
    }
}

fn guard(
    error: *mut c_uchar,
    capacity: usize,
    work: impl FnOnce() -> Result<(), String>,
) -> c_int {
    match catch_unwind(AssertUnwindSafe(work)) {
        Ok(Ok(())) => 0,
        Ok(Err(message)) => {
            write_error(error, capacity, &message);
            1
        },
        Err(_) => {
            write_error(error, capacity, "Rust panic contained at candidate boundary");
            1
        },
    }
}

unsafe fn terminal<'a>(value: *mut c_void) -> Result<&'a mut Terminal, String> {
    if value.is_null() {
        return Err("null terminal handle".into());
    }
    Ok(unsafe { &mut *value.cast::<Terminal>() })
}

fn drain_events(value: &mut Terminal) -> Result<(), String> {
    let mut pending = value.listener.0.lock().map_err(|_| "candidate event lock poisoned".to_string())?;
    value.events.extend(pending.drain(..));
    Ok(())
}

#[unsafe(no_mangle)]
pub extern "C" fn tf_terminal_new(rows: usize, columns: usize, history: usize) -> *mut c_void {
    if rows == 0 || rows > 200 || columns == 0 || columns > 500 || history > 10000 {
        return std::ptr::null_mut();
    }
    catch_unwind(AssertUnwindSafe(|| {
        let listener = Listener::default();
        let config = Config { scrolling_history: history, ..Config::default() };
        let term = Term::new(config, &Size { columns, rows }, listener.clone());
        let value = Terminal { term, parser: Processor::new(), listener, events: VecDeque::new(),
                               origin_epoch: 0, origin_map: None };
        Box::into_raw(Box::new(value)).cast::<c_void>()
    }))
    .unwrap_or(std::ptr::null_mut())
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn tf_terminal_free(value: *mut c_void) {
    if !value.is_null() {
        drop(unsafe { Box::from_raw(value.cast::<Terminal>()) });
    }
}

unsafe fn feed(
    value: *mut c_void,
    data: *const c_uchar,
    len: usize,
    error: *mut c_uchar,
    error_capacity: usize,
    capture: bool,
) -> c_int {
    guard(error, error_capacity, || {
        if len > 1024 * 1024 {
            return Err("candidate feed exceeds 1 MiB".into());
        }
        if data.is_null() && len != 0 {
            return Err("null candidate feed buffer".into());
        }
        let value = unsafe { terminal(value)? };
        let bytes = if len == 0 {
            &[]
        } else {
            unsafe { std::slice::from_raw_parts(data, len) }
        };
        let epoch = if capture {
            Some(value.origin_epoch.checked_add(1).ok_or("origin map epoch exhausted")?)
        } else { None };
        value.origin_map = None;
        let map = if capture {
            Some(value.term.feed_with_origin_map(&mut value.parser, bytes))
        } else {
            value.parser.advance(&mut value.term, bytes);
            None
        };
        drain_events(value)?;
        if let (Some(map), Some(epoch)) = (map, epoch) {
            value.origin_epoch = epoch;
            value.origin_map = Some((OriginOperation::Feed, map));
        }
        Ok(())
    })
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn tf_terminal_feed(
    value: *mut c_void, data: *const c_uchar, len: usize,
    error: *mut c_uchar, error_capacity: usize,
) -> c_int {
    unsafe { feed(value, data, len, error, error_capacity, false) }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn tf_terminal_feed_mapped(
    value: *mut c_void, data: *const c_uchar, len: usize,
    error: *mut c_uchar, error_capacity: usize,
) -> c_int {
    unsafe { feed(value, data, len, error, error_capacity, true) }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn tf_terminal_finish(
    value: *mut c_void,
    error: *mut c_uchar,
    error_capacity: usize,
) -> c_int {
    guard(error, error_capacity, || {
        unsafe { terminal(value)? }.origin_map = None;
        Ok(())
    })
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn tf_terminal_resize(
    value: *mut c_void,
    rows: usize,
    columns: usize,
    error: *mut c_uchar,
    error_capacity: usize,
) -> c_int {
    guard(error, error_capacity, || {
        if rows == 0 || rows > 200 || columns == 0 || columns > 500 {
            return Err("invalid candidate dimensions".into());
        }
        let value = unsafe { terminal(value)? };
        let epoch = value.origin_epoch.checked_add(1).ok_or("origin map epoch exhausted")?;
        value.origin_map = None;
        let map = value.term.resize_with_map(Size { columns, rows });
        drain_events(value)?;
        value.origin_epoch = epoch;
        value.origin_map = Some((OriginOperation::Resize, map));
        Ok(())
    })
}

fn origin_buffer(value: &Terminal, alternate: bool, operation: OriginOperation) -> Option<&GridResizeMap> {
    let (actual, map) = value.origin_map.as_ref()?;
    if *actual != operation { return None; }
    Some(if alternate { &map.alternate } else { &map.primary })
}

fn resize_row(map: &GridResizeMap, removed: bool, index: usize) -> Option<(&ResizeMappedRow, u32)> {
    if removed {
        let value = map.removed_rows.get(index)?;
        let reason = match value.reason {
            ResizeRemovalReason::Capacity => 1,
            ResizeRemovalReason::CursorReposition => 2,
            ResizeRemovalReason::HeightCrop => 3,
            ResizeRemovalReason::ColumnCrop => 4,
        };
        Some((&value.row, reason))
    } else {
        Some((map.target_rows.get(index)?, 0))
    }
}

unsafe fn origin_map_meta(
    value: *mut c_void, alternate: bool, output: *mut ResizeMapMeta,
    operation: OriginOperation,
) -> c_int {
    if output.is_null() { return 1; }
    let Ok(value) = (unsafe { terminal(value) }) else { return 1 };
    let Some(map) = origin_buffer(value, alternate, operation) else { return 1 };
    unsafe { *output = ResizeMapMeta {
        epoch: value.origin_epoch,
        before_rows: map.before_rows, before_columns: map.before_columns, before_history: map.before_history,
        after_rows: map.after_rows, after_columns: map.after_columns, after_history: map.after_history,
        discarded_before: map.discarded_before, discarded_after: map.discarded_after,
        source_rows: map.source_rows.len(), target_rows: map.target_rows.len(), removed_rows: map.removed_rows.len(),
    } };
    0
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn tf_terminal_resize_map_meta(
    value: *mut c_void, alternate: bool, output: *mut ResizeMapMeta,
) -> c_int {
    unsafe { origin_map_meta(value, alternate, output, OriginOperation::Resize) }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn tf_terminal_feed_map_meta(
    value: *mut c_void, alternate: bool, output: *mut ResizeMapMeta,
) -> c_int {
    unsafe { origin_map_meta(value, alternate, output, OriginOperation::Feed) }
}

unsafe fn origin_source(
    value: *mut c_void, epoch: u64, alternate: bool, index: usize, output: *mut ResizeSourceMeta,
    operation: OriginOperation,
) -> c_int {
    if output.is_null() { return 1; }
    let Ok(value) = (unsafe { terminal(value) }) else { return 1 };
    if epoch != value.origin_epoch { return 1; }
    let Some(map) = origin_buffer(value, alternate, operation) else { return 1 };
    let Some(row) = map.source_rows.get(index) else { return 1 };
    unsafe { *output = ResizeSourceMeta {
        id: row.id, line: row.line, columns: row.columns, hard_break: row.hard_break, wrap: row.wrap,
    } };
    0
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn tf_terminal_resize_source(
    value: *mut c_void, epoch: u64, alternate: bool, index: usize, output: *mut ResizeSourceMeta,
) -> c_int {
    unsafe { origin_source(value, epoch, alternate, index, output, OriginOperation::Resize) }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn tf_terminal_feed_source(
    value: *mut c_void, epoch: u64, alternate: bool, index: usize, output: *mut ResizeSourceMeta,
) -> c_int {
    unsafe { origin_source(value, epoch, alternate, index, output, OriginOperation::Feed) }
}

unsafe fn origin_row(
    value: *mut c_void, epoch: u64, alternate: bool, removed: bool,
    index: usize, output: *mut ResizeRowMeta,
    operation: OriginOperation,
) -> c_int {
    if output.is_null() { return 1; }
    let Ok(value) = (unsafe { terminal(value) }) else { return 1 };
    if epoch != value.origin_epoch { return 1; }
    let Some(map) = origin_buffer(value, alternate, operation) else { return 1 };
    let Some((row, reason)) = resize_row(map, removed, index) else { return 1 };
    unsafe { *output = ResizeRowMeta {
        id: row.id, hard_boundary: row.origins.hard_boundary.unwrap_or(0), line: row.line,
        removal_reason: reason, columns: row.columns, spans: row.origins.spans.len(),
    } };
    0
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn tf_terminal_resize_row(
    value: *mut c_void, epoch: u64, alternate: bool, removed: bool, index: usize, output: *mut ResizeRowMeta,
) -> c_int {
    unsafe { origin_row(value, epoch, alternate, removed, index, output, OriginOperation::Resize) }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn tf_terminal_feed_row(
    value: *mut c_void, epoch: u64, alternate: bool, removed: bool, index: usize, output: *mut ResizeRowMeta,
) -> c_int {
    unsafe { origin_row(value, epoch, alternate, removed, index, output, OriginOperation::Feed) }
}

unsafe fn origin_span(
    value: *mut c_void, epoch: u64, alternate: bool, removed: bool,
    row: usize, index: usize, output: *mut ResizeSpan,
    operation: OriginOperation,
) -> c_int {
    if output.is_null() { return 1; }
    let Ok(value) = (unsafe { terminal(value) }) else { return 1 };
    if epoch != value.origin_epoch { return 1; }
    let Some(map) = origin_buffer(value, alternate, operation) else { return 1 };
    let Some((row, _)) = resize_row(map, removed, row) else { return 1 };
    let Some(span) = row.origins.spans.get(index) else { return 1 };
    unsafe { *output = ResizeSpan {
        source_row: span.source_row, source_column: span.source_column, column: span.column, len: span.len,
    } };
    0
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn tf_terminal_resize_span(
    value: *mut c_void, epoch: u64, alternate: bool, removed: bool, row: usize, index: usize, output: *mut ResizeSpan,
) -> c_int {
    unsafe { origin_span(value, epoch, alternate, removed, row, index, output, OriginOperation::Resize) }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn tf_terminal_feed_span(
    value: *mut c_void, epoch: u64, alternate: bool, removed: bool, row: usize, index: usize, output: *mut ResizeSpan,
) -> c_int {
    unsafe { origin_span(value, epoch, alternate, removed, row, index, output, OriginOperation::Feed) }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn tf_terminal_meta(value: *mut c_void, output: *mut TerminalMeta) -> c_int {
    if output.is_null() {
        return 1;
    }
    let Ok(value) = (unsafe { terminal(value) }) else { return 1 };
    let grid = value.term.grid();
    let cursor = &grid.cursor;
    let result = TerminalMeta {
        rows: grid.screen_lines(),
        columns: grid.columns(),
        history: grid.history_size(),
        discarded: grid.discarded(),
        row: cursor.point.line.0,
        column: cursor.point.column.0,
        wrap_pending: cursor.input_needs_wrap,
        fg: color(cursor.template.fg),
        bg: color(cursor.template.bg),
        bold: cursor.template.flags.contains(Flags::BOLD),
        mode_bits: value.term.mode().bits() as u64,
        grapheme_overflows: value.term.grapheme_overflows(),
        history_clear_epoch: value.term.history_clear_epoch(),
    };
    unsafe { *output = result };
    0
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn tf_terminal_discard_history(
    value: *mut c_void,
    ids: *const u64,
    count: usize,
    discarded: *mut u64,
    error: *mut c_uchar,
    error_capacity: usize,
) -> c_int {
    guard(error, error_capacity, || {
        if count > 10000 || discarded.is_null() || (count != 0 && ids.is_null()) {
            return Err("invalid history eviction buffer".into());
        }
        let value = unsafe { terminal(value)? };
        if value.term.mode().contains(TermMode::ALT_SCREEN) {
            return Err("primary history eviction on alternate screen".into());
        }
        let grid = value.term.grid();
        let history = grid.history_size();
        if count > history {
            return Err("history eviction exceeds retained rows".into());
        }
        let ids = if count == 0 { &[] } else { unsafe { std::slice::from_raw_parts(ids, count) } };
        for (index, id) in ids.iter().enumerate() {
            if grid[Line(index as i32 - history as i32)].id() != *id {
                return Err("history eviction prefix identity differs".into());
            }
        }
        value.origin_map = None;
        value.term.grid_mut().discard_oldest_history(count);
        unsafe { *discarded = value.term.grid().discarded() };
        Ok(())
    })
}

fn row_at<'a>(value: &'a Terminal, index: usize) -> Result<(Line, &'a alacritty_terminal::grid::Row<Cell>), String> {
    let grid = value.term.grid();
    let history = grid.history_size();
    if index >= history + grid.screen_lines() {
        return Err("candidate row out of range".into());
    }
    let line = Line(index as i32 - history as i32);
    Ok((line, &grid[line]))
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn tf_terminal_row_meta(
    value: *mut c_void,
    index: usize,
    output: *mut RowMeta,
) -> c_int {
    if output.is_null() {
        return 1;
    }
    let Ok(value) = (unsafe { terminal(value) }) else { return 1 };
    let Ok((_, row)) = row_at(value, index) else { return 1 };
    let result = RowMeta {
        id: row.id(),
        hard_break: row.hard_break(),
        wrap: row.last().is_some_and(|cell| cell.flags.contains(Flags::WRAPLINE)),
        wrap_columns: row.wrap_columns(),
    };
    unsafe { *output = result };
    0
}

fn cell_text(cell: &Cell) -> String {
    let mut text = if cell.is_empty() { String::new() } else { cell.c.to_string() };
    if let Some(zero_width) = cell.zerowidth() {
        text.extend(zero_width);
    }
    text
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn tf_terminal_cell_meta(
    value: *mut c_void,
    index: usize,
    column: usize,
    output: *mut CellMeta,
) -> c_int {
    if output.is_null() {
        return 1;
    }
    let Ok(value) = (unsafe { terminal(value) }) else { return 1 };
    let Ok((line, row)) = row_at(value, index) else { return 1 };
    if column >= value.term.grid().columns() {
        return 1;
    }
    let cell = &row[Column(column)];
    let _ = line;
    let text = cell_text(cell);
    let flags = cell.flags();
    let result = CellMeta {
        width: if flags.contains(Flags::WIDE_CHAR) {
            2
        } else if flags.contains(Flags::WIDE_CHAR_SPACER) {
            0
        } else {
            1
        },
        fg: color(cell.fg),
        bg: color(cell.bg),
        bold: flags.contains(Flags::BOLD),
        marks: cell.zerowidth().map_or(0, |marks| marks.len()),
        written: !cell.is_empty() || !text.is_empty() ||
            flags.intersects(Flags::WIDE_CHAR | Flags::WIDE_CHAR_SPACER | Flags::LEADING_WIDE_CHAR_SPACER),
        text_len: text.len(),
        clipped: flags.contains(Flags::CLIPPED_GRAPHEME),
        grapheme_fragment: cell.grapheme_fragment(),
        leading_spacer: flags.contains(Flags::LEADING_WIDE_CHAR_SPACER),
    };
    unsafe { *output = result };
    0
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn tf_terminal_cell_text(
    value: *mut c_void,
    index: usize,
    column: usize,
    output: *mut c_uchar,
    capacity: usize,
    needed: *mut usize,
) -> c_int {
    if needed.is_null() {
        return 1;
    }
    let Ok(value) = (unsafe { terminal(value) }) else { return 1 };
    let Ok((_, row)) = row_at(value, index) else { return 1 };
    if column >= value.term.grid().columns() {
        return 1;
    }
    let text = cell_text(&row[Column(column)]);
    unsafe { *needed = text.len() };
    if capacity < text.len() || (output.is_null() && !text.is_empty()) {
        return 1;
    }
    if !text.is_empty() {
        unsafe { std::ptr::copy_nonoverlapping(text.as_ptr(), output, text.len()) };
    }
    0
}

fn event_data(event: &Event) -> (u32, Vec<u8>) {
    match event {
        Event::PtyWrite(text) => (1, text.as_bytes().to_vec()),
        Event::Title(text) => (2, text.as_bytes().to_vec()),
        Event::ResetTitle => (3, Vec::new()),
        Event::Bell => (4, Vec::new()),
        Event::Wakeup => (5, Vec::new()),
        Event::Exit => (6, Vec::new()),
        Event::MouseCursorDirty => (7, Vec::new()),
        Event::ClipboardStore(_, text) => (8, text.as_bytes().to_vec()),
        Event::ClipboardLoad(_, _) => (9, Vec::new()),
        Event::ColorRequest(_, _) => (10, Vec::new()),
        Event::TextAreaSizeRequest(_) => (11, Vec::new()),
        Event::CursorBlinkingChange => (12, Vec::new()),
        Event::ChildExit(status) => (13, format!("{status:?}").into_bytes()),
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn tf_terminal_next_event(
    value: *mut c_void,
    kind: *mut u32,
    output: *mut c_uchar,
    capacity: usize,
    needed: *mut usize,
) -> c_int {
    if kind.is_null() || needed.is_null() {
        return 2;
    }
    let Ok(value) = (unsafe { terminal(value) }) else { return 2 };
    let Some(event) = value.events.front() else { return 0 };
    let (event_kind, bytes) = event_data(event);
    unsafe {
        *kind = event_kind;
        *needed = bytes.len();
    }
    if capacity < bytes.len() || (output.is_null() && !bytes.is_empty()) {
        return 2;
    }
    if !bytes.is_empty() {
        unsafe { std::ptr::copy_nonoverlapping(bytes.as_ptr(), output, bytes.len()) };
    }
    value.events.pop_front();
    1
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::mem::MaybeUninit;
    use std::ptr::null_mut;

    struct Handle(*mut c_void);

    impl Drop for Handle {
        fn drop(&mut self) {
            unsafe { tf_terminal_free(self.0) };
        }
    }

    impl Handle {
        fn feed(&self, bytes: &[u8]) {
            assert_eq!(unsafe { tf_terminal_feed(self.0, bytes.as_ptr(), bytes.len(), null_mut(), 0) }, 0);
        }

        fn meta(&self) -> TerminalMeta {
            let mut meta = MaybeUninit::uninit();
            assert_eq!(unsafe { tf_terminal_meta(self.0, meta.as_mut_ptr()) }, 0);
            unsafe { meta.assume_init() }
        }

        fn rows(&self) -> Vec<(u64, u8)> {
            let meta = self.meta();
            (0..meta.history + meta.rows).map(|index| {
                let mut row = MaybeUninit::<RowMeta>::uninit();
                assert_eq!(unsafe { tf_terminal_row_meta(self.0, index, row.as_mut_ptr()) }, 0);
                let mut text = [0; 8];
                let mut needed = 0;
                assert_eq!(unsafe {
                    tf_terminal_cell_text(self.0, index, 0, text.as_mut_ptr(), text.len(), &mut needed)
                }, 0);
                (unsafe { row.assume_init() }.id, text[0])
            }).collect()
        }
    }

    #[test]
    fn capacity_counter_counts_real_loss_but_not_height_growth() {
        for (rows, history, input, next_rows, discarded, text) in [
            (3, 1, "A\r\nB\r\nC", 1, 1, "BC"),
            // rows() leaves its zeroed byte unchanged for an unwritten cell
            // whose public cell_text result has length zero.
            (1, 0, "A", 2, 0, "A\0"),
            (1, 1, "A\r\nB", 3, 0, "AB\0"),
            (3, 4, "A\r\nB\r\nC", 1, 0, "ABC"),
        ] {
            let value = Handle(tf_terminal_new(rows, 1, history));
            value.feed(input.as_bytes());
            assert_eq!(value.meta().discarded, 0);
            assert_eq!(unsafe { tf_terminal_resize(value.0, next_rows, 1, null_mut(), 0) }, 0);
            assert_eq!(value.meta().discarded, discarded,
                       "{rows} rows, history {history}, resize to {next_rows}");
            assert_eq!(value.rows().iter().map(|row| row.1).collect::<Vec<_>>(), text.as_bytes());
        }

        let value = Handle(tf_terminal_new(3, 1, 1));
        value.feed(b"A\r\nB\r\nC\x1b[2S");
        assert_eq!(value.meta().discarded, 1, "multi-line SU lost A past the capacity");
        assert_eq!(value.rows().iter().map(|row| row.1).collect::<Vec<_>>(), b"BC\0\0");
        let no_history = Handle(tf_terminal_new(1, 1, 0));
        no_history.feed(b"A\r\nB");
        assert_eq!(no_history.meta().discarded, 1, "ordinary zero-capacity scroll still evicts A");
    }

    #[test]
    fn resize_map_records_visible_rows_evicted_during_height_shrink() {
        let value = Handle(tf_terminal_new(4, 3, 1));
        value.feed(b"ABCDEFGH\r\nIJKLMNOP\r\nQRSTUVWX");
        let sources = value.rows();
        assert_eq!(sources.iter().map(|row| row.1).collect::<Vec<_>>(), b"LOQTW");
        assert_eq!(value.meta().discarded, 4);
        assert_eq!(unsafe { tf_terminal_resize(value.0, 1, 1, null_mut(), 0) }, 0);
        let mut map = MaybeUninit::<ResizeMapMeta>::uninit();
        assert_eq!(unsafe { tf_terminal_resize_map_meta(value.0, false, map.as_mut_ptr()) }, 0);
        let map = unsafe { map.assume_init() };
        assert_eq!((map.source_rows, map.target_rows, map.removed_rows), (5, 2, 6));
        assert_eq!((map.discarded_before, map.discarded_after), (4, 10));
        assert_eq!(value.rows().iter().map(|row| row.1).collect::<Vec<_>>(), b"WX");

        let mut removed = std::collections::BTreeSet::new();
        let mut hard_boundaries = Vec::new();
        for index in 0..map.removed_rows {
            let mut row = MaybeUninit::<ResizeRowMeta>::uninit();
            assert_eq!(unsafe {
                tf_terminal_resize_row(value.0, map.epoch, false, true, index, row.as_mut_ptr())
            }, 0);
            let row = unsafe { row.assume_init() };
            assert_eq!((row.removal_reason, row.spans), (1, 1));
            if row.hard_boundary != 0 { hard_boundaries.push(row.hard_boundary); }
            let mut span = MaybeUninit::<ResizeSpan>::uninit();
            assert_eq!(unsafe {
                tf_terminal_resize_span(value.0, map.epoch, false, true, index, 0, span.as_mut_ptr())
            }, 0);
            let span = unsafe { span.assume_init() };
            assert_eq!(span.column, 0);
            assert!(removed.insert((span.source_row, span.source_column, span.len)));
        }
        assert_eq!(removed, std::collections::BTreeSet::from([
            (sources[0].0, 0, 3), (sources[1].0, 0, 3), (sources[2].0, 0, 3),
            (sources[3].0, 0, 1), (sources[3].0, 1, 1), (sources[3].0, 2, 1),
        ]));
        assert_eq!(hard_boundaries, vec![sources[1].0]);
        for index in 0..2 {
            let mut span = MaybeUninit::<ResizeSpan>::uninit();
            assert_eq!(unsafe {
                tf_terminal_resize_span(value.0, map.epoch, false, false, index, 0, span.as_mut_ptr())
            }, 0);
            let span = unsafe { span.assume_init() };
            assert_eq!((span.source_row, span.source_column, span.column, span.len),
                       (sources[4].0, index, 0, 1));
        }
    }

    #[test]
    fn resize_map_clipped_wide_cell_does_not_own_its_removed_spacer() {
        let value = Handle(tf_terminal_new(2, 4, 8));
        value.feed("中X\r\nY".as_bytes());
        let sources = value.rows();
        assert_eq!(unsafe { tf_terminal_resize(value.0, 2, 1, null_mut(), 0) }, 0);
        let mut map = MaybeUninit::<ResizeMapMeta>::uninit();
        assert_eq!(unsafe { tf_terminal_resize_map_meta(value.0, false, map.as_mut_ptr()) }, 0);
        let map = unsafe { map.assume_init() };
        assert_eq!(map.target_rows, 3);
        for (index, &(source, column, hard)) in [(0, 0, false), (0, 2, true), (1, 0, false)].iter().enumerate() {
            let mut row = MaybeUninit::<ResizeRowMeta>::uninit();
            assert_eq!(unsafe {
                tf_terminal_resize_row(value.0, map.epoch, false, false, index, row.as_mut_ptr())
            }, 0);
            let row = unsafe { row.assume_init() };
            assert_eq!((row.spans, row.hard_boundary), (1, if hard { sources[source].0 } else { 0 }));
            let mut span = MaybeUninit::<ResizeSpan>::uninit();
            assert_eq!(unsafe {
                tf_terminal_resize_span(value.0, map.epoch, false, false, index, 0, span.as_mut_ptr())
            }, 0);
            let span = unsafe { span.assume_init() };
            assert_eq!((span.source_row, span.source_column, span.column, span.len),
                       (sources[source].0, column, 0, 1));
        }
    }

    #[test]
    fn resize_map_merges_equal_text_from_distinct_sources() {
        let value = Handle(tf_terminal_new(2, 4, 8));
        value.feed(b"ABCDABCD\r\nABCD");
        let sources = value.rows();
        assert_eq!(sources.len(), 3);
        assert_eq!(unsafe { tf_terminal_resize(value.0, 2, 8, null_mut(), 0) }, 0);
        let mut map = MaybeUninit::<ResizeMapMeta>::uninit();
        assert_eq!(unsafe { tf_terminal_resize_map_meta(value.0, false, map.as_mut_ptr()) }, 0);
        let map = unsafe { map.assume_init() };
        assert_eq!((map.source_rows, map.target_rows, map.after_history, map.discarded_after), (3, 2, 0, 0));
        let mut row = MaybeUninit::<ResizeRowMeta>::uninit();
        assert_eq!(unsafe { tf_terminal_resize_row(value.0, map.epoch, false, false, 0, row.as_mut_ptr()) }, 0);
        let row = unsafe { row.assume_init() };
        assert_eq!((row.columns, row.spans, row.hard_boundary), (8, 2, sources[1].0));
        for index in 0..2 {
            let mut span = MaybeUninit::<ResizeSpan>::uninit();
            assert_eq!(unsafe {
                tf_terminal_resize_span(value.0, map.epoch, false, false, 0, index, span.as_mut_ptr())
            }, 0);
            let span = unsafe { span.assume_init() };
            assert_eq!((span.source_row, span.source_column, span.column, span.len),
                       (sources[index].0, 0, index * 4, 4));
        }
        let mut separate = MaybeUninit::<ResizeSpan>::uninit();
        assert_eq!(unsafe {
            tf_terminal_resize_span(value.0, map.epoch, false, false, 1, 0, separate.as_mut_ptr())
        }, 0);
        let separate = unsafe { separate.assume_init() };
        assert_eq!((separate.source_row, separate.source_column, separate.column, separate.len),
                   (sources[2].0, 0, 0, 4));
    }

    #[test]
    fn resize_map_tracks_split_source_cells_and_hard_boundaries() {
        let value = Handle(tf_terminal_new(2, 8, 8));
        value.feed(b"ABCDEFGH\r\nIJKLMNOP\r\nQRSTUVWX");
        let sources = value.rows();
        assert_eq!(sources.len(), 3);
        assert_eq!(unsafe { tf_terminal_resize(value.0, 2, 4, null_mut(), 0) }, 0);
        let mut map = MaybeUninit::<ResizeMapMeta>::uninit();
        assert_eq!(unsafe { tf_terminal_resize_map_meta(value.0, false, map.as_mut_ptr()) }, 0);
        let map = unsafe { map.assume_init() };
        assert_eq!((map.before_rows, map.before_columns, map.before_history), (2, 8, 1));
        assert_eq!((map.after_rows, map.after_columns, map.after_history), (2, 4, 4));
        assert_eq!((map.source_rows, map.target_rows, map.removed_rows), (3, 6, 0));
        let actual_ids = value.rows();
        for (index, &(source_index, source_column, hard)) in [
            (0, 0, false), (0, 4, true),
            (1, 0, false), (1, 4, true),
            (2, 0, false), (2, 4, false),
        ].iter().enumerate() {
            let mut row = MaybeUninit::<ResizeRowMeta>::uninit();
            assert_eq!(unsafe {
                tf_terminal_resize_row(value.0, map.epoch, false, false, index, row.as_mut_ptr())
            }, 0);
            let row = unsafe { row.assume_init() };
            assert_eq!((row.id, row.line, row.columns, row.spans, row.removal_reason),
                       (actual_ids[index].0, index as i32 - 4, 4, 1, 0));
            assert_eq!(row.hard_boundary, if hard { sources[source_index].0 } else { 0 });
            let mut span = MaybeUninit::<ResizeSpan>::uninit();
            assert_eq!(unsafe {
                tf_terminal_resize_span(value.0, map.epoch, false, false, index, 0, span.as_mut_ptr())
            }, 0);
            let span = unsafe { span.assume_init() };
            assert_eq!((span.source_row, span.source_column, span.column, span.len),
                       (sources[source_index].0, source_column, 0, 4));
        }
    }

    #[test]
    fn width_reflow_exports_logical_line_ends() {
        let value = Handle(tf_terminal_new(2, 8, 8));
        assert!(!value.0.is_null());
        value.feed(b"ABCDEFGH\r\nIJKLMNOP\r\nQRSTUVWX");
        for (columns, expected) in [
            (4, vec![(false, true, 4), (true, false, 0),
                     (false, true, 4), (true, false, 0),
                     (false, true, 4), (false, false, 0)]),
            (8, vec![(true, false, 0), (true, false, 0), (false, false, 0)]),
        ] {
            assert_eq!(unsafe { tf_terminal_resize(value.0, 2, columns, null_mut(), 0) }, 0);
            let meta = value.meta();
            let actual: Vec<_> = (0..meta.history + meta.rows).map(|index| {
                let mut row = MaybeUninit::<RowMeta>::uninit();
                assert_eq!(unsafe { tf_terminal_row_meta(value.0, index, row.as_mut_ptr()) }, 0);
                let row = unsafe { row.assume_init() };
                (row.hard_break, row.wrap, row.wrap_columns)
            }).collect();
            assert_eq!(actual, expected);
            assert_eq!(meta.discarded, 0, "merging physical containers is not history eviction");
        }
    }

    #[test]
    fn width_reflow_counts_capacity_eviction_once() {
        let value = Handle(tf_terminal_new(2, 8, 1));
        assert!(!value.0.is_null());
        value.feed(b"ABCDEFGH\r\nIJKLMNOP\r\nQRSTUVWX");
        assert_eq!(value.meta().history, 1);
        assert_eq!(value.meta().discarded, 0);

        assert_eq!(unsafe { tf_terminal_resize(value.0, 2, 4, null_mut(), 0) }, 0);
        let meta = value.meta();
        assert_eq!((meta.history, meta.rows, meta.columns), (1, 2, 4));
        let mut actual = Vec::new();
        for row in 0..3 {
            for column in 0..4 {
                let mut text = [0; 8];
                let mut needed = 0;
                assert_eq!(unsafe {
                    tf_terminal_cell_text(value.0, row, column, text.as_mut_ptr(), text.len(), &mut needed)
                }, 0);
                actual.extend_from_slice(&text[..needed]);
            }
        }
        assert_eq!(actual, b"MNOPQRSTUVWX");
        assert_eq!(meta.discarded, 3, "three oldest reflowed rows exceeded history capacity");

        value.feed(b"");
        assert_eq!(unsafe { tf_terminal_resize(value.0, 2, 4, null_mut(), 0) }, 0);
        assert_eq!(value.meta().discarded, 3, "unchanged geometry must not recount eviction");
        value.feed(b"\x1b[3J");
        assert_eq!(value.meta().history, 0);
        assert_eq!(value.meta().discarded, 3, "explicit history clear is not capacity eviction");
    }

    #[test]
    fn history_eviction_checks_entire_prefix_before_mutating() {
        let value = Handle(tf_terminal_new(2, 8, 4));
        assert!(!value.0.is_null());
        value.feed(b"A\r\nB\r\nC\r\nD\r\nE\r\nF");
        let before = value.rows();
        let prefix = [before[0].0, before[1].0];
        let mut discarded = u64::MAX;
        let wrong_second = [prefix[0], prefix[0]];
        assert_eq!(unsafe {
            tf_terminal_discard_history(value.0, wrong_second.as_ptr(), 2, &mut discarded, null_mut(), 0)
        }, 1);
        assert_eq!(discarded, u64::MAX);
        assert_eq!(value.rows(), before);
        assert_eq!(value.meta().discarded, 0);

        value.feed(b"\x1b[?1049hALT");
        let alternate = value.rows();
        assert_eq!(unsafe {
            tf_terminal_discard_history(value.0, prefix.as_ptr(), 2, &mut discarded, null_mut(), 0)
        }, 1);
        assert_eq!(value.rows(), alternate);
        value.feed(b"\x1b[?1049l");
        assert_eq!(value.rows(), before);

        let initial = value.meta();
        assert_eq!(unsafe {
            tf_terminal_discard_history(value.0, prefix.as_ptr(), 2, &mut discarded, null_mut(), 0)
        }, 0);
        let after = value.meta();
        assert_eq!(discarded, 2);
        assert_eq!(after.history, 2);
        assert_eq!(value.rows(), before[2..]);
        assert_eq!((after.row, after.column, after.history_clear_epoch),
                   (initial.row, initial.column, initial.history_clear_epoch));
        assert_eq!(unsafe {
            tf_terminal_discard_history(value.0, std::ptr::null(), 0, &mut discarded, null_mut(), 0)
        }, 0);
        assert_eq!(discarded, 2);

        value.feed(b"\r\nG\r\nH");
        assert_eq!(value.meta().history, 4, "eviction must not reduce configured capacity");
        assert_eq!(value.meta().discarded, 2);
        value.feed(b"\r\nI");
        assert_eq!(value.meta().discarded, 3);
        assert_eq!(unsafe { tf_terminal_resize(value.0, 8, 8, null_mut(), 0) }, 0);
        assert!(value.rows().iter().all(|(_, text)| !b"ABC".contains(text)),
                "height growth must not recover evicted storage rows");
    }
}
