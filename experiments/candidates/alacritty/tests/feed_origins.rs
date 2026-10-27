use std::cell::RefCell;
use std::rc::Rc;

use alacritty_terminal::event::{Event, EventListener, VoidListener};
use alacritty_terminal::grid::Dimensions;
use alacritty_terminal::index::{Column, Line};
use alacritty_terminal::term::{Config, Term, TerminalResizeMap};
use alacritty_terminal::term::cell::Flags;
use alacritty_terminal::vte::ansi::Processor;

struct Size { rows: usize, columns: usize }

impl Dimensions for Size {
    fn total_lines(&self) -> usize { self.rows }
    fn screen_lines(&self) -> usize { self.rows }
    fn columns(&self) -> usize { self.columns }
}

fn terminal(rows: usize, columns: usize, bytes: &[u8]) -> (Term<VoidListener>, Processor) {
    let mut term = Term::new(Config::default(), &Size { rows, columns }, VoidListener);
    let mut parser = Processor::new();
    parser.advance(&mut term, bytes);
    (term, parser)
}

fn spans(map: &TerminalResizeMap, line: i32) -> Vec<(u64, usize, usize, usize)> {
    map.primary.target_rows.iter().find(|row| row.line == line).unwrap().origins.spans
        .iter().map(|span| (span.source_row, span.source_column, span.column, span.len)).collect()
}

fn row_text(term: &Term<VoidListener>, line: i32) -> String {
    (0..term.columns()).map(|col| term.grid()[Line(line)][Column(col)].c).collect()
}

#[test]
fn writing_equal_text_replaces_only_the_written_source_cells() {
    let mut term = Term::new(Config::default(), &Size { rows: 2, columns: 8 }, VoidListener);
    let mut parser = Processor::new();
    parser.advance(&mut term, b"ABCDEFGH\r\nX");
    let source = term.grid()[Line(0)].id();

    // The final bytes are unchanged. Content comparison must not mistake the
    // explicit rewrite of C/D for survival of those two old source cells.
    let map = term.feed_with_origin_map(&mut parser, b"\x1b[1;3HCD");
    let row = &map.primary.target_rows[0];
    let origins: Vec<_> = row.origins.spans.iter().map(|span|
        (span.source_row, span.source_column, span.column, span.len)).collect();
    assert_eq!(origins, vec![(source, 0, 0, 2), (source, 4, 4, 4)]);
    assert_eq!(row.origins.hard_boundary, Some(source));
    let text: String = (0..8).map(|col| term.grid()[Line(0)][Column(col)].c).collect();
    assert_eq!(text, "ABCDEFGH");
    assert!(term.grid()[Line(0)].origins().is_none());
}

#[test]
fn mapping_a_feed_preserves_the_open_grapheme_but_replaces_its_origins() {
    let mut term = Term::new(Config::default(), &Size { rows: 2, columns: 8 }, VoidListener);
    let mut parser = Processor::new();
    parser.advance(&mut term, b"1");
    let source = term.grid()[Line(0)].id();
    let map = term.feed_with_origin_map(&mut parser, "\u{fe0f}\u{20e3}".as_bytes());
    let cell = &term.grid()[Line(0)][Column(0)];
    assert_eq!(cell.c, '1');
    assert_eq!(cell.zerowidth().unwrap(), &['\u{fe0f}', '\u{20e3}']);
    assert!(cell.flags.contains(Flags::WIDE_CHAR));
    assert_eq!(term.grid().cursor.point.column, Column(2));
    let origins: Vec<_> = map.primary.target_rows[0].origins.spans.iter().map(|span|
        (span.source_row, span.source_column, span.column, span.len)).collect();
    assert_eq!(origins, vec![(source, 2, 2, 6)]);
}

#[test]
fn inserted_cells_move_old_origins_without_owning_the_new_blanks() {
    let mut term = Term::new(Config::default(), &Size { rows: 2, columns: 8 }, VoidListener);
    let mut parser = Processor::new();
    parser.advance(&mut term, b"ABCDEFGH\r\nX");
    let source = term.grid()[Line(0)].id();
    let map = term.feed_with_origin_map(&mut parser, b"\x1b[1;3H\x1b[2@");
    let text: String = (0..8).map(|col| term.grid()[Line(0)][Column(col)].c).collect();
    assert_eq!(text, "AB  CDEF");
    let origins: Vec<_> = map.primary.target_rows[0].origins.spans.iter().map(|span|
        (span.source_row, span.source_column, span.column, span.len)).collect();
    assert_eq!(origins, vec![(source, 0, 0, 2), (source, 2, 4, 4)]);
}

