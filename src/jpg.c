#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <math.h>
#include <stdint.h>     
#include <sys/ioctl.h>  
#include <unistd.h>     

#include "../include/jpg.h"

// JPEG markers
unsigned char JPG_HEADER[] = {0xFF,0xD8};
unsigned char APP0_HEADER[] = {0xFF, 0xE0};
unsigned char APP1_HEADER[] = {0xFF, 0xE1};
unsigned char DQT_HEADER[] = {0xFF, 0xDB};
unsigned char SOF_HEADER[] = {0XFF, 0xC0};
unsigned char DHT_HEADER[] = {0xFF, 0xC4};
unsigned char SOS_HEADER[] = {0xFF, 0xDA};
unsigned char EOI_HEADER[] = {0xFF, 0xD9};

bool verify_jpg_header(unsigned char *buffer, size_t f_size, bool verbose){
    bool jpg_header = memcmp(buffer, JPG_HEADER, 2) == 0;
    bool eoi = memcmp(buffer+f_size-2, EOI_HEADER, 2) == 0;
    bool is_valid = jpg_header && eoi;
    if(verbose) printf("%s\n", is_valid ? "True" : "False");
    return is_valid;
}

unsigned char* file_buffer(FILE *f, size_t *f_len){
    fseek(f,0,SEEK_END);
    size_t file_length = ftell(f);
    fseek(f,0,SEEK_SET);

    unsigned char *buffer = malloc(file_length+1);
    if(!buffer) return NULL;

    size_t bytes_read = fread(buffer, 1, file_length, f);
    buffer[bytes_read] = '\0';
    *f_len = file_length;
    return buffer;  
}

char *extract_variable_between_headers(unsigned char *buffer, size_t buf_len, 
    unsigned char start_header[2], unsigned char end_header[2], size_t *out_len){
    size_t curr_pos = 0;

    while(curr_pos < buf_len - 1){
        if(memcmp(buffer+curr_pos, start_header, 2) == 0){
            break;
        }
        curr_pos++;
    }
    if(curr_pos >= buf_len - 1) return NULL;

    size_t start_pos = curr_pos;
    curr_pos += 2;
    while(curr_pos < buf_len - 1){
        if(memcmp(buffer+curr_pos, end_header, 2) == 0) break;
        curr_pos++;
    }
    if(curr_pos >= buf_len - 1) return NULL;

    size_t data_len = curr_pos - start_pos;
    if(data_len == 0) return NULL;

    char *data = malloc(data_len + 1);
    if(!data) return NULL;
    memcpy(data, buffer+start_pos, data_len);
    data[data_len] = '\0';

    *out_len = data_len; 
    return data;
}

Jpg *extract_jpg_info(unsigned char *buffer, size_t f_len){
    Jpg *jpg = malloc(sizeof(Jpg));
    memset(jpg, 0, sizeof(Jpg));
    jpg->app0 = extract_variable_between_headers(buffer, f_len, APP0_HEADER, DQT_HEADER, &jpg->app0_len);
    jpg->app1 = extract_variable_between_headers(buffer, f_len, APP1_HEADER, DQT_HEADER, &jpg->app1_len);
    jpg->dqt  = extract_variable_between_headers(buffer, f_len, DQT_HEADER,  SOF_HEADER, &jpg->dqt_len);
    jpg->sof  = extract_variable_between_headers(buffer, f_len, SOF_HEADER,  DHT_HEADER, &jpg->sof_len);
    jpg->dht  = extract_variable_between_headers(buffer, f_len, DHT_HEADER,  SOS_HEADER, &jpg->dht_len);
    jpg->sos  = extract_variable_between_headers(buffer, f_len, SOS_HEADER,  EOI_HEADER, &jpg->sos_len);

    return jpg;
}

Dqt *decode_dqt(Jpg *jpg) {
    if (!jpg->dqt || jpg->dqt_len < 5) return NULL;
    
    Dqt *dqt = malloc(sizeof(Dqt));
    if (!dqt) return NULL;
    
    unsigned char *ptr = (unsigned char *)jpg->dqt;
    
    if (ptr[0] == 0xFF && ptr[1] == 0xDB) {
        int length = (ptr[2] << 8) | ptr[3];
        ptr += 4;
        dqt->length = length;
    } else {
        dqt->length = 0;
    }
    
    dqt->table_info = ptr[0];
    memcpy(dqt->table_data, ptr + 1, 64);
    
    return dqt;
}

