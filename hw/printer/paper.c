#include "hw/printer/paper.h"

static void paper_dealloc(PaperState* paper)
{
    if(paper->buffer != NULL)
    {
        free(paper->buffer);
        paper->buffer = NULL;
    }
}

void paper_init(PaperState* paper, uint64_t width, uint64_t height)
{
    if(!paper->modified)
    {
        return;
    }

    paper->modified = false;

    paper->width = width;
    paper->height = height;
    paper->current_x = 0;
    paper->current_y = 0;

    paper_dealloc(paper);

    uint64_t stride = paper->width * 3;
    stride += (4-(stride)%4)%4;

    paper->buffer_size = stride * paper->height;

    paper->buffer = malloc(paper->buffer_size);
    memset(paper->buffer, 255, paper->buffer_size);
}

void paper_advance(PaperState* paper, int64_t x, int64_t y)
{
    paper->modified = true;

    paper->current_x += x;
    paper->current_y += y;
}

void paper_reset_horizontal(PaperState* paper)
{
    paper->modified = true;

    paper->current_x = 0;
}

void paper_set_pixel(PaperState* paper, int64_t relative_x, int64_t relative_y, uint8_t r, uint8_t g, uint8_t b)
{
    paper->modified = true;

    int64_t x = (paper->current_x + relative_x);
    int64_t y = (paper->height - (paper->current_y + relative_y)) - 1;

    if(x >= paper->width || x < 0)
    {
        return;
    }

    if(y >= paper->height || y < 0)
    {
        return;
    }

    uint64_t position = (paper->width * y) + x;
    position *= 3;

    // FIXME: Probably not correct, but it does allow overlapping pixels for now, so...
    paper->buffer[position] &= r;
    paper->buffer[position + 1] &= g;
    paper->buffer[position + 2] &= b;
}

void paper_set_block(PaperState* paper, int64_t relative_x, int64_t relative_y, int64_t width, int64_t height, uint8_t r, uint8_t g, uint8_t b)
{
    paper->modified = true;

    for(int y = 0; y < height; y++)
    {
        for(int x = 0; x < width; x++)
        {
            paper_set_pixel(paper,relative_x + x, relative_y + y, r, g, b);
        }
    }
}

#define HEADER_SIZE 54

void paper_finish(PaperState* paper, const char* filename)
{
    paper->modified = true;

    FILE *fp = fopen(filename, "wb");

    int filesize = HEADER_SIZE + paper->buffer_size;
    unsigned char header[14] = {
        'B','M', // Signature
        0,0,0,0, // File size
        0,0, // Reserved
        0,0, // Reserved
        HEADER_SIZE,0,0,0 // Start of pixel data
    };

    // File size
    header[2] = (filesize);
    header[3] = (filesize>>8);
    header[4] = (filesize>>16);
    header[5] = (filesize>>24);

    unsigned char info[40] = {
        40,0,0,0, // Header size
        0,0,0,0, // Image width
        0,0,0,0, // Image height
        1,0, // Number of planes
        24,0, // Bits per pixel
        0,0,0,0, /// Compression
        0,0,0,0, /// Image size
        0,0,0,0, /// Horizontal resolution
        0,0,0,0, /// Vertical resolution
        0,0,0,0, /// Color table count
        0,0,0,0, /// Important color count
    };

    // Image width
    info[ 4] = (paper->width);
    info[ 5] = (paper->width>>8);
    info[ 6] = (paper->width>>16);
    info[ 7] = (paper->width>>24);
    // Image height
    info[ 8] = (paper->height);
    info[ 9] = (paper->height>>8);
    info[10] = (paper->height>>16);
    info[11] = (paper->height>>24);

    fwrite(header,1,14,fp);
    fwrite(info,1,40,fp);

    fwrite(paper->buffer, 1, paper->buffer_size, fp);

    fclose(fp);

    paper_dealloc(paper);
}