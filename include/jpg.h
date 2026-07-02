#ifndef JPG_H
#define JPG_H

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    unsigned char *app0;
    size_t app0_len;
    unsigned char *app1;
    size_t app1_len;
    unsigned char *dqt;
    size_t dqt_len;
    unsigned char *sof;
    size_t sof_len;
    unsigned char *dht;
    size_t dht_len;
    unsigned char *sos;
    size_t sos_len;
} Jpg;

typedef struct {
    int length;
    unsigned char table_info;
    unsigned char table_data[64];
} Dqt;

typedef struct {
    int width;
    int height;
    int num_components;
    struct {
        unsigned char component_id;
        unsigned char sampling_factors;
        unsigned char q_table_id;
    } components[3];
} Sof;

typedef struct {
    int length;
    unsigned char table_info;
    int num_symbols;
    unsigned char counts_array[16];
    unsigned char *symbols_array;
} DhtTable;

typedef struct {
    int count;
    DhtTable tables[2][2];
} Dht;

typedef struct {
    unsigned char component_id;
    unsigned char huffman_table_selector;
} Sos_Components;

typedef struct {
    int length;
    int num_components;
    Sos_Components *component_selector_mapping;
    unsigned char spectral_approximation_bytes[3];
} Sos;

typedef struct {
    unsigned char *data;
    size_t size;
    size_t byte_index;
    int bit_index;
} BitReader;

typedef struct {
    int code;
    int length;
    int value;
} HuffmanCode;

typedef struct {
    HuffmanCode *codes;
    int num_codes;
    int max_length;
} HuffmanTable;

bool verify_jpg_header(unsigned char *buffer, size_t f_size, bool verbose);
unsigned char* file_buffer_jpg(FILE *f, size_t *f_len);
Jpg *extract_jpg_info(unsigned char *buffer, size_t f_len);
Dqt *decode_dqt(Jpg *jpg);
Sof *decode_sof(Jpg *jpg);
Dht *decode_dht(Jpg *jpg);
Sos *decode_sos(Jpg *jpg);
unsigned char* bitstream_loop(Jpg *jpg, Sos *sos, size_t total_jpg_size, size_t *output_size);
void init_bit_reader(BitReader *br, unsigned char *data, size_t size);
int read_bit(BitReader *br);
int read_bits(BitReader *br, int num_bits);
HuffmanTable* build_huffman_table(DhtTable *dht_table);
int decode_huffman_symbol(BitReader *br, HuffmanTable *table);
int find_huffman_value(HuffmanTable *table, int code, int length);
void zigzag_to_matrix(int zigzag[64], int matrix[8][8]);
int decode_signed_value(int value, int category);
void decode_block(BitReader *br, HuffmanTable *dc_table, HuffmanTable *ac_table, int block[64], int *dc_predictor);
void decode_mcu(BitReader *br, HuffmanTable *dc_tables[2], HuffmanTable *ac_tables[2], Sof *sof, Sos *sos, int mcu_blocks[][64], int *dc_predictors);
void dequantize_block(int block[64], unsigned char *quant_table);
void idct_8x8(int input[8][8], float output[8][8]);
void ycbcr_to_rgb(int y, int cb, int cr, int *r, int *g, int *b);
void reconstruct_image(Sof *sof, Dht *dht, Sos *sos, unsigned char *merged_data, size_t merged_size, unsigned char *output_image);
unsigned char* decode_jpeg(Jpg *jpg, Sof *sof, Dqt *dqt, Dht *dht, Sos *sos, size_t *image_size, bool verbose);
void convert_jpg_to_ascii(unsigned char *rgb_image, int width, int height,const char *output_file);
void debug_jpg(Jpg *jpg);
bool is_jpg_file(unsigned char *buffer);
int save_ppm(const char *filename, unsigned char *rgb_data, int width, int height);
#endif