#[test]
fn deleted_cells_follow_the_existing_swaps_and_tail_clear_even_at_the_right_edge() {
    for (bytes, text, expected) in [
        (&b"\x1b[1;3H\x1b[2P"[..], "ABEFGH  ", vec![(0, 0, 2), (4, 2, 4)]),
        (&b"\x1b[1;7H\x1b[4P"[..], "ABCD    ", vec![(0, 0, 4)]),
    ] {
        let (mut term, mut parser) = terminal(2, 8, b"ABCDEFGH\r\nX");
        let source = term.grid()[Line(0)].id();
        let map = term.feed_with_origin_map(&mut parser, bytes);
        assert_eq!(row_text(&term, 0), text);
        assert_eq!(spans(&map, 0), expected.into_iter().map(|(src, dst, len)|
            (source, src, dst, len)).collect::<Vec<_>>());
    }
}

#[test]
fn erased_cells_leave_both_neighboring_sources_in_place() {
    let (mut term, mut parser) = terminal(2, 8, b"AB  EFGH\r\nX");
    let source = term.grid()[Line(0)].id();
    let map = term.feed_with_origin_map(&mut parser, b"\x1b[1;3H\x1b[2X");
    assert_eq!(row_text(&term, 0), "AB  EFGH");
    assert_eq!(spans(&map, 0), vec![(source, 0, 0, 2), (source, 4, 4, 4)]);
}

#[test]
fn line_clear_tracks_its_actual_range_and_preserves_the_pending_wrap_noop() {
    for (bytes, expected) in [
        (&b"\x1b[1;3H\x1b[K"[..], vec![(0, 0, 2)]),
        (&b"\x1b[1;3H\x1b[1K"[..], vec![(3, 3, 5)]),
        (&b"\x1b[1;3H\x1b[2K"[..], vec![]),
    ] {
        let (mut term, mut parser) = terminal(2, 8, b"ABCDEFGH\r\nX");
        let source = term.grid()[Line(0)].id();
        let map = term.feed_with_origin_map(&mut parser, bytes);
        assert_eq!(spans(&map, 0), expected.into_iter().map(|(src, dst, len)|
            (source, src, dst, len)).collect::<Vec<_>>());
        assert_eq!(map.primary.target_rows[0].origins.hard_boundary, Some(source));
    }
    let (mut term, mut parser) = terminal(2, 8, b"ABCDEFGH");
    let source = term.grid()[Line(0)].id();
    let map = term.feed_with_origin_map(&mut parser, b"\x1b[K");
    assert_eq!(row_text(&term, 0), "ABCDEFGH");
    assert_eq!(spans(&map, 0), vec![(source, 0, 0, 8)]);
}

#[test]
fn screen_clear_discards_reset_rows_but_only_overwritten_cells_in_the_partial_row() {
    for (bytes, remaining) in [
        (&b"\x1b[2;3H\x1b[J"[..], (0, 0, 2)),
        (&b"\x1b[2;3H\x1b[1J"[..], (3, 3, 5)),
    ] {
        let (mut term, mut parser) = terminal(3, 8, b"ABCDEFGH\r\nIJKLMNOP\r\nQRSTUVWX");
        let source = term.grid()[Line(1)].id();
        let map = term.feed_with_origin_map(&mut parser, bytes);
        assert_eq!(spans(&map, 1), vec![(source, remaining.0, remaining.1, remaining.2)]);
        let reset_line = if remaining.0 == 0 { 2 } else { 0 };
        assert!(spans(&map, reset_line).is_empty());
        assert_eq!(map.primary.target_rows[reset_line as usize].origins.hard_boundary, None);
        assert_eq!(map.primary.target_rows[1].origins.hard_boundary, Some(source));
    }
    let (mut term, mut parser) = terminal(3, 8, b"A\r\nB\r\nC");
    let map = term.feed_with_origin_map(&mut parser, b"\x1b[2J");
    assert!(map.primary.target_rows.iter().all(|row|
        row.origins.spans.is_empty() && row.origins.hard_boundary.is_none()));
}

