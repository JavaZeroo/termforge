//! Grid resize and reflow.

use std::cmp::{Ordering, max, min};
use std::mem;

use crate::index::{Boundary, Column, Line};
use crate::term::cell::{Flags, ResetDiscriminant};

use crate::grid::row::Row;
use crate::grid::origins::RowOrigins;
use crate::grid::{Dimensions, Grid, GridCell, ResizeMappedRow, ResizeRemoval, ResizeRemovalReason};

fn origin_extent(origins: &mut Option<Box<RowOrigins>>, columns: usize) {
    if let Some(origins) = origins {
        if columns < origins.columns {
            origins.truncate(columns);
        } else if columns > origins.columns {
            origins.insert_gap(origins.columns, columns - origins.columns);
        }
    }
}

impl<T: GridCell + Default + PartialEq> Grid<T> {
    /// Resize the grid's width and/or height.
    pub fn resize<D>(&mut self, reflow: bool, lines: usize, columns: usize)
    where
        T: ResetDiscriminant<D>,
        D: PartialEq,
    {
        // Use empty template cell for resetting cells due to resize.
        let template = mem::take(&mut self.cursor.template);

        match self.lines.cmp(&lines) {
            Ordering::Less => self.grow_lines(lines),
            Ordering::Greater => self.shrink_lines(lines),
            Ordering::Equal => (),
        }

        match self.columns.cmp(&columns) {
            Ordering::Less => self.grow_columns(reflow, columns),
            Ordering::Greater => self.shrink_columns(reflow, columns),
            Ordering::Equal => (),
        }

        // Restore template cell.
        self.cursor.template = template;
    }

    /// Add lines to the visible area.
    ///
    /// Alacritty keeps the cursor at the bottom of the terminal as long as there
    /// is scrollback available. Once scrollback is exhausted, new lines are
    /// simply added to the bottom of the screen.
    fn grow_lines<D>(&mut self, target: usize)
    where
        T: ResetDiscriminant<D>,
        D: PartialEq,
    {
        let lines_added = target - self.lines;

        // Need to resize before updating buffer.
        self.raw.grow_visible_lines(target);
        self.lines = target;

        let history_size = self.history_size();
        let from_history = min(history_size, lines_added);

        // Move existing lines up for every line that couldn't be pulled from history.
        if from_history != lines_added {
            let delta = lines_added - from_history;
            // This rotation removes newly allocated rows, not retained history.
            self.scroll_up_inner(&(Line(0)..Line(target as i32)), delta, false);
        }

        // Move cursor down for every line pulled from history.
        self.saved_cursor.point.line += from_history;
        self.cursor.point.line += from_history;

        self.display_offset = self.display_offset.saturating_sub(lines_added);
        self.decrease_scroll_limit(lines_added);
    }

    /// Remove lines from the visible area.
    ///
    /// The behavior in Terminal.app and iTerm.app is to keep the cursor at the
    /// bottom of the screen. This is achieved by pushing history "out the top"
    /// of the terminal window.
    ///
    /// Alacritty takes the same approach.
    fn shrink_lines<D>(&mut self, target: usize)
    where
        T: ResetDiscriminant<D>,
        D: PartialEq,
    {
        // Scroll up to keep content inside the window.
        let required_scrolling = (self.cursor.point.line.0 as usize + 1).saturating_sub(target);
        if required_scrolling > 0 {
            self.scroll_up(&(Line(0)..Line(self.lines as i32)), required_scrolling);

            // Clamp cursors to the new viewport size.
            self.cursor.point.line = min(self.cursor.point.line, Line(target as i32 - 1));
        }

        // Clamp saved cursor, since only primary cursor is scrolled into viewport.
        self.saved_cursor.point.line = min(self.saved_cursor.point.line, Line(target as i32 - 1));

        if let Some(records) = &mut self.resize_removed {
            for line in target..self.lines {
                let row = &mut self.raw[Line(line as i32)];
                if row.origins().is_some() {
                    records.push(ResizeRemoval {
                        row: Self::take_resize_row(row, line as i32),
                        reason: ResizeRemovalReason::HeightCrop,
                    });
                }
            }
        }
        self.raw.rotate((self.lines - target) as isize);
        self.raw.shrink_visible_lines(target);
        self.lines = target;
    }

