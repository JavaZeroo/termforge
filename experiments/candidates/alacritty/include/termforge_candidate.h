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

void* tf_terminal_new(size_t rows, size_t columns, size_t history);
void tf_terminal_free(void* terminal);
int tf_terminal_feed(void* terminal, const uint8_t* data, size_t len, uint8_t* error, size_t error_capacity);
int tf_terminal_finish(void* terminal, uint8_t* error, size_t error_capacity);
int tf_terminal_resize(void* terminal, size_t rows, size_t columns, uint8_t* error, size_t error_capacity);
int tf_terminal_meta(void* terminal, TfTerminalMeta* output);
int tf_terminal_row_meta(void* terminal, size_t index, TfRowMeta* output);
int tf_terminal_cell_meta(void* terminal, size_t index, size_t column, TfCellMeta* output);
int tf_terminal_cell_text(void* terminal, size_t index, size_t column, uint8_t* output,
                          size_t capacity, size_t* needed);
int tf_terminal_next_event(void* terminal, uint32_t* kind, uint8_t* output,
                           size_t capacity, size_t* needed);

#ifdef __cplusplus
}
#endif