Sof *decode_sof(Jpg *jpg) {
    if (!jpg->sof || jpg->sof_len < 10) return NULL;
    
    Sof *sof = malloc(sizeof(Sof));
    if (!sof) return NULL;
    
    unsigned char *ptr = (unsigned char *)jpg->sof;
    
    if (ptr[0] == 0xFF && ptr[1] == 0xC0) {
        ptr += 4;
    } else {
        free(sof);
        return NULL;
    }
    
    sof->height = (ptr[1] << 8) | ptr[2];
    sof->width = (ptr[3] << 8) | ptr[4];
    sof->num_components = ptr[5];
    
    if (sof->num_components > 3 || sof->num_components < 1) {
        free(sof);
        return NULL;
    }
    
    for (int i = 0; i < sof->num_components; i++) {
        int offset = 6 + (i * 3);
        sof->components[i].component_id = ptr[offset];
        sof->components[i].sampling_factors = ptr[offset + 1];
        sof->components[i].q_table_id = ptr[offset + 2];
    }
    
    return sof;
}

Dht *decode_dht(Jpg *jpg) {
    if (!jpg->dht) return NULL;
    
    Dht *dht = malloc(sizeof(Dht));
    if (!dht) return NULL;
    memset(dht, 0, sizeof(Dht));
    
    unsigned char *ptr = (unsigned char *)jpg->dht;
    size_t remaining = jpg->dht_len;
    
    while (remaining > 0 && dht->count < 4) {
        // Skip marker if present
        if (remaining >= 2 && ptr[0] == 0xFF && ptr[1] == 0xC4) {
            ptr += 2;
            remaining -= 2;
            continue;
        }
        
        // Need at least 17 bytes for header + 16 counts
        if (remaining < 17) break;
        
        // Get length
        int seg_len = (ptr[0] << 8) | ptr[1];
        if (seg_len < 17 || seg_len > remaining) break;
        
        unsigned char info = ptr[2];
        int table_class = (info >> 4) & 0x0F;
        int table_id = info & 0x0F;
        
        if (table_class > 1 || table_id > 1) {
            ptr += 2 + seg_len;
            remaining -= 2 + seg_len;
            continue;
        }
        
        DhtTable *t = &dht->tables[table_class][table_id];
        t->length = seg_len;
        t->table_info = info;
        t->num_symbols = 0;
        
        for (int i = 0; i < 16; i++) {
            t->counts_array[i] = ptr[3 + i];
            t->num_symbols += t->counts_array[i];
        }
        
        if (t->num_symbols > 0 && remaining >= 17 + t->num_symbols) {
            t->symbols_array = malloc(t->num_symbols);
            if (!t->symbols_array) break;
            memcpy(t->symbols_array, ptr + 19, t->num_symbols);
        } else {
            t->symbols_array = NULL;
        }
        
        dht->count++;
        ptr += 2 + seg_len;
        remaining -= 2 + seg_len;
    }
    
    return dht;
}

Sos *decode_sos(Jpg *jpg) {
    if (!jpg->sos || jpg->sos_len < 10) return NULL;
    
    Sos *sos = malloc(sizeof(Sos));
    if (!sos) return NULL;
    
    unsigned char *ptr = (unsigned char *)jpg->sos;
    
    if (ptr[0] == 0xFF && ptr[1] == 0xDA) {
        ptr += 4;
    }
    
    sos->num_components = ptr[0];
    
    if (sos->num_components <= 0 || sos->num_components > 4) {
        free(sos);
        return NULL;
    }
    
    sos->component_selector_mapping = malloc(sos->num_components * sizeof(Sos_Components));
    if (!sos->component_selector_mapping) {
        free(sos);
        return NULL;
    }
    
    for (int i = 0; i < sos->num_components; i++) {
        int offset = 1 + (i * 2);
        sos->component_selector_mapping[i].component_id = ptr[offset];
        sos->component_selector_mapping[i].huffman_table_selector = ptr[offset + 1];
    }
    
    int final_offset = 1 + (sos->num_components * 2);
    if (final_offset + 2 < jpg->sos_len) {
        sos->spectral_approximation_bytes[0] = ptr[final_offset];
        sos->spectral_approximation_bytes[1] = ptr[final_offset + 1];
        sos->spectral_approximation_bytes[2] = ptr[final_offset + 2];
    } else {
        sos->spectral_approximation_bytes[0] = 0;
        sos->spectral_approximation_bytes[1] = 0;
        sos->spectral_approximation_bytes[2] = 0;
    }
    
    sos->length = 0;
    return sos;
}