#[test]
fn tab_replaces_only_an_empty_cell_and_keeps_a_nonempty_cell_source() {
    for (initial, expected, first) in [
        (&b"\r"[..], vec![(1, 1, 7)], '\t'),
        (&b"ABCDEFGH\r"[..], vec![(0, 0, 8)], 'A'),
    ] {
        let (mut term, mut parser) = terminal(2, 8, initial);
        let source = term.grid()[Line(0)].id();
        let map = term.feed_with_origin_map(&mut parser, b"\t");
        assert_eq!(term.grid()[Line(0)][Column(0)].c, first);
        assert_eq!(spans(&map, 0), expected.into_iter().map(|(src, dst, len)|
            (source, src, dst, len)).collect::<Vec<_>>());
    }
}

#[test]
fn alignment_test_replaces_every_visible_source_even_when_the_letters_are_equal() {
    let (mut term, mut parser) = terminal(2, 8, b"EEEEEEEE\r\nEEEEEEEE");
    let map = term.feed_with_origin_map(&mut parser, b"\x1b#8");
    assert_eq!(row_text(&term, 0), "EEEEEEEE");
    assert_eq!(row_text(&term, 1), "EEEEEEEE");
    assert!(map.primary.target_rows.iter().all(|row| row.origins.spans.is_empty()));
}

#[test]
fn a_combining_mark_attached_after_a_control_replaces_the_old_lead_source() {
    let (mut term, mut parser) = terminal(2, 8, b"e\x1b[1;2H");
    let source = term.grid()[Line(0)].id();
    let map = term.feed_with_origin_map(&mut parser, "\u{301}".as_bytes());
    assert_eq!(term.grid()[Line(0)][Column(0)].zerowidth().unwrap(), &['\u{301}']);
    assert_eq!(spans(&map, 0), vec![(source, 1, 1, 7)]);
}

#[test]
fn insert_mode_moves_the_old_suffix_before_writing_the_new_character() {
    let (mut term, mut parser) = terminal(2, 8, b"ABCDEFGH\r\x1b[4h");
    let source = term.grid()[Line(0)].id();
    let map = term.feed_with_origin_map(&mut parser, b"x");
    assert_eq!(row_text(&term, 0), "xABCDEFG");
    assert_eq!(spans(&map, 0), vec![(source, 0, 1, 7)]);
}

#[test]
fn widening_an_open_grapheme_in_insert_mode_moves_the_old_tail() {
    let (mut term, mut parser) = terminal(2, 8, b"ABCDEF\r\x1b[4h1");
    let source = term.grid()[Line(0)].id();
    let map = term.feed_with_origin_map(&mut parser, "\u{fe0f}\u{20e3}".as_bytes());
    assert_eq!(row_text(&term, 0), "1 ABCDEF");
    assert_eq!(term.grid().cursor.point.column, Column(2));
    assert_eq!(spans(&map, 0), vec![(source, 1, 2, 6)]);
}

#[test]
fn narrowing_an_open_grapheme_in_insert_mode_moves_the_tail_and_replaces_padding() {
    let (mut term, mut parser) = terminal(2, 8, "ABCDEF\r\x1b[4h⌚".as_bytes());
    let source = term.grid()[Line(0)].id();
    let map = term.feed_with_origin_map(&mut parser, "\u{fe0e}".as_bytes());
    assert_eq!(row_text(&term, 0), "⌚ABCDEF ");
    assert_eq!(term.grid().cursor.point.column, Column(1));
    assert_eq!(spans(&map, 0), vec![(source, 2, 1, 6)]);
}

#[test]
fn insert_mode_clipping_clears_the_displaced_wide_lead_source() {
    let (mut term, mut parser) = terminal(2, 8, "ABCDEF界\r\x1b[4h".as_bytes());
    let source = term.grid()[Line(0)].id();
    let map = term.feed_with_origin_map(&mut parser, b"x");
    assert_eq!(row_text(&term, 0), "xABCDEF ");
    assert_eq!(spans(&map, 0), vec![(source, 0, 1, 6)]);
}

#[test]
fn repeated_hard_linefeed_keeps_its_boundary_and_a_new_boundary_has_no_old_source() {
    let (mut term, mut parser) = terminal(3, 4, b"ABCD\r\nX");
    let source = term.grid()[Line(0)].id();
    let repeated = term.feed_with_origin_map(&mut parser, b"\x1b[1;1H\n");
    assert_eq!(repeated.primary.target_rows[0].origins.hard_boundary, Some(source));

    let (mut term, mut parser) = terminal(3, 4, b"A");
    let source = term.grid()[Line(0)].id();
    let map = term.feed_with_origin_map(&mut parser, b"\n");
    assert!(term.grid()[Line(0)].hard_break());
    assert_eq!(map.primary.target_rows[0].origins.hard_boundary, None);
    assert_eq!(spans(&map, 0), vec![(source, 0, 0, 4)]);
}

