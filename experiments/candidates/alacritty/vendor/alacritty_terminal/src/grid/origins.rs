//! Sparse physical-cell origins for one synchronous resize or feed transaction.

/// A contiguous run of surviving cells from one pre-transaction row.
///
/// Both coordinates count physical cells. A clipped wide glyph retains its
/// lead-cell origin; its logical width still belongs to the source cell.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct OriginSpan {
    pub source_row: u64,
    pub source_column: usize,
    pub column: usize,
    pub len: usize,
}

/// Origins carried beside a row or temporary fragment during a transaction.
///
/// `columns` includes gaps and padding without origins. The optional hard
/// boundary is independent of cells, so an empty hard line remains observable.
/// Feed writes remove old origins even when the replacement text is equal.
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct RowOrigins {
    pub columns: usize,
    pub spans: Vec<OriginSpan>,
    pub hard_boundary: Option<u64>,
}

impl RowOrigins {
    pub(crate) fn new(source_row: u64, columns: usize, hard: bool) -> Self {
        let spans = if columns == 0 {
            Vec::new()
        } else {
            vec![OriginSpan { source_row, source_column: 0, column: 0, len: columns }]
        };
        Self { columns, spans, hard_boundary: hard.then_some(source_row) }
    }

    /// Move the suffix and its end boundary into a fragment starting at zero.
    pub(crate) fn split_off(&mut self, at: usize) -> Self {
        let at = at.min(self.columns);
        let mut suffix = Self {
            columns: self.columns - at,
            spans: Vec::new(),
            hard_boundary: self.hard_boundary.take(),
        };
        let spans = std::mem::take(&mut self.spans);
        for span in spans {
            let end = span.column + span.len;
            if span.column < at {
                self.push(OriginSpan { len: end.min(at) - span.column, ..span });
            }
            if end > at {
                let start = span.column.max(at);
                suffix.push(OriginSpan {
                    source_column: span.source_column + start - span.column,
                    column: start - at,
                    len: end - start,
                    ..span
                });
            }
        }
        self.columns = at;
        suffix
    }

    /// Move a prefix out and shift the remaining origins toward column zero.
    pub(crate) fn take_prefix(&mut self, count: usize) -> Self {
        if count >= self.columns {
            return std::mem::take(self);
        }
        let suffix = self.split_off(count);
        std::mem::replace(self, suffix)
    }

    /// Append a fragment at `offset`, including any unowned intervening gap.
    pub(crate) fn append(&mut self, other: Self, offset: usize) {
        assert!(offset >= self.columns, "origin append overlaps existing extent");
        self.columns = offset.checked_add(other.columns).expect("origin extent exhausted");
        self.hard_boundary = other.hard_boundary;
        for mut span in other.spans {
            span.column = offset.checked_add(span.column).expect("origin column exhausted");
            self.push(span);
        }
    }

    /// Insert a preceding fragment; the original right-hand end remains ours.
    pub(crate) fn prepend(&mut self, other: Self, width: usize) {
        assert!(other.columns <= width, "origin prefix exceeds inserted width");
        let columns = self.columns.checked_add(width).expect("origin extent exhausted");
        let end = self.hard_boundary;
        let previous = std::mem::take(&mut self.spans);
        for span in other.spans {
            self.push(span);
        }
        for mut span in previous {
            span.column = span.column.checked_add(width).expect("origin column exhausted");
            self.push(span);
        }
        self.columns = columns;
        self.hard_boundary = end;
    }

    /// Drop a physical suffix while retaining the separately owned end boundary.
    pub(crate) fn truncate(&mut self, columns: usize) {
        if columns >= self.columns {
            return;
        }
        self.spans.retain_mut(|span| {
            if span.column >= columns {
                return false;
            }
            span.len = span.len.min(columns - span.column);
            true
        });
        self.columns = columns;
    }

    /// Insert synthetic cells without assigning them an old source.
    pub(crate) fn insert_gap(&mut self, at: usize, len: usize) {
        if len == 0 {
            return;
        }
        let at = at.min(self.columns);
        let suffix = self.split_off(at);
        let offset = at.checked_add(len).expect("origin extent exhausted");
        self.append(suffix, offset);
    }

    /// Remove physical cells and close the gap, preserving the logical end.
    pub(crate) fn remove_range(&mut self, at: usize, len: usize) {
        let at = at.min(self.columns);
        let len = len.min(self.columns - at);
        if len == 0 {
            return;
        }
        let boundary = self.hard_boundary;
        let mut suffix = self.split_off(at);
        suffix.take_prefix(len);
        // Removing the final physical cells does not erase their hard boundary.
        self.append(suffix, at);
        self.hard_boundary = boundary;
    }

    /// Forget overwritten cells without moving their surviving neighbors.
    pub(crate) fn invalidate_range(&mut self, at: usize, len: usize) {
        let at = at.min(self.columns);
        let end = at + len.min(self.columns - at);
        if at == end {
            return;
        }

        let spans = std::mem::take(&mut self.spans);
        for span in spans {
            let span_end = span.column + span.len;
            if span_end <= at || span.column >= end {
                self.push(span);
                continue;
            }
            if span.column < at {
                self.push(OriginSpan { len: at - span.column, ..span });
            }
            if span_end > end {
                self.push(OriginSpan {
                    source_column: span.source_column + end - span.column,
                    column: end,
                    len: span_end - end,
                    ..span
                });
            }
        }
    }

    /// Exchange two physical cells, carrying gaps and origins with them.
    pub(crate) fn swap_cells(&mut self, a: usize, b: usize) {
        if a == b {
            return;
        }
        let (left, right) = if a < b { (a, b) } else { (b, a) };
        assert!(right < self.columns, "origin swap exceeds row extent");

        let mut left_cell = self.split_off(left);
        let mut between = left_cell.split_off(1);
        let mut right_cell = between.split_off(right - left - 1);
        let suffix = right_cell.split_off(1);
        self.append(right_cell, left);
        self.append(between, left + 1);
        self.append(left_cell, right);
        self.append(suffix, right + 1);
    }

    fn push(&mut self, span: OriginSpan) {
        if span.len == 0 {
            return;
        }
        if let Some(previous) = self.spans.last_mut() {
            if previous.source_row == span.source_row
                && previous.column + previous.len == span.column
                && previous.source_column + previous.len == span.source_column
            {
                previous.len += span.len;
                return;
            }
        }
        self.spans.push(span);
    }
}