unsigned char* bitstream_loop(Jpg *jpg, Sos *sos, size_t total_jpg_size, size_t *output_size) {
    if (!jpg->sos || !sos) return NULL;
    
    unsigned char *ptr = (unsigned char *)jpg->sos;
    size_t sos_len = jpg->sos_len;
    
    if (ptr[0] == 0xFF && ptr[1] == 0xDA) {
        ptr += 2;
        int length = (ptr[0] << 8) | ptr[1];
        ptr += 2;
        int num_components = ptr[0];
        ptr += 1 + (num_components * 2) + 3;
    } else {
        return NULL;
    }
    
    size_t remaining = sos_len - (ptr - (unsigned char *)jpg->sos);
    if (remaining == 0) return NULL;
    
    unsigned char *merged = malloc(remaining);
    if (!merged) return NULL;
    
    int read_idx = 0;
    int write_idx = 0;
    
    while (read_idx + 1 < remaining) {
        if (ptr[read_idx] == 0xFF && ptr[read_idx + 1] == 0xD9) {
            break;
        }
        if (ptr[read_idx] == 0xFF && ptr[read_idx + 1] == 0x00) {
            merged[write_idx] = 0xFF;
            write_idx++;
            read_idx += 2;
        } else {
            merged[write_idx] = ptr[read_idx];
            write_idx++;
            read_idx++;
        }
    }
    
    if (write_idx == 0) {
        free(merged);
        return NULL;
    }
    
    unsigned char *cleaned = realloc(merged, write_idx);
    if (!cleaned) {
        free(merged);
        return NULL;
    }
    
    *output_size = write_idx;
    return cleaned;
}

// BitReader functions
void init_bit_reader(BitReader *br, unsigned char *data, size_t size) {
    br->data = data;
    br->size = size;
    br->byte_index = 0;
    br->bit_index = 0;
}

int read_bit(BitReader *br){
    if (br->byte_index >= br->size) return -1;
    int bit = (br->data[br->byte_index] >> (7-br->bit_index)) & 1;
    br->bit_index++;
    if(br->bit_index == 8){
        br->bit_index = 0;
        br->byte_index++;
    }
    return bit;
}

int read_bits(BitReader *br, int num_bits){
    int result = 0;
    for(int i=0; i<num_bits; i++){
        int bit = read_bit(br);
        if (bit == -1) return -1;
        result = (result<<1) | bit;
    }
    return result;
}

// Huffman functions
int find_huffman_value(HuffmanTable *table, int code, int length) {
    for (int i = 0; i < table->num_codes; i++) {
        if (table->codes[i].length == length && table->codes[i].code == code) {
            return table->codes[i].value;
        }
    }
    return -1;
}

HuffmanTable* build_huffman_table(DhtTable *dht_table) {
    if (!dht_table || dht_table->num_symbols == 0) return NULL;
    
    HuffmanTable *table = malloc(sizeof(HuffmanTable));
    if (!table) return NULL;
    
    int num_codes = dht_table->num_symbols;
    table->codes = malloc(num_codes * sizeof(HuffmanCode));
    if (!table->codes) {
        free(table);
        return NULL;
    }
    table->num_codes = num_codes;
    table->max_length = 0;
    
    int code_index = 0;
    int code = 0;
    
    for (int length = 1; length <= 16; length++) {
        int count = dht_table->counts_array[length - 1];
        
        for (int i = 0; i < count; i++) {
            table->codes[code_index].code = code;
            table->codes[code_index].length = length;
            table->codes[code_index].value = dht_table->symbols_array[code_index];
            code_index++;
            code++;
            
            if (code > (1 << length) - 1) {
                code = 0;
            }
        }
        
        code <<= 1;
        if (code > 0) {
            table->max_length = length;
        }
    }
    
    return table;
}

int decode_huffman_symbol(BitReader *br, HuffmanTable *table) {
    if (!table) return -1;
    
    int code = 0;
    int length = 0;
    
    while (length < table->max_length && length < 16) {
        int bit = read_bit(br);
        if (bit == -1) return -1;
        
        code = (code << 1) | bit;
        length++;
        
        int value = find_huffman_value(table, code, length);
        if (value != -1) {
            return value;
        }
    }
    
    return -1; 
}