#[test]
fn automatic_wrap_removes_the_old_hard_boundary_without_changing_cell_survivors() {
    let (mut term, mut parser) = terminal(3, 4, b"ABCD\r\nX");
    let source = term.grid()[Line(0)].id();
    let map = term.feed_with_origin_map(&mut parser, b"\x1b[1;4HEF");
    assert_eq!(row_text(&term, 0), "ABCE");
    assert_eq!(row_text(&term, 1), "F   ");
    assert_eq!(term.grid().cursor.point.line, Line(1));
    assert_eq!(term.grid().cursor.point.column, Column(1));
    assert!(term.grid()[Line(0)][Column(3)].flags.contains(Flags::WRAPLINE));
    assert_eq!(spans(&map, 0), vec![(source, 0, 0, 3)]);
    assert!(!term.grid()[Line(0)].hard_break());
    assert_eq!(map.primary.target_rows[0].origins.hard_boundary, None);
}

#[test]
fn line_edits_and_region_scrolls_move_whole_row_sources_and_discard_reset_rows() {
    for history in [0, 10] {
        for (operation, expected) in [
            (&b"\x1b[L"[..], [Some(0), Some(1), None, Some(2), Some(4)]),
            (&b"\x1b[M"[..], [Some(0), Some(1), Some(3), None, Some(4)]),
            (&b"\x1b[S"[..], [Some(0), Some(2), Some(3), None, Some(4)]),
            (&b"\x1b[T"[..], [Some(0), None, Some(1), Some(2), Some(4)]),
            (&b"\x1b[99L"[..], [Some(0), Some(1), None, None, Some(4)]),
            (&b"\x1b[99M"[..], [Some(0), Some(1), None, None, Some(4)]),
        ] {
            let mut config = Config::default();
            config.scrolling_history = history;
            let mut term = Term::new(config, &Size { rows: 5, columns: 4 }, VoidListener);
            let mut parser = Processor::new();
            if history != 0 {
                parser.advance(&mut term, b"ZZZZ\r\n");
            }
            parser.advance(&mut term, b"AAAA\r\nBBBB\r\nCCCC\r\nDDDD\r\nEEEE\x1b[2;4r\x1b[3;1H");
            let source: Vec<_> = (0..5).map(|line| term.grid()[Line(line)].id()).collect();
            let map = term.feed_with_origin_map(&mut parser, operation);
            for (line, old) in expected.into_iter().enumerate() {
                let expected = old.map(|old| vec![(source[old], 0, 0, 4)]).unwrap_or_default();
                assert_eq!(spans(&map, line as i32), expected, "history={history} op={operation:?} line={line}");
                let boundary = old.filter(|old| *old < 4).map(|old| source[old]);
                let target = map.primary.target_rows.iter().find(|row| row.line == line as i32).unwrap();
                assert_eq!(target.origins.hard_boundary, boundary);
            }
            assert!(map.primary.removed_rows.is_empty());
        }
    }
}

#[test]
fn a_capacity_eviction_carries_surviving_origins_and_is_absent_from_target_rows() {
    let mut config = Config::default();
    config.scrolling_history = 1;
    let mut term = Term::new(config, &Size { rows: 2, columns: 4 }, VoidListener);
    let mut parser = Processor::new();
    parser.advance(&mut term, b"AAAA\r\nBBBB\r\nCCCC");
    let source: Vec<_> = (-1..2).map(|line| term.grid()[Line(line)].id()).collect();
    let map = term.feed_with_origin_map(&mut parser, b"\r\nD");
    assert_eq!(map.primary.removed_rows.len(), 1);
    assert_eq!(map.primary.removed_rows[0].row.origins.spans[0].source_row, source[0]);
    assert_eq!(spans(&map, -1), vec![(source[1], 0, 0, 4)]);
    assert_eq!(spans(&map, 0), vec![(source[2], 0, 0, 4)]);
    assert!(spans(&map, 1).is_empty());
    assert_eq!(map.primary.discarded_after, map.primary.discarded_before + 1);
    assert!(map.primary.target_rows.iter().all(|row|
        row.origins.spans.iter().all(|span| span.source_row != source[0])));
}

