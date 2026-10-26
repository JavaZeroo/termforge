//! Bounded, immediate streaming extended-grapheme placement.
//!
//! Feed and observation boundaries are deliberately absent from this state machine.
//! A screen/cursor mutation seals the last cluster; SGR and queries are transparent.
use std::mem;

use arrayvec::ArrayString;
use unicode_segmentation::UnicodeSegmentation;
use unicode_width::{UnicodeWidthChar, UnicodeWidthStr};

use crate::grid::Dimensions;

use super::{Cell, Column, EventListener, Flags, Point, Term, TermMode};

pub(crate) const MAX_GRAPHEME_SCALARS: usize = 64;
pub(crate) const MAX_GRAPHEME_BYTES: usize = 256;

#[derive(Default)]
pub(super) struct StreamingGrapheme {
    text: String,
    point: Option<Point>,
    width: usize,
    template: Cell,
    fragment: bool,
}

impl StreamingGrapheme {
    fn reset(&mut self, fragment: bool) {
        self.text.clear();
        self.point = None;
        self.fragment = fragment;
    }

    fn joins(&self, c: char) -> bool {
        if self.point.is_none() {
            return false;
        }
        // Printable ASCII always has a boundary; CR/LF still follow UAX29.
        if self.text.len() == 1
            && (b' '..=b'~').contains(&self.text.as_bytes()[0])
            && (' '..='~').contains(&c)
        {
            return false;
        }
        // Probe the next scalar on a fixed stack buffer. It must not grow the
        // persistent String beyond its 256-byte fragment budget just to decide
        // whether a boundary exists at that budget.
        let mut probe = ArrayString::<{ MAX_GRAPHEME_BYTES + 4 }>::new();
        probe.push_str(&self.text);
        probe.push(c);
        probe.graphemes(true).nth(1).is_none()
    }
}

// Zero-width Prepend scalars need a forward editable prefix. An ordinary
// ASCII base is a boundary after every other zero-width class; query the same
// versioned UAX29 implementation instead of maintaining another property table.
fn is_forward_prefix(c: char) -> bool {
    let mut probe = ArrayString::<5>::new();
    probe.push(c);
    probe.push('A');
    probe.graphemes(true).nth(1).is_none()
}

impl<T> Term<T> {
    /// End the editable suffix without changing its already visible text/cursor.
    pub fn seal_grapheme(&mut self) {
        self.grapheme.reset(false);
    }

    /// Number of lossless bounded fragments, not a count of discarded scalars.
    pub fn grapheme_overflows(&self) -> u64 {
        self.grapheme_overflows
    }
}

impl<T: EventListener> Term<T> {
    pub(super) fn input_grapheme(&mut self, c: char) {
        let c = self.grid.cursor.charsets[self.active_charset].map(c);
        let Some(scalar_width) = c.width() else {
            return;
        };
        // UAX29 always breaks between two printable ASCII scalars. Keep the
        // same editable-prefix contract without moving the entire pending state
        // or cloning an unchanged style template for every byte in an ASCII run.
        if (' '..='~').contains(&c)
            && self.grapheme.point.is_some()
            && self.grapheme.text.len() == 1
            && self.grapheme.text.is_ascii()
        {
            self.grapheme.reset(false);
            let (point, width) = self.place_grapheme(c, 1);
            self.grapheme.text.push(c);
            self.grapheme.point = Some(point);
            self.grapheme.width = width;
            if self.grapheme.template != self.grid.cursor.template {
                self.grapheme.template = self.grid.cursor.template.clone();
            }
            return;
        }
        let mut pending = mem::take(&mut self.grapheme);
        if pending.joins(c) {
            if pending.text.chars().count() == MAX_GRAPHEME_SCALARS
                || pending.text.len() + c.len_utf8() > MAX_GRAPHEME_BYTES
            {
                self.grapheme_overflows = self.grapheme_overflows.saturating_add(1);
                pending.reset(true);
            } else {
                pending.text.push(c);
                self.extend_grapheme(&mut pending);
                self.grapheme = pending;
                return;
            }
        } else {
            pending.reset(false);
        }

        // Preserve the existing degenerate-mark attachment policy after a control
        // boundary. It cannot re-open ZWJ/RI segmentation across that boundary.
        if scalar_width == 0 && !pending.fragment && !is_forward_prefix(c) {
            let mut point = self.grid.cursor.point;
            if !self.grid.cursor.input_needs_wrap {
                point.column.0 = point.column.saturating_sub(1);
            }
            if self.grid[point].flags.contains(Flags::WIDE_CHAR_SPACER) {
                point.column.0 = point.column.saturating_sub(1);
            }
            if self.grid[point].push_zerowidth(c) {
                self.damage
                    .damage_point(Point::new(point.line.0 as usize, point.column));
                self.grapheme = pending;
                return;
            }
            self.grapheme_overflows = self.grapheme_overflows.saturating_add(1);
            pending.fragment = true;
        }

        pending.text.push(c);
        pending.template = self.grid.cursor.template.clone();
        let (point, width) = self.place_grapheme(c, scalar_width.max(1));
        pending.point = Some(point);
        pending.width = width;
        if pending.fragment {
            self.grid[point].set_grapheme_suffix(std::iter::empty(), true);
        }
        self.grapheme = pending;
    }