void zigzag_to_matrix(int zigzag[64], int matrix[8][8]) {
    int zigzag_order[64] = {
        0, 1, 5, 6, 14, 15, 27, 28,
        2, 4, 7, 13, 16, 26, 29, 42,
        3, 8, 12, 17, 25, 30, 41, 43,
        9, 11, 18, 24, 31, 40, 44, 53,
        10, 19, 23, 32, 39, 45, 52, 54,
        20, 22, 33, 38, 46, 51, 55, 60,
        21, 34, 37, 47, 50, 56, 59, 61,
        35, 36, 48, 49, 57, 58, 62, 63
    };
    
    for (int i = 0; i < 64; i++) {
        int row = zigzag_order[i] / 8;
        int col = zigzag_order[i] % 8;
        matrix[row][col] = zigzag[i];
    }
}

int decode_signed_value(int value, int category) {
    if (category == 0) return 0;
    
    int sign = value >> (category - 1);
    if (sign == 0) {
        int mask = (1 << category) - 1;
        return value - mask;
    } else {
        return value;
    }
}

void decode_block(BitReader *br, HuffmanTable *dc_table, HuffmanTable *ac_table,
                  int block[64], int *dc_predictor) {
    memset(block, 0, 64 * sizeof(int));
    
    int dc_category = decode_huffman_symbol(br, dc_table);
    if (dc_category == -1) return;
    
    int dc_value = 0;
    if (dc_category > 0) {
        dc_value = read_bits(br, dc_category);
        if (dc_value == -1) return;
        dc_value = decode_signed_value(dc_value, dc_category);
    }
    block[0] = *dc_predictor + dc_value;
    *dc_predictor = block[0];
    
    int pos = 1;
    while (pos < 64) {
        int ac_symbol = decode_huffman_symbol(br, ac_table);
        if (ac_symbol == -1) return;
        
        int run_length = ac_symbol >> 4;
        int category = ac_symbol & 0x0F;
        
        if (run_length == 0 && category == 0) break;
        
        if (run_length == 15 && category == 0) {
            pos += 16;
            continue;
        }
        
        pos += run_length;
        if (pos >= 64) break;
        
        int ac_value = 0;
        if (category > 0) {
            ac_value = read_bits(br, category);
            if (ac_value == -1) return;
            ac_value = decode_signed_value(ac_value, category);
        }
        block[pos] = ac_value;
        pos++;
    }
}

void decode_mcu(BitReader *br, HuffmanTable *dc_tables[2], HuffmanTable *ac_tables[2],
                Sof *sof, Sos *sos, int mcu_blocks[][64], int *dc_predictors) {
    for (int comp = 0; comp < sof->num_components; comp++) {
        int dc_table_id = (sos->component_selector_mapping[comp].huffman_table_selector >> 4) & 0x0F;
        int ac_table_id = sos->component_selector_mapping[comp].huffman_table_selector & 0x0F;
        
        int h_sampling = 1;
        int v_sampling = 1;
        for (int i = 0; i < sof->num_components; i++) {
            if (sof->components[i].component_id == sos->component_selector_mapping[comp].component_id) {
                h_sampling = (sof->components[i].sampling_factors >> 4) & 0x0F;
                v_sampling = sof->components[i].sampling_factors & 0x0F;
                break;
            }
        }
        
        int num_blocks = h_sampling * v_sampling;
        for (int b = 0; b < num_blocks; b++) {
            int block_idx = comp * num_blocks + b;
            decode_block(br, dc_tables[dc_table_id], ac_tables[ac_table_id],
                        mcu_blocks[block_idx], &dc_predictors[comp]);
        }
    }
}

void dequantize_block(int block[64], unsigned char *quant_table) {
    for (int i = 0; i < 64; i++) {
        block[i] *= quant_table[i];
    }
}

