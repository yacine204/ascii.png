#ifndef PNG_H
#define PNG_H

#include <stdlib.h>
#include <stdbool.h>

typedef struct {
    int length;
    int chunk_type;
    unsigned char* chunk_data;
    int crc;
}Idat;

typedef struct {
    size_t width;
    size_t height;
    int bit_depth;
    int color_type;
    int compression_method;
    int filter_method;
    int interlace_method;
    Idat* idat;
    int total_idat;
}Png;

bool verify_png_header(char* buffer, bool verbose);
Png *extract_png_structure(char *buffer);
Idat *assemble_idat(Png *png);
unsigned char* decompress_assembled_idat(Idat *assembled_idat, unsigned long expected_uncompressed_size, bool verbose);
void debug_png_info(Png *png);
void defilter_png(unsigned char *decompressed, Png *png, int bpp);

#endif