    /// Place one currently-known prefix using the existing wrap/insert/write paths.
    #[inline]
    fn place_grapheme(&mut self, c: char, requested_width: usize) -> (Point, usize) {
        if self.grid.cursor.input_needs_wrap {
            self.wrapline();
        }
        let columns = self.columns();
        let mut width = requested_width.min(2);
        if width == 2 && self.grid.cursor.point.column + 1 >= columns {
            if columns > 1 && self.mode.contains(TermMode::LINE_WRAP) {
                self.grid
                    .cursor
                    .template
                    .flags
                    .insert(Flags::LEADING_WIDE_CHAR_SPACER);
                self.write_at_cursor(' ');
                self.grid
                    .cursor
                    .template
                    .flags
                    .remove(Flags::LEADING_WIDE_CHAR_SPACER);
                self.wrapline();
            } else {
                // Keep all scalars even when a two-cell glyph cannot fit.
                width = 1;
            }
        }
        if self.mode.contains(TermMode::INSERT) && self.grid.cursor.point.column + width < columns {
            // Inserting at a wide trailer must clear the original pair before
            // shifting; otherwise the shift detaches the trailer from its lead.
            if self
                .grid
                .cursor_cell()
                .flags
                .intersects(Flags::WIDE_CHAR | Flags::WIDE_CHAR_SPACER)
            {
                self.write_at_cursor(' ');
            }
            let line = self.grid.cursor.point.line;
            let col = self.grid.cursor.point.column;
            for col in (col.0..(columns - width)).rev() {
                self.grid[line][..].swap(col + width, col);
            }
            self.clip_inserted_right_edge();
        }
        let point = self.grid.cursor.point;
        self.grid
            .cursor
            .template
            .flags
            .set(Flags::CLIPPED_GRAPHEME, requested_width > width);
        self.grid
            .cursor
            .template
            .flags
            .set(Flags::WIDE_CHAR, width == 2);
        self.write_at_cursor(c);
        self.grid
            .cursor
            .template
            .flags
            .remove(Flags::WIDE_CHAR | Flags::CLIPPED_GRAPHEME);
        if width == 2 {
            self.grid.cursor.point.column += 1;
            self.grid
                .cursor
                .template
                .flags
                .insert(Flags::WIDE_CHAR_SPACER);
            self.write_at_cursor(' ');
            self.grid
                .cursor
                .template
                .flags
                .remove(Flags::WIDE_CHAR_SPACER);
        }
        self.advance_grapheme_cursor(point, width);
        (point, width)
    }

    fn advance_grapheme_cursor(&mut self, point: Point, width: usize) {
        self.grid.cursor.point = point;
        let next = point.column.0 + width;
        self.grid.cursor.point.column = Column(next.min(self.columns() - 1));
        self.grid.cursor.input_needs_wrap = next >= self.columns();
        self.damage.damage_line(
            point.line.0 as usize,
            point.column.0,
            (point.column.0 + width - 1).min(self.columns() - 1),
        );
    }