void idct_8x8(int input[8][8], float output[8][8]) {
    float temp[8][8];
    
    for (int i = 0; i < 8; i++) {
        for (int j = 0; j < 8; j++) {
            float sum = 0;
            for (int k = 0; k < 8; k++) {
                float coeff = (k == 0) ? 1.0f / sqrt(2.0f) : 1.0f;
                sum += coeff * input[i][k] * cos((2 * j + 1) * k * 3.14159 / 16.0);
            }
            temp[i][j] = sum / 2.0f;
        }
    }
    
    for (int i = 0; i < 8; i++) {
        for (int j = 0; j < 8; j++) {
            float sum = 0;
            for (int k = 0; k < 8; k++) {
                float coeff = (k == 0) ? 1.0f / sqrt(2.0f) : 1.0f;
                sum += coeff * temp[k][j] * cos((2 * i + 1) * k * 3.14159 / 16.0);
            }
            output[i][j] = sum / 2.0f;
        }
    }
}

// Only keep ONE ycbcr_to_rgb function - use this improved version
void ycbcr_to_rgb(int y, int cb, int cr, int *r, int *g, int *b) {
    // Convert to float for better precision
    float y_f = y;
    float cb_f = cb - 128.0f;
    float cr_f = cr - 128.0f;
    
    // Standard JPEG YCbCr to RGB conversion
    float r_val = y_f + 1.402f * cr_f;
    float g_val = y_f - 0.344136f * cb_f - 0.714136f * cr_f;
    float b_val = y_f + 1.772f * cb_f;
    
    // Round to nearest integer
    *r = (int)(r_val + 0.5f);
    *g = (int)(g_val + 0.5f);
    *b = (int)(b_val + 0.5f);
    
    // Clamp to 0-255
    if (*r < 0) *r = 0;
    if (*r > 255) *r = 255;
    if (*g < 0) *g = 0;
    if (*g > 255) *g = 255;
    if (*b < 0) *b = 0;
    if (*b > 255) *b = 255;
}