    /// Grow number of columns in each row, reflowing if necessary.
    fn grow_columns(&mut self, reflow: bool, columns: usize) {
        // Check if a row needs to be wrapped.
        let should_reflow = |row: &Row<T>| -> bool {
            let len = Column(row.len());
            reflow && len.0 > 0 && len < columns && row[len - 1].flags().contains(Flags::WRAPLINE)
        };

        self.columns = columns;

        let mut reversed: Vec<Row<T>> = Vec::with_capacity(self.raw.len());
        let mut cursor_line_delta = 0;

        // Remove the linewrap special case, by moving the cursor outside of the grid.
        if self.cursor.input_needs_wrap && reflow {
            self.cursor.input_needs_wrap = false;
            self.cursor.point.column += 1;
        }

        let mut rows = self.raw.take_all();

        for (i, mut row) in rows.drain(..).enumerate().rev() {
            // Check if reflowing should be performed.
            let last_row = match reversed.last_mut() {
                Some(last_row) if should_reflow(last_row) => last_row,
                _ => {
                    reversed.push(row);
                    continue;
                },
            };

            // Remove wrap flag before appending additional cells.
            if let Some(cell) = last_row.last_mut() {
                cell.flags_mut().remove(Flags::WRAPLINE);
            }

            // Remove leading spacers when reflowing wide char to the previous line.
            let mut last_origins = last_row.take_origins();
            let mut last_len = last_row.len();
            if last_len >= 1
                && last_row[Column(last_len - 1)].flags().contains(Flags::LEADING_WIDE_CHAR_SPACER)
            {
                last_row.shrink(last_len - 1);
                last_len -= 1;
                origin_extent(&mut last_origins, last_len);
            }

            // Don't try to pull more cells from the next line than available.
            let mut num_wrapped = columns - last_len;
            let len = min(row.len(), num_wrapped);
            let mut origins = row.take_origins();
            let hard_origin = origins.as_ref().and_then(|origins| origins.hard_boundary);
            let mut moved_columns = len;

            // Insert leading spacer when there's not enough room for reflowing wide char.
            let mut cells = if row[Column(len - 1)].flags().contains(Flags::WIDE_CHAR) {
                num_wrapped -= 1;
                moved_columns -= 1;

                let mut cells = row.front_split_off(len - 1);

                let mut spacer = T::default();
                spacer.flags_mut().insert(Flags::LEADING_WIDE_CHAR_SPACER);
                cells.push(spacer);

                cells
            } else {
                row.front_split_off(len)
            };

            let mut moved_origins = origins.as_mut().map(|origins| {
                Box::new(origins.take_prefix(moved_columns))
            });
            origin_extent(&mut moved_origins, cells.len());
            // The continuation's hard boundary moves only if that row is
            // actually consumed, including the cursor-sensitive empty case.
            if let Some(origins) = &mut origins {
                origins.hard_boundary = hard_origin;
            }
            if let Some(mut moved) = moved_origins {
                moved.hard_boundary = None;
                let previous = last_origins.get_or_insert_with(|| Box::new(RowOrigins {
                    columns: last_len, ..RowOrigins::default()
                }));
                previous.append(*moved, last_len);
            } else if let Some(previous) = &mut last_origins {
                previous.insert_gap(last_len, cells.len());
                previous.hard_boundary = None;
            }
            row.set_origins(origins);

            // Add removed cells to previous row and reflow content.
            last_row.append(&mut cells);

            let cursor_buffer_line = self.lines - self.cursor.point.line.0 as usize - 1;

            if i == cursor_buffer_line && reflow {
                // Resize cursor's line and reflow the cursor if necessary.
                let mut target = self.cursor.point.sub(self, Boundary::Cursor, num_wrapped);

                // Clamp to the last column, if no content was reflown with the cursor.
                if target.column.0 == 0 && row.is_clear() {
                    self.cursor.input_needs_wrap = true;
                    target = target.sub(self, Boundary::Cursor, 1);
                }
                self.cursor.point.column = target.column;

                // Get required cursor line changes. Since `num_wrapped` is smaller than `columns`
                // this will always be either `0` or `1`.
                let line_delta = self.cursor.point.line - target.line;

                if line_delta != 0 && row.is_clear() {
                    // The consumed continuation can carry a hard boundary even
                    // when its remaining cells are empty.
                    last_row.set_hard_break(row.hard_break());
                    if let Some(origins) = &mut last_origins {
                        origins.hard_boundary = hard_origin;
                    }
                    last_row.set_origins(last_origins);
                    continue;
                }

                cursor_line_delta += line_delta.0 as usize;
            } else if row.is_clear() {
                if i < self.display_offset {
                    // Since we removed a line, rotate down the viewport.
                    self.display_offset = self.display_offset.saturating_sub(1);
                }

                // Rotate cursor down if content below them was pulled from history.
                if i < cursor_buffer_line {
                    self.cursor.point.line += 1;
                }

                // A physical row may disappear; its logical end must survive
                // on the row which received its cells.
                last_row.set_hard_break(row.hard_break());
                if let Some(origins) = &mut last_origins {
                    origins.hard_boundary = hard_origin;
                }
                last_row.set_origins(last_origins);

                // Don't push line into the new buffer.
                continue;
            }

            if let Some(cell) = last_row.last_mut() {
                // Set wrap flag if next line still has cells.
                cell.flags_mut().insert(Flags::WRAPLINE);
            }
            last_row.set_origins(last_origins);

            reversed.push(row);
        }

        // Make sure we have at least the viewport filled.
        if reversed.len() < self.lines {
            let delta = (self.lines - reversed.len()) as i32;
            self.cursor.point.line = max(self.cursor.point.line - delta, Line(0));
            reversed.resize_with(self.lines, || Row::new(columns));
        }

        // Pull content down to put cursor in correct position, or move cursor up if there's no
        // more lines to delete below the cursor.
        if cursor_line_delta != 0 {
            let cursor_buffer_line = self.lines - self.cursor.point.line.0 as usize - 1;
            let available = min(cursor_buffer_line, reversed.len() - self.lines);
            let overflow = cursor_line_delta.saturating_sub(available);
            let retained = reversed.len() + overflow - cursor_line_delta;
            let history = reversed.len() - self.lines;
            if let Some(records) = &mut self.resize_removed {
                for index in retained..reversed.len() {
                    records.push(ResizeRemoval {
                        row: Self::take_resize_row(&mut reversed[index], index as i32 - history as i32),
                        reason: ResizeRemovalReason::CursorReposition,
                    });
                }
            }
            reversed.truncate(retained);
            self.cursor.point.line = max(self.cursor.point.line - overflow, Line(0));
        }

        // Reverse iterator and fill all rows that are still too short.
        let mut new_raw = Vec::with_capacity(reversed.len());
        for mut row in reversed.drain(..).rev() {
            if row.len() < columns {
                row.grow(columns);
                let mut origins = row.take_origins();
                origin_extent(&mut origins, columns);
                row.set_origins(origins);
            }
            if reflow {
                let wrapped = row.last().is_some_and(|cell| cell.flags().contains(Flags::WRAPLINE));
                row.set_wrap_columns(if wrapped { columns } else { 0 });
                if wrapped {
                    row.set_hard_break(false);
                }
            }
            new_raw.push(row);
        }

        self.raw.replace_inner(new_raw);

        // Clamp display offset in case lines above it got merged.
        self.display_offset = min(self.display_offset, self.history_size());
    }

