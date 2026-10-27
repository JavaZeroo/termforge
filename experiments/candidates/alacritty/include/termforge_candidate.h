#pragma once
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct TfTerminalMeta {
    size_t rows, columns, history;
    uint64_t discarded;
    int32_t row;
    size_t column;
    bool wrap_pending;
    int32_t fg, bg;
    bool bold;
    uint64_t mode_bits;
    uint64_t grapheme_overflows;
    uint64_t history_clear_epoch;
} TfTerminalMeta;

typedef struct TfRowMeta {
    uint64_t id;
    bool hard_break, wrap;
    size_t wrap_columns;
} TfRowMeta;

typedef struct TfCellMeta {
    int32_t width, fg, bg;
    bool bold;
    size_t marks;
    bool written;
    size_t text_len;
    bool clipped, grapheme_fragment, leading_spacer;
} TfCellMeta;

typedef struct TfResizeMapMeta {
    uint64_t epoch;
    size_t before_rows, before_columns, before_history;
    size_t after_rows, after_columns, after_history;
    uint64_t discarded_before, discarded_after;
    size_t source_rows, target_rows, removed_rows;
} TfResizeMapMeta;

typedef struct TfResizeSourceMeta {
    uint64_t id;
    int32_t line;
    size_t columns;
    bool hard_break, wrap;
} TfResizeSourceMeta;

typedef struct TfResizeRowMeta {
    uint64_t id, hard_boundary;
    int32_t line;
    uint32_t removal_reason;
    size_t columns, spans;
} TfResizeRowMeta;

typedef struct TfResizeSpan {
    uint64_t source_row;
    size_t source_column, column, len;
} TfResizeSpan;

void* tf_terminal_new(size_t rows, size_t columns, size_t history);
void tf_terminal_free(void* terminal);
int tf_terminal_feed(void* terminal, const uint8_t* data, size_t len, uint8_t* error, size_t error_capacity);
// Opt-in feed transaction tracking. New writes/erases have no old-cell origin,
// even when bytes are unchanged; surviving cells carry their old row/column.
// Uses the same physical-source record layout as resize, through feed queries.
// Does not seal a pending grapheme. Ordinary feed performs no origin capture.
int tf_terminal_feed_mapped(void* terminal, const uint8_t* data, size_t len, uint8_t* error, size_t error_capacity);
int tf_terminal_finish(void* terminal, uint8_t* error, size_t error_capacity);
int tf_terminal_resize(void* terminal, size_t rows, size_t columns, uint8_t* error, size_t error_capacity);
// Owned-value queries for the latest resize only. alternate selects the named
// buffer, regardless of which buffer is active. Copy before any successful
// feed (including empty), finish, discard, or next resize; later writes are not
// described by this map. Row/span queries reject a different epoch.
// Reasons: retained=0, capacity=1, cursor reposition=2, height crop=3, column crop=4.
// Removed records describe fragments, not final grid rows. Spans map physical
// cells; absent padding/spacers are not proof of lost logical text.
// Invalid queries return 1 without writing the output; valid queries return 0.
int tf_terminal_resize_map_meta(void* terminal, bool alternate, TfResizeMapMeta* output);
int tf_terminal_resize_source(void* terminal, uint64_t epoch, bool alternate,
                              size_t index, TfResizeSourceMeta* output);
int tf_terminal_resize_row(void* terminal, uint64_t epoch, bool alternate, bool removed,
                           size_t index, TfResizeRowMeta* output);
int tf_terminal_resize_span(void* terminal, uint64_t epoch, bool alternate, bool removed,
                            size_t row, size_t index, TfResizeSpan* output);
// A map is available for only its actual operation. A mapped feed expires the
// resize map, and resize expires the feed map. Both share the monotonic epoch
// and mutation/invalid-query rules above. Missing origins identify replaced or
// newly created cells; removed fragments are not a complete erase event log.
// Removal records are events within the transaction, not permission to retain
// external history: a later ED3/RIS can clear a source after its earlier removal.
// TerminalMeta.history_clear_epoch invalidates old external history, but its
// final value does not order removals around clears: do not clear then blindly
// append every removed fragment from the same feed.
int tf_terminal_feed_map_meta(void* terminal, bool alternate, TfResizeMapMeta* output);
int tf_terminal_feed_source(void* terminal, uint64_t epoch, bool alternate,
                            size_t index, TfResizeSourceMeta* output);
int tf_terminal_feed_row(void* terminal, uint64_t epoch, bool alternate, bool removed,
                         size_t index, TfResizeRowMeta* output);
int tf_terminal_feed_span(void* terminal, uint64_t epoch, bool alternate, bool removed,
                          size_t row, size_t index, TfResizeSpan* output);
int tf_terminal_meta(void* terminal, TfTerminalMeta* output);
// Evict an exact oldest primary-history prefix. Validate all IDs before mutation;
// return the cumulative native eviction count without changing history capacity.
int tf_terminal_discard_history(void* terminal, const uint64_t* ids, size_t count,
                                uint64_t* discarded, uint8_t* error, size_t error_capacity);
int tf_terminal_row_meta(void* terminal, size_t index, TfRowMeta* output);
int tf_terminal_cell_meta(void* terminal, size_t index, size_t column, TfCellMeta* output);
int tf_terminal_cell_text(void* terminal, size_t index, size_t column, uint8_t* output,
                          size_t capacity, size_t* needed);
int tf_terminal_next_event(void* terminal, uint32_t* kind, uint8_t* output,
                           size_t capacity, size_t* needed);

#ifdef __cplusplus
}
#endif