void reconstruct_image(Sof *sof, Dht *dht, Sos *sos, unsigned char *merged_data, 
                       size_t merged_size, unsigned char *output_image) {
    BitReader br;
    init_bit_reader(&br, merged_data, merged_size);
    
    HuffmanTable *dc_tables[2] = {NULL, NULL};
    HuffmanTable *ac_tables[2] = {NULL, NULL};
    
    for (int class = 0; class < 2; class++) {
        for (int id = 0; id < 2; id++) {
            if (dht->tables[class][id].num_symbols > 0) {
                HuffmanTable *table = build_huffman_table(&dht->tables[class][id]);
                if (class == 0) dc_tables[id] = table;
                else ac_tables[id] = table;
            }
        }
    }
    
    // Get sampling factors
    int h_max = 1, v_max = 1;
    int h_samp[4] = {1, 1, 1, 1};
    int v_samp[4] = {1, 1, 1, 1};
    int q_table_id[4] = {0, 0, 0, 0};
    int comp_id[4] = {0, 0, 0, 0};
    
    // Map components: Y=1, Cb=2, Cr=3
    for (int i = 0; i < sof->num_components; i++) {
        comp_id[i] = sof->components[i].component_id;
        h_samp[i] = (sof->components[i].sampling_factors >> 4) & 0x0F;
        v_samp[i] = sof->components[i].sampling_factors & 0x0F;
        q_table_id[i] = sof->components[i].q_table_id;
        if (h_samp[i] > h_max) h_max = h_samp[i];
        if (v_samp[i] > v_max) v_max = v_samp[i];
    }
    
    int mcu_width = h_max * 8;
    int mcu_height = v_max * 8;
    
    int mcus_per_row = (sof->width + mcu_width - 1) / mcu_width;
    int mcus_per_col = (sof->height + mcu_height - 1) / mcu_height;
    
    // Default quantization tables
    unsigned char default_luma_qt[64] = {
        16, 11, 10, 16, 24, 40, 51, 61,
        12, 12, 14, 19, 26, 58, 60, 55,
        14, 13, 16, 24, 40, 57, 69, 56,
        14, 17, 22, 29, 51, 87, 80, 62,
        18, 22, 37, 56, 68, 109, 103, 77,
        24, 35, 55, 64, 81, 104, 113, 92,
        49, 64, 78, 87, 103, 121, 120, 101,
        72, 92, 95, 98, 112, 100, 103, 99
    };
    unsigned char default_chroma_qt[64] = {
        17, 18, 24, 47, 99, 99, 99, 99,
        18, 21, 26, 66, 99, 99, 99, 99,
        24, 26, 56, 99, 99, 99, 99, 99,
        47, 66, 99, 99, 99, 99, 99, 99,
        99, 99, 99, 99, 99, 99, 99, 99,
        99, 99, 99, 99, 99, 99, 99, 99,
        99, 99, 99, 99, 99, 99, 99, 99,
        99, 99, 99, 99, 99, 99, 99, 99
    };
    
    unsigned char *q_tables[4] = {default_luma_qt, default_chroma_qt, default_luma_qt, default_chroma_qt};
    
    int dc_predictors[4] = {0, 0, 0, 0};
    
    // For each MCU row
    for (int mcu_row = 0; mcu_row < mcus_per_col; mcu_row++) {
        for (int mcu_col = 0; mcu_col < mcus_per_row; mcu_col++) {
            int mcu_blocks[10][64] = {{0}};
            
            decode_mcu(&br, dc_tables, ac_tables, sof, sos, mcu_blocks, dc_predictors);
            
            // Extract Y, Cb, Cr blocks
            float y_blocks[4][8][8];
            float cb_blocks[4][8][8];
            float cr_blocks[4][8][8];
            int y_count = 0, cb_count = 0, cr_count = 0;
            
            int block_idx = 0;
            for (int comp = 0; comp < sof->num_components; comp++) {
                int id = comp_id[comp];
                int h = h_samp[comp];
                int v = v_samp[comp];
                int num_blocks = h * v;
                unsigned char *q_table = q_tables[q_table_id[comp]];
                
                for (int b = 0; b < num_blocks; b++) {
                    int block[64];
                    memcpy(block, mcu_blocks[block_idx], 64 * sizeof(int));
                    block_idx++;
                    
                    dequantize_block(block, q_table);
                    
                    int matrix[8][8];
                    zigzag_to_matrix(block, matrix);
                    float float_matrix[8][8];
                    idct_8x8(matrix, float_matrix);
                    
                    // Store based on component ID
                    if (id == 1) { // Y
                        memcpy(y_blocks[y_count], float_matrix, 8 * 8 * sizeof(float));
                        y_count++;
                    } else if (id == 2) { // Cb
                        memcpy(cb_blocks[cb_count], float_matrix, 8 * 8 * sizeof(float));
                        cb_count++;
                    } else if (id == 3) { // Cr
                        memcpy(cr_blocks[cr_count], float_matrix, 8 * 8 * sizeof(float));
                        cr_count++;
                    }
                }
            }
            
            // Now convert YCbCr to RGB for each pixel
            // For 4:2:0 subsampling, each chroma block covers 2x2 Y blocks
            int y_blocks_per_mcu = y_count;
            int cb_blocks_per_mcu = cb_count;
            int cr_blocks_per_mcu = cr_count;
            
            // Calculate chroma subsampling ratios
            int h_ratio = h_samp[0] / (h_samp[1] > 0 ? h_samp[1] : 1);
            int v_ratio = v_samp[0] / (v_samp[1] > 0 ? v_samp[1] : 1);
            
            for (int yb = 0; yb < y_blocks_per_mcu; yb++) {
                int y_block_x = (mcu_col * h_samp[0] + (yb % h_samp[0])) * 8;
                int y_block_y = (mcu_row * v_samp[0] + (yb / h_samp[0])) * 8;
                
                // Find corresponding chroma block (for 4:2:0, 1 chroma block per 2x2 Y blocks)
                int cb_idx = (cb_blocks_per_mcu > 0) ? yb / (h_ratio * v_ratio) : 0;
                int cr_idx = (cr_blocks_per_mcu > 0) ? yb / (h_ratio * v_ratio) : 0;
                
                // Clamp indices
                if (cb_idx >= cb_blocks_per_mcu) cb_idx = cb_blocks_per_mcu - 1;
                if (cr_idx >= cr_blocks_per_mcu) cr_idx = cr_blocks_per_mcu - 1;
                if (cb_idx < 0) cb_idx = 0;
                if (cr_idx < 0) cr_idx = 0;
                
                for (int y = 0; y < 8; y++) {
                    for (int x = 0; x < 8; x++) {
                        int px = y_block_x + x;
                        int py = y_block_y + y;
                        
                        if (px < sof->width && py < sof->height) {
                            // Get Y value (0-255)
                            float y_val = y_blocks[yb][y][x] + 128.0f;
                            if (y_val < 0) y_val = 0;
                            if (y_val > 255) y_val = 255;
                            
                            // Get Cb and Cr values with upsampling
                            float cb_val = 128.0f;
                            float cr_val = 128.0f;
                            
                            if (cb_blocks_per_mcu > 0) {
                                int cx = x / (8 / h_ratio);
                                int cy = y / (8 / v_ratio);
                                if (cx >= 8) cx = 7;
                                if (cy >= 8) cy = 7;
                                cb_val = cb_blocks[cb_idx][cy][cx] + 128.0f;
                                if (cb_val < 0) cb_val = 0;
                                if (cb_val > 255) cb_val = 255;
                            }
                            if (cr_blocks_per_mcu > 0) {
                                int cx = x / (8 / h_ratio);
                                int cy = y / (8 / v_ratio);
                                if (cx >= 8) cx = 7;
                                if (cy >= 8) cy = 7;
                                cr_val = cr_blocks[cr_idx][cy][cx] + 128.0f;
                                if (cr_val < 0) cr_val = 0;
                                if (cr_val > 255) cr_val = 255;
                            }
                            
                            // Convert YCbCr to RGB
                            int r, g, b;
                            ycbcr_to_rgb((int)y_val, (int)cb_val, (int)cr_val, &r, &g, &b);
                            
                            int idx = (py * sof->width + px) * 3;
                            output_image[idx] = r;
                            output_image[idx+1] = g;
                            output_image[idx+2] = b;
                        }
                    }
                }
            }
        }
    }
    
    // Free Huffman tables
    for (int i = 0; i < 2; i++) {
        if (dc_tables[i]) { if (dc_tables[i]->codes) free(dc_tables[i]->codes); free(dc_tables[i]); }
        if (ac_tables[i]) { if (ac_tables[i]->codes) free(ac_tables[i]->codes); free(ac_tables[i]); }
    }
}