    /// Shrink number of columns in each row, reflowing if necessary.
    fn shrink_columns(&mut self, reflow: bool, columns: usize) {
        self.columns = columns;

        // Remove the linewrap special case, by moving the cursor outside of the grid.
        if self.cursor.input_needs_wrap && reflow {
            self.cursor.input_needs_wrap = false;
            self.cursor.point.column += 1;
        }

        let mut new_raw = Vec::with_capacity(self.raw.len());
        let mut buffered: Option<Vec<T>> = None;
        let mut buffered_origins: Option<Box<RowOrigins>> = None;

        let mut rows = self.raw.take_all();
        for (i, mut row) in rows.drain(..).enumerate().rev() {
            // The logical end belongs to the final fragment, not to the
            // physical container retained for the first fragment.
            let hard_break = row.hard_break();
            let hard_origin = row.origins().and_then(|origins| origins.hard_boundary);

            // Append lines left over from the previous row.
            if let Some(buffered) = buffered.take() {
                let width = buffered.len();
                let mut origins = row.take_origins();
                if let Some(prefix) = buffered_origins.take() {
                    let origins = origins.get_or_insert_with(|| Box::new(RowOrigins {
                        columns: row.len(), ..RowOrigins::default()
                    }));
                    origins.prepend(*prefix, width);
                } else if let Some(origins) = &mut origins {
                    origins.insert_gap(0, width);
                }
                // Add a column for every cell added before the cursor, if it goes beyond the new
                // width it is then later reflown.
                let cursor_buffer_line = self.lines - self.cursor.point.line.0 as usize - 1;
                if i == cursor_buffer_line {
                    self.cursor.point.column += buffered.len();
                }

                row.append_front(buffered);
                row.set_origins(origins);
            }

            loop {
                let mut origins = row.take_origins();
                let mut wrapped_origins = origins.as_mut().map(|origins| {
                    Box::new(origins.split_off(columns))
                });
                // Remove all cells which require reflowing.
                let removed = row.shrink(columns);
                let mut wrapped = match removed {
                    Some(wrapped) if reflow => wrapped,
                    removed => {
                        if !reflow {
                            if let Some(origins) = &mut wrapped_origins {
                                origins.hard_boundary = None;
                                origins.truncate(removed.as_ref().map_or(0, Vec::len));
                            }
                            if let (Some(removed), Some(origins), Some(records)) =
                                (removed, wrapped_origins.take(), &mut self.resize_removed)
                            {
                                if !origins.spans.is_empty() {
                                    records.push(ResizeRemoval {
                                        row: ResizeMappedRow { id: row.id(),
                                            line: self.lines as i32 - i as i32 - 1,
                                            columns: removed.len(), origins: *origins },
                                        reason: ResizeRemovalReason::ColumnCrop,
                                    });
                                }
                            }
                        }
                        // The adapter supports one-column windows. Preserve a
                        // whole glyph in a clipped footprint in the non-reflow
                        // alternate buffer as well.
                        if columns == 1 && row[Column(0)].flags().contains(Flags::WIDE_CHAR) {
                            row[Column(0)].flags_mut().remove(Flags::WIDE_CHAR);
                            row[Column(0)].flags_mut().insert(Flags::CLIPPED_GRAPHEME);
                        }
                        let cursor_buffer_line = self.lines - self.cursor.point.line.0 as usize - 1;
                        if reflow && i == cursor_buffer_line && self.cursor.point.column > columns {
                            // If there are empty cells before the cursor, we assume it is explicit
                            // whitespace and need to wrap it like normal content.
                            Vec::new()
                        } else {
                            if let Some(origins) = &mut origins {
                                origins.hard_boundary = hard_origin;
                            }
                            origin_extent(&mut origins, row.len());
                            row.set_origins(origins);
                            // Since it fits, just push the existing line without any reflow.
                            new_raw.push(row);
                            break;
                        }
                    },
                };
                origin_extent(&mut wrapped_origins, wrapped.len());

                // A two-cell glyph cannot be reflowed into a one-column
                // grid: repeatedly pushing it to the next row never converges.
                // Retain the entire cell text and remove only its owned spacer.
                if columns == 1 && row[Column(0)].flags().contains(Flags::WIDE_CHAR) {
                    row[Column(0)].flags_mut().remove(Flags::WIDE_CHAR);
                    row[Column(0)].flags_mut().insert(Flags::CLIPPED_GRAPHEME);
                    if wrapped.first().is_some_and(|cell| cell.flags().contains(Flags::WIDE_CHAR_SPACER)) {
                        wrapped.remove(0);
                        if let Some(origins) = &mut wrapped_origins {
                            origins.remove_range(0, 1);
                        }
                        let cursor_buffer_line = self.lines - self.cursor.point.line.0 as usize - 1;
                        if i == cursor_buffer_line && self.cursor.point.column.0 > 0 {
                            self.cursor.point.column -= 1;
                        }
                    }
                    if wrapped.is_empty() {
                        if let Some(origins) = &mut origins {
                            origins.hard_boundary = hard_origin;
                        }
                        row.set_origins(origins);
                        new_raw.push(row);
                        break;
                    }
                }

                // Insert spacer if a wide char would be wrapped into the last column.
                if columns > 1 && row.len() >= columns
                    && row[Column(columns - 1)].flags().contains(Flags::WIDE_CHAR)
                {
                    let mut spacer = T::default();
                    spacer.flags_mut().insert(Flags::LEADING_WIDE_CHAR_SPACER);

                    let wide_char = mem::replace(&mut row[Column(columns - 1)], spacer);
                    wrapped.insert(0, wide_char);
                    let leading = origins.as_mut().map(|origins| {
                        let leading = origins.split_off(columns - 1);
                        origins.insert_gap(columns - 1, 1);
                        leading
                    });
                    if let Some(leading) = leading {
                        let suffix = wrapped_origins.get_or_insert_with(|| Box::new(RowOrigins {
                            columns: wrapped.len() - 1, ..RowOrigins::default()
                        }));
                        suffix.prepend(leading, 1);
                    } else if let Some(origins) = &mut wrapped_origins {
                        origins.insert_gap(0, 1);
                    }
                }

                // Remove wide char spacer before shrinking.
                let len = wrapped.len();
                if len > 0 && wrapped[len - 1].flags().contains(Flags::LEADING_WIDE_CHAR_SPACER) {
                    if len == 1 {
                        row[Column(columns - 1)].flags_mut().insert(Flags::WRAPLINE);
                        row.set_origins(origins);
                        new_raw.push(row);
                        break;
                    } else {
                        // Remove the leading spacer from the end of the wrapped row.
                        wrapped[len - 2].flags_mut().insert(Flags::WRAPLINE);
                        wrapped.truncate(len - 1);
                        origin_extent(&mut wrapped_origins, wrapped.len());
                    }
                }

                row.set_origins(origins);
                new_raw.push(row);

                // Set line as wrapped if cells got removed.
                if let Some(cell) = new_raw.last_mut().and_then(|r| r.last_mut()) {
                    cell.flags_mut().insert(Flags::WRAPLINE);
                }

                if wrapped
                    .last()
                    .map(|c| c.flags().contains(Flags::WRAPLINE) && i >= 1)
                    .unwrap_or(false)
                    && wrapped.len() < columns
                {
                    // Make sure previous wrap flag doesn't linger around.
                    if let Some(cell) = wrapped.last_mut() {
                        cell.flags_mut().remove(Flags::WRAPLINE);
                    }

                    // Add removed cells to start of next row.
                    buffered = Some(wrapped);
                    buffered_origins = wrapped_origins;
                    break;
                } else {
                    // Reflow cursor if a line below it is deleted.
                    let cursor_buffer_line = self.lines - self.cursor.point.line.0 as usize - 1;
                    if (i == cursor_buffer_line && self.cursor.point.column < columns)
                        || i < cursor_buffer_line
                    {
                        self.cursor.point.line = max(self.cursor.point.line - 1, Line(0));
                    }

                    // Reflow the cursor if it is on this line beyond the width.
                    if i == cursor_buffer_line && self.cursor.point.column >= columns {
                        // Since only a single new line is created, we subtract only `columns`
                        // from the cursor instead of reflowing it completely.
                        self.cursor.point.column -= columns;
                    }

                    // Make sure new row is at least as long as new width.
                    let occ = wrapped.len();
                    if occ < columns {
                        wrapped.resize_with(columns, T::default);
                    }
                    origin_extent(&mut wrapped_origins, wrapped.len());
                    row = Row::from_vec(wrapped, occ);
                    row.set_hard_break(hard_break);
                    row.set_origins(wrapped_origins);

                    if i < self.display_offset {
                        // Since we added a new line, rotate up the viewport.
                        self.display_offset += 1;
                    }
                }
            }
        }

        // Reverse iterator and use it as the new grid storage.
        let mut reversed: Vec<Row<T>> = new_raw.drain(..).rev().collect();
        // Reflow can create more physical rows than the history capacity holds.
        // These are real oldest-history evictions, not merged row identities.
        let evicted = reversed.len().saturating_sub(self.max_scroll_limit + self.lines);
        self.discarded = self.discarded.checked_add(evicted as u64)
            .expect("history discarded exhausted");
        if let Some(records) = &mut self.resize_removed {
            let limit = self.max_scroll_limit + self.lines;
            for index in (limit..reversed.len()).rev() {
                records.push(ResizeRemoval {
                    row: Self::take_resize_row(&mut reversed[index], self.lines as i32 - index as i32 - 1),
                    reason: ResizeRemovalReason::Capacity,
                });
            }
        }
        reversed.truncate(self.max_scroll_limit + self.lines);
        if reflow {
            for row in &mut reversed {
                let wrapped = row.last().is_some_and(|cell| cell.flags().contains(Flags::WRAPLINE));
                row.set_wrap_columns(if wrapped { columns } else { 0 });
                if wrapped {
                    row.set_hard_break(false);
                }
            }
        }
        self.raw.replace_inner(reversed);

        // Clamp display offset in case some lines went off.
        self.display_offset = min(self.display_offset, self.history_size());

        // Reflow the primary cursor, or clamp it if reflow is disabled.
        if !reflow {
            self.cursor.point.column = min(self.cursor.point.column, Column(columns - 1));
        } else if self.cursor.point.column == columns
            && !self[self.cursor.point.line][Column(columns - 1)].flags().contains(Flags::WRAPLINE)
        {
            self.cursor.input_needs_wrap = true;
            self.cursor.point.column -= 1;
        } else {
            self.cursor.point = self.cursor.point.grid_clamp(self, Boundary::Cursor);
        }

        // Clamp the saved cursor to the grid.
        self.saved_cursor.point.column = min(self.saved_cursor.point.column, Column(columns - 1));
    }
}
