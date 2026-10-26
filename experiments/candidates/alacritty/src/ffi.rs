#[allow(dead_code)]
#[path = "main.rs"]
mod trace;

use std::collections::VecDeque;
use std::ffi::{c_int, c_uchar, c_void};
use std::panic::{AssertUnwindSafe, catch_unwind};

use alacritty_terminal::event::Event;
use alacritty_terminal::grid::{Dimensions, GridCell};
use alacritty_terminal::index::{Column, Line};
use alacritty_terminal::term::{Config, Term, cell::{Cell, Flags}};
use alacritty_terminal::vte::ansi::{Color, NamedColor, Processor};
use trace::{Listener, Size};

struct Terminal {
    term: Term<Listener>,
    parser: Processor,
    listener: Listener,
    events: VecDeque<Event>,
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
        let value = Terminal { term, parser: Processor::new(), listener, events: VecDeque::new() };
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

#[unsafe(no_mangle)]
pub unsafe extern "C" fn tf_terminal_feed(
    value: *mut c_void,
    data: *const c_uchar,
    len: usize,
    error: *mut c_uchar,
    error_capacity: usize,
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
        value.parser.advance(&mut value.term, bytes);
        drain_events(value)
    })
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn tf_terminal_finish(
    value: *mut c_void,
    error: *mut c_uchar,
    error_capacity: usize,
) -> c_int {
    guard(error, error_capacity, || {
        unsafe { terminal(value)? };
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
    unsafe { resize_terminal(value, rows, columns, false, error, error_capacity) }
}

unsafe fn resize_terminal(
    value: *mut c_void, rows: usize, columns: usize, preserve_viewport: bool,
    error: *mut c_uchar, error_capacity: usize,
) -> c_int {
    guard(error, error_capacity, || {
        if rows == 0 || rows > 200 || columns == 0 || columns > 500 {
            return Err("invalid candidate dimensions".into());
        }
        let value = unsafe { terminal(value)? };
        if preserve_viewport {
            value.term.resize_native_viewport(Size { columns, rows });
        } else {
            value.term.resize(Size { columns, rows });
        }
        drain_events(value)
    })
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn tf_terminal_resize_native_viewport(
    value: *mut c_void,
    rows: usize,
    columns: usize,
    error: *mut c_uchar,
    error_capacity: usize,
) -> c_int {
    unsafe { resize_terminal(value, rows, columns, true, error, error_capacity) }
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
    };
    unsafe { *output = result };
    0
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

fn wrap_padding(row: &alacritty_terminal::grid::Row<Cell>, column: usize) -> bool {
    // A short retained continuation can need physical padding to keep the
    // native viewport boundary. Its wrap marker does not turn that padding
    // into a written space. Other cell contents or attributes remain visible.
    row.native_wrap_padding() && row.wrap_columns() > 0 && row.wrap_columns() < row.len() && column >= row.wrap_columns()
        && row.last().is_some_and(|cell| cell.flags.contains(Flags::WRAPLINE))
        && row[Column(column)] == Cell { flags: Flags::WRAPLINE, ..Cell::default() }
}

fn row_cell_text(row: &alacritty_terminal::grid::Row<Cell>, column: usize) -> String {
    if wrap_padding(row, column) {
        return String::new();
    }
    let cell = &row[Column(column)];
    // Moving a wrap marker onto padding must not make the original trailing
    // spaces unwritten. They still belong to the retained logical span.
    if row.native_wrap_padding() && column < row.wrap_columns() && cell.is_empty() {
        return " ".into();
    }
    cell_text(cell)
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
    let padding = wrap_padding(row, column);
    let text = row_cell_text(row, column);
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
        written: !padding && (!cell.is_empty() || !text.is_empty() ||
            flags.intersects(Flags::WIDE_CHAR | Flags::WIDE_CHAR_SPACER | Flags::LEADING_WIDE_CHAR_SPACER)),
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
    let text = row_cell_text(row, column);
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
