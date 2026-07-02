#ifndef PNG_H
#define PNG_H

#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    int length;
    int chunk_type;
    unsigned char* chunk_data;
    int crc;
} Idat;

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
} Png;


uint32_t read_32_be(const unsigned char* p);
int get_bytes_per_pixel(int color_type);
char* file_buffer_png(FILE *f);
bool verify_png_header(char* buffer, bool verbose);
Png *extract_png_structure(char *buffer);
Idat *assemble_idat(Png *png);
unsigned char* decompress_assembled_idat(Idat *assembled_idat, unsigned long expected_uncompressed_size, bool verbose);
void debug_png_info(Png *png);
void defilter_png(unsigned char *decompressed, Png *png, int bpp);
void convert_png_to_ascii(unsigned char *decompressed, Png *png, const char *output_file);
bool is_png_file(unsigned char *buffer);
bool is_jpg_file(unsigned char *buffer);
unsigned char* file_buffer_jpg(FILE *f, size_t *f_len);
int save_ppm(const char *filename, unsigned char *rgb_data, int width, int height);

#endif