unsigned char* decode_jpeg(Jpg *jpg, Sof *sof, Dqt *dqt, Dht *dht, Sos *sos, 
                           size_t *image_size, bool verbose) {
    size_t bitstream_size;
    unsigned char *merged = bitstream_loop(jpg, sos, jpg->sos_len + 10, &bitstream_size);
    if (!merged) return NULL;
    
    if(verbose) printf("Bitstream size: %zu bytes\n", bitstream_size);
    
    *image_size = sof->width * sof->height * 3;
    unsigned char *output = malloc(*image_size);
    if (!output) {
        free(merged);
        return NULL;
    }
    memset(output, 0, *image_size);
    
    reconstruct_image(sof, dht, sos, merged, bitstream_size, output);
    
    free(merged);
    return output;
}

void convert_jpg_to_ascii(unsigned char *rgb_image, int width, int height, const char *output_path) {
    if (!rgb_image) {
        printf("Error: rgb_image is NULL!\n");
        return;
    }
    
    struct winsize w;
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
    int term_w = w.ws_col > 0 ? w.ws_col : 200;
    int term_h = w.ws_row > 0 ? w.ws_row - 1 : 50;

    if (output_path) {
        printf("Saving PPM to: %s\n", output_path);
        printf("Dimensions: %dx%d\n", term_w, term_h);
        
        unsigned char *pixelated = malloc((size_t)term_w * term_h * 3);
        if (!pixelated) {
            perror("Could not allocate pixelated buffer");
            return;
        }
        
        for (int ty = 0; ty < term_h; ty++) {
            size_t y_top = (ty * 2) * height / (term_h * 2);
            size_t y_bot = (ty * 2 + 1) * height / (term_h * 2);

            if (y_top >= height) y_top = height - 1;
            if (y_bot >= height) y_bot = height - 1;
            
            unsigned char *row_top = rgb_image + (y_top * width * 3);
            unsigned char *row_bot = rgb_image + (y_bot * width * 3);
            
            for (int tx = 0; tx < term_w; tx++) {
                size_t x = tx * width / term_w;
                if (x >= width) x = width - 1;
                size_t idx = x * 3;
                size_t dst_idx = ((size_t)ty * term_w + tx) * 3;
                
                // Average top and bottom rows
                pixelated[dst_idx] = (row_top[idx] + row_bot[idx]) / 2;
                pixelated[dst_idx + 1] = (row_top[idx+1] + row_bot[idx+1]) / 2;
                pixelated[dst_idx + 2] = (row_top[idx+2] + row_bot[idx+2]) / 2;
            }
        }

        int result = save_ppm(output_path, pixelated, term_w, term_h);
        if (result == 0) {
            printf("Successfully saved PPM to: %s\n", output_path);
        } else {
            printf("Failed to save PPM to: %s\n", output_path);
        }
        
        free(pixelated);
    }

    printf("\n");
    for (int ty = 0; ty < term_h; ty++) {
        size_t y_top = (ty * 2) * height / (term_h * 2);
        size_t y_bot = (ty * 2 + 1) * height / (term_h * 2);
        
        if (y_top >= height) y_top = height - 1;
        if (y_bot >= height) y_bot = height - 1;
        
        unsigned char *row_top = rgb_image + (y_top * width * 3);
        unsigned char *row_bot = rgb_image + (y_bot * width * 3);
        
        for (int tx = 0; tx < term_w; tx++) {
            size_t x = tx * width / term_w;
            if (x >= width) x = width - 1;
            size_t idx = x * 3;
            
            printf("\033[38;2;%d;%d;%dm\033[48;2;%d;%d;%dm\xe2\x96\x80", 
                   row_top[idx], row_top[idx+1], row_top[idx+2],
                   row_bot[idx], row_bot[idx+1], row_bot[idx+2]);
        }
        printf("\033[0m\033[K\n");
    }
    printf("\033[0m");
}

