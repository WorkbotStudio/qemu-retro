#ifndef PAPER_H
#define PAPER_H

#include "qemu/osdep.h"

struct PaperState
{
    int64_t current_x;
    int64_t current_y;
    uint64_t width;
    uint64_t height;
    uint64_t buffer_size;
    uint8_t* buffer;
};

typedef struct PaperState PaperState;

void paper_init(PaperState* paper, uint64_t width, uint64_t height);

void paper_advance(PaperState* paper, int64_t x, int64_t y);

void paper_reset_horizontal(PaperState* paper);

void paper_set_pixel(PaperState* paper, int64_t relative_x, int64_t relative_y, uint8_t r, uint8_t g, uint8_t b);

void paper_set_block(PaperState* paper, int64_t relative_x, int64_t relative_y, int64_t width, int64_t height, uint8_t r, uint8_t g, uint8_t b);

void paper_finish(PaperState* paper, const char* filename);

#endif