#[test]
fn buffer_names_survive_odd_switches_roundtrips_reentry_and_reset() {
    for (start_alt, bytes, clear_alt, reset) in [
        (false, &b"\x1b[?1049hA"[..], true, false),
        (false, &b"\x1b[?1049hA\x1b[?1049l"[..], true, false),
        (true, &b"\x1b[?1049l"[..], false, false),
        (true, &b"\x1b[?1049h"[..], false, false),
        (true, &b"\x1b[?1049l\x1b[?1049hB"[..], true, false),
        (false, &b"\x1bc"[..], true, true),
        (true, &b"\x1bc"[..], true, true),
    ] {
        let (mut term, mut parser) = terminal(2, 8, b"P\r\nQ");
        parser.advance(&mut term, b"\x1b[?1049hA\r\nB");
        if !start_alt {
            parser.advance(&mut term, b"\x1b[?1049l");
        }
        let before = term.feed_with_origin_map(&mut parser, b"");
        let map = term.feed_with_origin_map(&mut parser, bytes);
        for (old, new, cleared) in [
            (&before.primary, &map.primary, reset),
            (&before.alternate, &map.alternate, clear_alt),
        ] {
            assert_eq!(old.source_rows.iter().map(|row| row.id).collect::<Vec<_>>(),
                new.source_rows.iter().map(|row| row.id).collect::<Vec<_>>());
            for (index, row) in new.target_rows.iter().enumerate() {
                if cleared {
                    assert!(row.origins.spans.is_empty(), "start_alt={start_alt} bytes={bytes:?}");
                    assert_eq!(row.origins.hard_boundary, None);
                } else {
                    assert_eq!(row.origins.spans, old.target_rows[index].origins.spans);
                    assert_eq!(row.origins.hard_boundary, old.target_rows[index].origins.hard_boundary);
                }
            }
        }
    }
}

#[test]
fn overwriting_a_wide_pair_drops_both_cells_and_its_leading_wrap_spacer() {
    for column in [2, 3] {
        let (mut term, mut parser) = terminal(2, 8, "A界BCD".as_bytes());
        let source = term.grid()[Line(0)].id();
        let bytes = format!("\x1b[1;{column}Hx");
        let map = term.feed_with_origin_map(&mut parser, bytes.as_bytes());
        assert_eq!(spans(&map, 0), vec![(source, 0, 0, 1), (source, 3, 3, 5)]);
    }
    let (mut term, mut parser) = terminal(3, 4, "ABC界".as_bytes());
    let first = term.grid()[Line(0)].id();
    let second = term.grid()[Line(1)].id();
    let map = term.feed_with_origin_map(&mut parser, b"\x1b[2;1Hx");
    assert_eq!(spans(&map, 0), vec![(first, 0, 0, 3)]);
    assert_eq!(spans(&map, 1), vec![(second, 2, 2, 2)]);
}

#[test]
fn a_rejected_combining_append_keeps_the_full_old_fragment_source() {
    // The candidate stores 64 scalars per fragment; the next mark is placed
    // in a new fragment after this explicit control boundary.
    let input = format!("e{}\x1b[1;2H", "\u{301}".repeat(63));
    let (mut term, mut parser) = terminal(2, 8, input.as_bytes());
    let source = term.grid()[Line(0)].id();
    let map = term.feed_with_origin_map(&mut parser, "\u{301}".as_bytes());
    assert_eq!(term.grid()[Line(0)][Column(0)].zerowidth().unwrap().len(), 63);
    assert!(term.grid()[Line(0)][Column(1)].grapheme_fragment());
    assert_eq!(spans(&map, 0), vec![(source, 0, 0, 1), (source, 2, 2, 6)]);
}

#[derive(Clone, Default)]
struct Events(Rc<RefCell<Vec<String>>>);

impl EventListener for Events {
    fn send_event(&self, event: Event) {
        self.0.borrow_mut().push(format!("{event:?}"));
    }
}