    fn extend_grapheme(&mut self, pending: &mut StreamingGrapheme) {
        let point = pending.point.expect("editable grapheme placement");
        let requested = pending.text.width().max(1);
        let available = self.columns() - point.column.0;
        let width = requested.min(2).min(available);
        let live_template = mem::replace(&mut self.grid.cursor.template, pending.template.clone());
        if requested >= 2
            && available == 1
            && self.columns() > 1
            && self.mode.contains(TermMode::LINE_WRAP)
        {
            // The visible narrow prefix becomes the normal leading-wide spacer.
            self.grid.cursor.point = point;
            self.grid.cursor.input_needs_wrap = false;
            let (new_point, width) =
                self.place_grapheme(pending.text.chars().next().unwrap(), requested);
            pending.point = Some(new_point);
            pending.width = width;
        } else {
            if width > pending.width && self.mode.contains(TermMode::INSERT) {
                self.shift_grapheme_tail(point, pending.width, width);
            }
            if width != pending.width {
                // The writer owns neighboring wide-cell cleanup. Shrink keeps the
                // prefix's prior wrap/overwrite side effects; it never unscrolls.
                self.grid.cursor.point = Point::new(point.line, point.column + 1);
                if width == 2 {
                    self.grid
                        .cursor
                        .template
                        .flags
                        .insert(Flags::WIDE_CHAR_SPACER);
                }
                self.write_at_cursor(' ');
                self.grid
                    .cursor
                    .template
                    .flags
                    .remove(Flags::WIDE_CHAR_SPACER);
            }
            if width < pending.width && self.mode.contains(TermMode::INSERT) {
                self.shift_grapheme_tail(point, pending.width, width);
            }
            let cell = &mut self.grid[point];
            *cell = pending.template.clone();
            cell.c = pending.text.chars().next().unwrap();
            cell.flags.set(Flags::WIDE_CHAR, width == 2);
            cell.flags.set(Flags::CLIPPED_GRAPHEME, requested > width);
            pending.width = width;
            self.advance_grapheme_cursor(point, width);
        }
        let point = pending.point.unwrap();
        self.grid[point].set_grapheme_suffix(pending.text.chars().skip(1), pending.fragment);
        self.damage.damage_line(
            point.line.0 as usize,
            point.column.0,
            (point.column.0 + pending.width - 1).min(self.columns() - 1),
        );
        self.grid.cursor.template = live_template;
    }

    fn shift_grapheme_tail(&mut self, point: Point, old_width: usize, new_width: usize) {
        let columns = self.columns();
        let row = &mut self.grid[point.line][..];
        if new_width > old_width {
            for column in ((point.column.0 + old_width)..columns - 1).rev() {
                row.swap(column + 1, column);
            }
        } else {
            for column in (point.column.0 + new_width)..columns - 1 {
                row.swap(column, column + 1);
            }
            row[columns - 1] = Cell::default();
        }
        self.clip_inserted_right_edge();
        self.damage
            .damage_line(point.line.0 as usize, point.column.0, columns - 1);
    }