// convert_jpg_to_ascii version for wasm export 

char* convert_jpg_to_ascii_wasm(unsigned char *rgb_image, int width, int height, int term_w, int term_h) {

    if (!rgb_image) {
        const char *msg = "ERROR: rgb_image is NULL!\n";
        char *error = malloc(strlen(msg) + 1);
        if (error) snprintf(error, strlen(msg) + 1, "%s", msg);
        return error;
    }

    size_t buffer_size = (size_t)term_w * term_h * 50 + (size_t)term_h * 16 + 32;
    char *buffer = malloc(buffer_size);
    if (!buffer) {
        const char *msg = "ERROR: buffer couldnt be allocated!\n";
        char *error = malloc(strlen(msg) + 1);
        if (error) snprintf(error, strlen(msg) + 1, "%s", msg);
        return error;
    }

    size_t remaining = buffer_size;
    char *write_ptr = buffer;
    int written;

    for (int ty = 0; ty < term_h; ty++) {
        size_t y_top = (ty * 2) * height / (term_h * 2);
        size_t y_bot = (ty * 2 + 1) * height / (term_h * 2);
        if (y_top >= (size_t)height) y_top = height - 1;
        if (y_bot >= (size_t)height) y_bot = height - 1;

        unsigned char *row_top = rgb_image + (y_top * width * 3);
        unsigned char *row_bot = rgb_image + (y_bot * width * 3);

        for (int tx = 0; tx < term_w; tx++) {
            size_t x = tx * width / term_w;
            if (x >= (size_t)width) x = width - 1;
            size_t idx = x * 3;

            written = snprintf(write_ptr, remaining,
                "\033[38;2;%d;%d;%dm\033[48;2;%d;%d;%dm\xe2\x96\x80",
                row_top[idx], row_top[idx+1], row_top[idx+2],
                row_bot[idx], row_bot[idx+1], row_bot[idx+2]);

            if (written < 0 || (size_t)written >= remaining) break;
            write_ptr += written;
            remaining -= written;
        }

        written = snprintf(write_ptr, remaining, "\033[0m\r\n");
        if (written < 0 || (size_t)written >= remaining) break;
        write_ptr += written;
        remaining -= written;
    }

    snprintf(write_ptr, remaining, "\033[0m");
    return buffer;
}

bool is_jpg_file(unsigned char *buffer) {
    unsigned char jpg_header[] = {0xFF, 0xD8};
    return memcmp(buffer, jpg_header, 2) == 0;
}

unsigned char* file_buffer_jpg(FILE *f, size_t *f_len) {
    fseek(f, 0, SEEK_END);
    size_t file_length = ftell(f);
    fseek(f, 0, SEEK_SET);

    unsigned char *buffer = malloc(file_length + 1);
    if (!buffer) return NULL;

    size_t bytes_read = fread(buffer, 1, file_length, f);
    buffer[bytes_read] = '\0';
    *f_len = file_length;
    return buffer;
}