#[test]
fn mapped_feeds_match_plain_parser_cells_styles_cursors_and_events_at_every_chunk() {
    let plain_events = Events::default();
    let mapped_events = Events::default();
    let mut plain = Term::new(Config::default(), &Size { rows: 3, columns: 8 }, plain_events.clone());
    let mut mapped = Term::new(Config::default(), &Size { rows: 3, columns: 8 }, mapped_events.clone());
    let mut plain_parser: Processor = Processor::new();
    let mut mapped_parser: Processor = Processor::new();
    let chunks = [
        &b"\x1b[31;44mABC1"[..],
        &b"\x1b[6n\x07\x1b]2;origins\x07"[..],
        &b"\xef\xb8"[..],
        &b"\x8f\xe2\x83\xa3"[..],
        &b"\r\x1b[4hx\x1b[4l\x1b[2@\x1b[2P\x1b[2X"[..],
        "\x1b[?1049h\x1b[1;32m界\r\n\t".as_bytes(),
        &b"\x1b[?1049l\r\nA\r\nB\r\nC\x1b[1;2H\x1b[K"[..],
        &b"\x1b[1J\x1b#8\x1b[2J\x1b[3J\x1bc"[..],
        &b"1"[..],
        "\u{fe0f}\u{20e3}".as_bytes(),
    ];
    for bytes in chunks {
        plain_parser.advance(&mut plain, bytes);
        let map = mapped.feed_with_origin_map(&mut mapped_parser, bytes);
        assert_eq!(plain.mode(), mapped.mode());
        assert_eq!(plain.grid().cursor, mapped.grid().cursor);
        assert_eq!(plain.grid().saved_cursor, mapped.grid().saved_cursor);
        assert_eq!(plain.history_size(), mapped.history_size());
        for line in -(plain.history_size() as i32)..plain.screen_lines() as i32 {
            let left = &plain.grid()[Line(line)];
            let right = &mapped.grid()[Line(line)];
            assert_eq!(left, right, "chunk={bytes:?} line={line}");
            assert_eq!(left.hard_break(), right.hard_break());
            assert_eq!(left.wrap_columns(), right.wrap_columns());
            assert!(left.origins().is_none());
            assert!(right.origins().is_none());
        }
        assert_eq!(*plain_events.0.borrow(), *mapped_events.0.borrow());
        for grid in [&map.primary, &map.alternate] {
            for row in &grid.target_rows {
                assert!(row.origins.spans.iter().all(|span|
                    grid.source_rows.iter().any(|source| source.id == span.source_row)));
            }
        }
    }
}

#[test]
fn removed_rows_are_events_and_do_not_override_a_later_history_clear() {
    for clear in [&b"\x1b[3J"[..], &b"\x1bc"[..]] {
        let mut config = Config::default();
        config.scrolling_history = 1;
        let mut term = Term::new(config, &Size { rows: 2, columns: 4 }, VoidListener);
        let mut parser = Processor::new();
        parser.advance(&mut term, b"AAAA\r\nBBBB\r\nCCCC");
        let old_history = term.grid()[Line(-1)].id();
        let newly_scrolled_history = term.grid()[Line(0)].id();
        let old_epoch = term.history_clear_epoch();
        let mut bytes = b"\r\nD".to_vec();
        bytes.extend_from_slice(clear);
        let map = term.feed_with_origin_map(&mut parser, &bytes);
        assert!(term.history_clear_epoch() > old_epoch);
        assert_eq!(term.history_size(), 0);
        assert!(map.primary.target_rows.iter().all(|row| row.origins.spans.iter().all(|span|
            span.source_row != old_history && span.source_row != newly_scrolled_history)));
        // This earlier capacity event cannot grant permission to restore the
        // subsequently cleared source in an external history consumer.
        assert_eq!(map.primary.removed_rows.len(), 1);
        assert_eq!(map.primary.removed_rows[0].row.origins.spans[0].source_row, old_history);
    }
}

#[test]
fn a_feed_map_composes_with_the_next_resize_without_resurrecting_rewritten_cells() {
    use std::collections::BTreeMap;

    let (mut term, mut parser) = terminal(2, 8, b"ABCDEFGH\r\nX");
    let source = term.grid()[Line(0)].id();
    let feed = term.feed_with_origin_map(&mut parser, b"\x1b[1;3HCD");
    let mut bindings = BTreeMap::new();
    for row in &feed.primary.target_rows {
        for span in &row.origins.spans {
            for offset in 0..span.len {
                bindings.insert((row.id, span.column + offset),
                    (span.source_row, span.source_column + offset));
            }
        }
    }
    let resize = term.resize_with_map(Size { rows: 2, columns: 4 });
    let mut survivors = Vec::new();
    for row in &resize.primary.target_rows {
        for span in &row.origins.spans {
            for offset in 0..span.len {
                if let Some(&(old_row, old_column)) = bindings.get(&(span.source_row, span.source_column + offset)) {
                    if old_row == source {
                        survivors.push(old_column);
                    }
                }
            }
        }
    }
    survivors.sort_unstable();
    assert_eq!(survivors, [0, 1, 4, 5, 6, 7]);
    assert!((-(term.history_size() as i32)..term.screen_lines() as i32)
        .all(|line| term.grid()[Line(line)].origins().is_none()));
}
