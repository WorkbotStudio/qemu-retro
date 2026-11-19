#include "hw/printer/paper.h"

void paper_init(PaperState* paper, uint64_t width, uint64_t height)
{
    paper->width = width;
    paper->height = height;
    paper->current_x = 0;
    paper->current_y = 0;
    if(paper->buffer != NULL)
    {
        free(paper->buffer);
        paper->buffer = NULL;
    }
    paper->buffer = malloc(paper->width * paper->height * 3);
    memset(paper->buffer, 255, paper->width * paper->height * 3);
}

void paper_advance(PaperState* paper, int64_t x, int64_t y)
{
    paper->current_x += x;
    paper->current_y += y;
}

void paper_reset_horizontal(PaperState* paper)
{
    paper->current_x = 0;
}

void paper_set_pixel(PaperState* paper, int64_t relative_x, int64_t relative_y, uint8_t r, uint8_t g, uint8_t b)
{
    int64_t x = (paper->current_x + relative_x);
    int64_t y = (paper->current_y + relative_y);

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
    for(int x = 0; x < width; x++)
    {
        for(int y = 0; y < height; y++)
        {
            paper_set_pixel(paper,relative_x + x, relative_y + y, r, g, b);
        }
    }
}

#define HEADER_SIZE 54

void paper_finish(PaperState* paper, const char* filename)
{
    FILE *fp = fopen(filename, "wb");

    int filesize = HEADER_SIZE + 3*paper->width*paper->height;
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
    unsigned char bmppad[3] = {0,0,0};

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

    for(int i=0; i<paper->height; i++)
    {
        fwrite(paper->buffer+(paper->width*(paper->height-i-1)*3),3,paper->width,fp);
        fwrite(bmppad,1,(4-(paper->width*3)%4)%4,fp);
    }

    fclose(fp);

    free(paper->buffer);
    paper->buffer = NULL;
}