    // INSERT cropping must remove a whole wide glyph, including at the
    // immediately visible one-scalar prefix before a later width increase.
    fn clip_inserted_right_edge(&mut self) {
        let line = self.grid.cursor.point.line;
        let last = Column(self.columns() - 1);
        if self.grid[line][last].flags.contains(Flags::WIDE_CHAR) {
            self.grid[line][last].clear_wide();
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::event::Event;
    use crate::index::Line;
    use crate::term::{Config, test::TermSize};
    use crate::vte::ansi::{Color, NamedColor, Processor};
    use std::sync::{Arc, Mutex};

    #[derive(Clone, Default)]
    struct Listener(Arc<Mutex<Vec<String>>>);
    impl EventListener for Listener {
        fn send_event(&self, event: Event) {
            if let Event::PtyWrite(text) = event {
                self.0.lock().unwrap().push(text);
            }
        }
    }
    fn term(columns: usize) -> Term<Listener> {
        Term::new(
            Config {
                scrolling_history: 8,
                ..Config::default()
            },
            &TermSize::new(columns, 3),
            Listener::default(),
        )
    }
    fn feed(term: &mut Term<Listener>, text: &str, fragmented: bool) {
        let mut parser: Processor = Processor::new();
        if fragmented {
            for byte in text.bytes() {
                parser.advance(term, &[byte]);
            }
        } else {
            parser.advance(term, text.as_bytes());
        }
    }
    fn text(cell: &Cell) -> String {
        let mut text = cell.c.to_string();
        text.extend(cell.zerowidth().into_iter().flatten());
        text
    }
    fn assert_cluster(term: &Term<Listener>, row: i32, column: usize, value: &str, width: usize) {
        let cell = &term.grid()[Line(row)][Column(column)];
        assert_eq!(text(cell), value);
        assert_eq!(cell.flags.contains(Flags::WIDE_CHAR), width == 2);
        if width == 2 {
            assert!(
                term.grid()[Line(row)][Column(column + 1)]
                    .flags
                    .contains(Flags::WIDE_CHAR_SPACER)
            );
        }
    }
    #[test]
    fn unicode17_normative_grapheme_break_data_matches_streaming_boundaries() {
        assert_eq!(unicode_segmentation::UNICODE_VERSION, (17, 0, 0));
        assert_eq!(unicode_width::UNICODE_VERSION, (17, 0, 0));
        let data = include_str!("../../tests/data/GraphemeBreakTest-17.0.0.txt");
        let mut tested = 0;
        for (number, line) in data.lines().enumerate() {
            let line = line.split('#').next().unwrap().trim();
            if line.is_empty() {
                continue;
            }
            let tokens: Vec<_> = line.split_whitespace().collect();
            let mut pending = StreamingGrapheme::default();
            pending.point = Some(Point::new(Line(0), Column(0)));
            for pair in tokens[..tokens.len() - 1].chunks_exact(2) {
                let c = char::from_u32(u32::from_str_radix(pair[1], 16).unwrap()).unwrap();
                if !pending.text.is_empty() {
                    assert_eq!(
                        pending.joins(c),
                        pair[0] == "×",
                        "official case line {}",
                        number + 1
                    );
                    if pair[0] == "÷" {
                        pending.text.clear();
                    }
                }
                pending.text.push(c);
            }
            tested += 1;
        }
        assert_eq!(tested, 766);
    }
    #[test]
    fn fixed_zwj_keycap_whole_and_byte_are_one_two_cell_cluster() {
        for fragmented in [false, true] {
            for value in ["👩\u{200d}💻", "1\u{fe0f}\u{20e3}"] {
                let mut term = term(8);
                feed(&mut term, value, fragmented);
                assert_cluster(&term, 0, 0, value, 2);
                assert_eq!(term.grid.cursor.point, Point::new(Line(0), Column(2)));
            }
        }
    }
    #[test]
    fn ordinary_text_ri_parity_non_emoji_zwj_and_normalization_are_not_overjoined() {
        let mut term = term(24);
        feed(&mut term, "Ae\u{301}x\u{200d}y🇨🇳🇯B", true);
        assert_cluster(&term, 0, 0, "A", 1);
        assert_cluster(&term, 0, 1, "e\u{301}", 1);
        assert_cluster(&term, 0, 2, "x\u{200d}", 1);
        assert_cluster(&term, 0, 3, "y", 1);
        assert_cluster(&term, 0, 4, "🇨🇳", 2);
        assert_cluster(&term, 0, 6, "🇯", 1);
        assert_cluster(&term, 0, 7, "B", 1);
    }
    #[test]
    fn prefix_is_visible_and_query_sgr_and_observation_are_transparent() {
        let mut term = term(8);
        let mut parser: Processor = Processor::new();
        parser.advance(&mut term, "1".as_bytes());
        assert_cluster(&term, 0, 0, "1", 1);
        assert_eq!(term.grid.cursor.point.column, Column(1));
        let _ = term.grid();
        let _ = term.renderable_content();
        parser.advance(&mut term, b"\x1b[6n\x1b[31m");
        parser.advance(&mut term, "\u{fe0f}\u{20e3}".as_bytes());
        assert_cluster(&term, 0, 0, "1\u{fe0f}\u{20e3}", 2);
        assert_eq!(
            term.grid()[Line(0)][Column(0)].fg,
            Color::Named(NamedColor::Foreground)
        );
        assert_eq!(*term.event_proxy.0.lock().unwrap(), vec!["\x1b[1;2R"]);
        parser.advance(&mut term, b"X");
        assert_eq!(
            term.grid()[Line(0)][Column(2)].fg,
            Color::Named(NamedColor::Red)
        );
    }
    #[test]
    fn growth_at_right_margin_wraps_whole_cluster_and_shrink_keeps_prior_wrap() {
        let mut growing = term(4);
        feed(&mut growing, "ABC1\u{fe0f}\u{20e3}", true);
        assert_cluster(&growing, 1, 0, "1\u{fe0f}\u{20e3}", 2);
        assert!(
            growing.grid()[Line(0)][Column(3)]
                .flags
                .contains(Flags::LEADING_WIDE_CHAR_SPACER)
        );
        let mut shrinking = term(4);
        feed(&mut shrinking, "ABC⌚\u{fe0e}", true);
        assert_cluster(&shrinking, 1, 0, "⌚\u{fe0e}", 1);
        assert_eq!(shrinking.grid.cursor.point, Point::new(Line(1), Column(1)));
    }
    #[test]
    fn no_autowrap_and_one_column_clip_footprint_but_keep_complete_text() {
        for value in ["1\u{fe0f}\u{20e3}", "👩\u{200d}💻"] {
            let mut term = term(4);
            feed(&mut term, &format!("\x1b[?7lABC{value}"), true);
            assert_cluster(&term, 0, 3, value, 1);
            assert!(
                term.grid()[Line(0)][Column(3)]
                    .flags
                    .contains(Flags::CLIPPED_GRAPHEME)
            );
            assert!(term.grid.cursor.input_needs_wrap);
            let mut single = super::tests::term(1);
            feed(&mut single, value, true);
            assert_cluster(&single, 0, 0, value, 1);
            assert!(
                single.grid()[Line(0)][Column(0)]
                    .flags
                    .contains(Flags::CLIPPED_GRAPHEME)
            );
        }
    }
    #[test]
    fn shrinking_a_wide_cluster_to_one_column_terminates_and_retains_text_in_both_buffers() {
        let mut term = term(4);
        feed(&mut term, "👩\u{200d}💻", true);
        term.resize(TermSize::new(1, 3));
        assert_cluster(&term, 0, 0, "👩\u{200d}💻", 1);
        assert!(
            term.grid()[Line(0)][Column(0)]
                .flags
                .contains(Flags::CLIPPED_GRAPHEME)
        );
        assert_eq!(term.grid.cursor.point, Point::new(Line(0), Column(0)));
        assert!(term.grid.cursor.input_needs_wrap);
        term.resize(TermSize::new(4, 3));
        feed(&mut term, "\x1b[?1049h\x1b[H1\u{fe0f}\u{20e3}", true);
        term.resize(TermSize::new(1, 3));
        assert_cluster(&term, 0, 0, "1\u{fe0f}\u{20e3}", 1);
        feed(&mut term, "\x1b[?1049l", true);
        assert_cluster(&term, 0, 0, "👩\u{200d}💻", 1);
        assert!(
            term.grid()[Line(0)][Column(0)]
                .flags
                .contains(Flags::CLIPPED_GRAPHEME)
        );
    }
    #[test]
    fn insert_growth_and_shrink_shift_tail_by_width_delta_only() {
        for (value, expected_column) in [("1\u{fe0f}\u{20e3}", 2), ("⌚\u{fe0e}", 1)] {
            let mut term = term(8);
            feed(&mut term, &format!("ABCDEF\r\x1b[4h{value}"), true);
            assert_cluster(&term, 0, 0, value, expected_column);
            assert_cluster(&term, 0, expected_column, "A", 1);
            assert_cluster(&term, 0, expected_column + 1, "B", 1);
            assert_cluster(&term, 0, 7, if expected_column == 2 { "F" } else { " " }, 1);
        }
    }
    #[test]
    fn insert_into_a_wide_trailer_clears_the_pair_before_every_prefix_shift() {
        for (value, expected) in [
            ("X", " X ABCDE"),
            ("1\u{fe0f}\u{20e3}", " 1\u{fe0f}\u{20e3} ABCD"),
            ("⌚\u{fe0e}", " ⌚\u{fe0e} ABCD"),
        ] {
            let mut term = term(8);
            let mut parser: Processor = Processor::new();
            parser.advance(&mut term, "中ABCDEF\x1b[1;2H\x1b[4h".as_bytes());
            for byte in value.bytes() {
                parser.advance(&mut term, &[byte]);
                for column in 0..8 {
                    let flags = term.grid()[Line(0)][Column(column)].flags;
                    if flags.contains(Flags::WIDE_CHAR) {
                        assert!(
                            column < 7
                                && term.grid()[Line(0)][Column(column + 1)]
                                    .flags
                                    .contains(Flags::WIDE_CHAR_SPACER)
                        );
                    }
                    if flags.contains(Flags::WIDE_CHAR_SPACER) {
                        assert!(
                            column > 0
                                && term.grid()[Line(0)][Column(column - 1)]
                                    .flags
                                    .contains(Flags::WIDE_CHAR)
                        );
                    }
                }
            }
            let projected = (0..8)
                .filter(|column| {
                    !term.grid()[Line(0)][Column(*column)]
                        .flags
                        .contains(Flags::WIDE_CHAR_SPACER)
                })
                .map(|column| text(&term.grid()[Line(0)][Column(column)]))
                .collect::<String>();
            assert_eq!(projected.trim_end(), expected);
        }
    }
    #[test]
    fn zero_width_prepend_is_a_visible_forward_prefix_and_preserves_its_scalar() {
        let mut prefix = term(8);
        let mut parser: Processor = Processor::new();
        parser.advance(&mut prefix, "\u{70f}".as_bytes());
        assert_cluster(&prefix, 0, 0, "\u{70f}", 1);
        let _ = prefix.renderable_content();
        parser.advance(&mut prefix, b"A");
        assert_cluster(&prefix, 0, 0, "\u{70f}A", 1);
        assert_eq!(prefix.grid.cursor.point.column, Column(1));
        for fragmented in [false, true] {
            let mut term = term(8);
            feed(&mut term, "\u{70f}A", fragmented);
            assert_cluster(&term, 0, 0, "\u{70f}A", 1);
        }
    }
    #[test]
    fn insert_prefix_and_width_growth_never_leave_a_wide_lead_at_cropped_right_edge() {
        let mut term = term(8);
        let mut parser: Processor = Processor::new();
        parser.advance(&mut term, "ABCDEF中\r\x1b[4h1".as_bytes());
        assert!(
            !term.grid()[Line(0)][Column(7)]
                .flags
                .contains(Flags::WIDE_CHAR),
            "one-cell insert cropped the trailing half but retained its wide lead"
        );
        parser.advance(&mut term, "\u{fe0f}\u{20e3}".as_bytes());
        assert_cluster(&term, 0, 0, "1\u{fe0f}\u{20e3}", 2);
        for column in 0..8 {
            let flags = term.grid()[Line(0)][Column(column)].flags;
            if flags.contains(Flags::WIDE_CHAR) {
                assert!(
                    column < 7
                        && term.grid()[Line(0)][Column(column + 1)]
                            .flags
                            .contains(Flags::WIDE_CHAR_SPACER)
                );
            }
            if flags.contains(Flags::WIDE_CHAR_SPACER) {
                assert!(
                    column > 0
                        && term.grid()[Line(0)][Column(column - 1)]
                            .flags
                            .contains(Flags::WIDE_CHAR)
                );
            }
        }
    }
    #[test]
    fn cursor_erase_newline_resize_and_alt_mutations_seal_zwj_context() {
        for barrier in [
            "\x1b[1C",
            "\x1b[X",
            "\r\n",
            "\x1b[?1049h\x1b[?1049l",
            "\x1bc",
        ] {
            let mut term = term(12);
            feed(&mut term, &format!("👩\u{200d}{barrier}💻"), true);
            assert!(
                term.grid()
                    .display_iter()
                    .all(|item| text(item.cell) != "👩\u{200d}💻")
            );
        }
        let mut term = term(12);
        feed(&mut term, "👩\u{200d}", true);
        term.resize(TermSize::new(14, 3));
        feed(&mut term, "💻", true);
        assert_cluster(&term, 0, 0, "👩\u{200d}", 2);
        assert_cluster(&term, 0, 2, "💻", 2);
    }
    #[test]
    fn long_combining_input_spills_without_dropping_or_normalizing_scalars() {
        let value = format!("e{}", "\u{301}".repeat(200));
        let mut term = term(16);
        feed(&mut term, &value, true);
        let actual = (0..4)
            .map(|column| text(&term.grid()[Line(0)][Column(column)]))
            .collect::<String>();
        assert_eq!(actual, value);
        assert_eq!(term.grapheme_overflows(), 3);
        assert_eq!(
            term.grid()[Line(0)][Column(0)].zerowidth().unwrap().len(),
            63
        );
        assert!(term.grid()[Line(0)][Column(1)].grapheme_fragment());
        assert_eq!(term.grapheme.text.chars().count(), 9);
        assert!(term.grapheme.text.len() <= MAX_GRAPHEME_BYTES);
    }
}
