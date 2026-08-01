#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdint.h>
#include <zlib.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "../include/png.h"

// PNG constants
unsigned char PNG_HEADER[] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
#define PNG_HEADER_SIZE 8

// PNG helper functions (move from main.c)
uint32_t read_32_be(const unsigned char* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | 
           ((uint32_t)p[2] << 8)  | p[3];
}

int get_bytes_per_pixel(int color_type) {
    switch (color_type) {
        case 0: return 1;
        case 2: return 3;
        case 3: return 1;
        case 4: return 2;
        case 6: return 4;
        default: return 3;
    }
}

uint8_t paeth_predictor(uint8_t a, uint8_t b, uint8_t c) {
    int p = (int)a + (int)b - (int)c;
    int pa = abs(p - (int)a);
    int pb = abs(p - (int)b);
    int pc = abs(p - (int)c);
    if (pa <= pb && pa <= pc) return a;
    if (pb <= pc) return b;
    return c;
}

char* file_buffer_png(FILE *f) {
    fseek(f, 0, SEEK_END);
    size_t f_len = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *buffer = malloc(f_len + 1);
    if (!buffer) return NULL;

    size_t bytes_read = fread(buffer, 1, f_len, f);
    buffer[bytes_read] = '\0';
    return buffer;
}

bool verify_png_header(char* buffer, bool verbose) {
    bool is_png = memcmp(buffer, PNG_HEADER, PNG_HEADER_SIZE) == 0;
    if (verbose) printf("%s\n", is_png ? "is png" : "isnt png");
    return is_png;
}

Png *extract_png_structure(char *buffer) {
    Png *png_info = malloc(sizeof(Png));
    if (!png_info) {
        perror("couldnt allocate memory for png_info");
        return NULL;
    }

    unsigned char *ihdr = (unsigned char*)buffer + 16;
    png_info->width = read_32_be(ihdr);
    png_info->height = read_32_be(ihdr + 4);
    png_info->bit_depth = ihdr[8];
    png_info->color_type = ihdr[9];
    png_info->compression_method = ihdr[10];
    png_info->filter_method = ihdr[11];
    png_info->interlace_method = ihdr[12];

    unsigned char *current_chunk = (unsigned char*)buffer + 33;
    unsigned char *tmp = current_chunk;

    int idat_count = 0;
    size_t total_size = 0;

    while (1) {
        uint32_t chunk_len = read_32_be(tmp);
        uint32_t chunk_type = read_32_be(tmp + 4);

        if (chunk_type == 0x49454E44) break;

        if (chunk_type == 0x49444154) {
            idat_count++;
            total_size += chunk_len;
        }
        tmp += 12 + chunk_len;
    }
    
    png_info->total_idat = idat_count;
    if (idat_count > 0) {
        png_info->idat = malloc(sizeof(Idat) * idat_count);
        if (!png_info->idat) {
            perror("couldnt allocate memory for idat");
        }

        tmp = current_chunk;
        int index = 0;

        while (1) {
            uint32_t chunk_len = read_32_be(tmp);
            uint32_t chunk_type = read_32_be(tmp + 4);

            if (chunk_type == 0x49454E44) break;

            if (chunk_type == 0x49444154) {
                png_info->idat[index].length = chunk_len;
                png_info->idat[index].chunk_type = chunk_type;
                png_info->idat[index].chunk_data = tmp + 8;
                png_info->idat[index].crc = read_32_be(tmp + 8 + chunk_len);
                index++;
            }
            tmp += 12 + chunk_len;
        }
    }
    return png_info;
}

Idat *assemble_idat(Png *png) {
    Idat *assembled = malloc(sizeof(Idat));
    assembled->length = 0;
    assembled->chunk_data = NULL;

    if (!assembled) {
        perror("couldnt allocate memory for assembled idat");
        return NULL;
    }
    
    uint32_t total_size = 0;
    for (int i = 0; i < png->total_idat; i++) {
        total_size += png->idat[i].length;
    }

    assembled->chunk_data = malloc(total_size);
    if (!assembled->chunk_data && total_size == 0) {
        perror("could not allocate memory for chunk data");
        free(assembled);
        return NULL;
    }

    uint32_t current_offset = 0;
    for (int i = 0; i < png->total_idat; i++) {
        memcpy(assembled->chunk_data + current_offset, 
               png->idat[i].chunk_data, png->idat[i].length);
        current_offset += png->idat[i].length;
    }

    assembled->length = total_size;
    return assembled;
}

unsigned char* decompress_assembled_idat(Idat *assembled_idat, 
                                         unsigned long expected_uncompressed_size, 
                                         bool verbose) {
    if (!assembled_idat || !assembled_idat->chunk_data || assembled_idat->length == 0) {
        if (verbose) printf("No valid IDAT data to decompress\n");
        return NULL;
    }

    if (verbose) {
        printf("Compressed size: %u bytes\n", assembled_idat->length);
        printf("Expected decompressed size: %lu bytes\n", expected_uncompressed_size);
    }

    unsigned char *decompressed_data = malloc(expected_uncompressed_size);
    if (!decompressed_data) {
        perror("Could not allocate memory for decompressed data");
        return NULL;
    }

    unsigned long decompressed_len = expected_uncompressed_size;

    int status = uncompress(decompressed_data, &decompressed_len, 
                           assembled_idat->chunk_data, assembled_idat->length);
    
    if (status == Z_OK) {
        if (verbose) printf("Decompression successful! Bytes written: %lu\n", decompressed_len);
        return decompressed_data;
    } else if (status == Z_BUF_ERROR) {
        if (verbose) printf("Buffer too small\n");
        free(decompressed_data);
        return NULL;
    } else {
        if (verbose) printf("Decompression failed with error code: %d\n", status);
        free(decompressed_data);
        return NULL;
    }
}

void debug_png_info(Png *png) {
    printf("width: %zu\n", png->width);
    printf("height: %zu\n", png->height);
    printf("bit depth: %d\n", png->bit_depth);
    printf("color type: %d\n", png->color_type);
    printf("compression type: %d\n", png->compression_method);
    printf("filter method: %d\n", png->filter_method);
    printf("interlace method: %d\n", png->interlace_method);

    for (int i = 0; i < png->total_idat; i++) {
        printf("idat[%d]: chunk_type: 0x%X, chunk_data: %p, crc: %d\n", 
               i, png->idat[i].chunk_type, (void*)png->idat[i].chunk_data, png->idat[i].crc);
    }
}

void defilter_png(unsigned char *decompressed, Png *png, int bpp) {
    size_t stride = png->width * bpp;
    size_t row_size = 1 + stride;

    for (size_t r = 0; r < png->height; r++) {
        unsigned char *current_row_raw = decompressed + (r * row_size);
        uint8_t filter_type = current_row_raw[0];
        unsigned char *recon_row = current_row_raw + 1;
        
        unsigned char *prior_recon_row = (r == 0) ? NULL : (decompressed + ((r - 1) * row_size) + 1);

        for (size_t i = 0; i < stride; i++) {
            uint8_t raw = recon_row[i];
            uint8_t a = (i >= (size_t)bpp) ? recon_row[i - bpp] : 0;
            uint8_t b = (prior_recon_row) ? prior_recon_row[i] : 0;
            uint8_t c = (prior_recon_row && i >= (size_t)bpp) ? prior_recon_row[i - bpp] : 0;

            switch (filter_type) {
                case 0: recon_row[i] = raw; break;
                case 1: recon_row[i] = raw + a; break;
                case 2: recon_row[i] = raw + b; break;
                case 3: recon_row[i] = raw + ((int)a + (int)b) / 2; break;
                case 4: recon_row[i] = raw + paeth_predictor(a, b, c); break;
            }
        }
    }
}

void convert_png_to_ascii(unsigned char *decompressed, Png *png, const char *output_path) {
    int bpp = get_bytes_per_pixel(png->color_type);
    size_t row_size = 1 + (png->width * bpp);
    

    struct winsize w;
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
    int term_w = w.ws_col > 0 ? w.ws_col : 200;
    int term_h = w.ws_row > 0 ? w.ws_row - 1 : 50;
    

    if (output_path) {
        int save_width = term_w*2;
        int save_height = term_h*2;
        unsigned char *pixelated = malloc(save_width * save_height * 3);
        if (!pixelated) {
            perror("Could not allocate pixelated buffer");
        }
        
        for (int ty = 0; ty < save_height; ty++) {
            size_t y_top = (ty * 2) * png->height / (save_height * 2);
            size_t y_bot = (ty * 2 + 1) * png->height / (save_height * 2);
            
            unsigned char *row_top = decompressed + (y_top * row_size) + 1;
            unsigned char *row_bot = decompressed + (y_bot * row_size) + 1;
            
            for (int tx = 0; tx < save_width; tx++) {
                size_t x = tx * png->width / save_width;
                size_t idx = x * bpp;
                size_t dst_idx = (ty * save_width + tx) * 3;
                
                uint8_t rt, gt, bt, rb, gb, bb;
                if (bpp >= 3) {
                    rt = row_top[idx]; gt = row_top[idx+1]; bt = row_top[idx+2];
                    rb = row_bot[idx]; gb = row_bot[idx+1]; bb = row_bot[idx+2];
                } else {
                    rt = gt = bt = row_top[idx];
                    rb = gb = bb = row_bot[idx];
                }
                
                pixelated[dst_idx] = (rt + rb) / 2;
                pixelated[dst_idx + 1] = (gt + gb) / 2;
                pixelated[dst_idx + 2] = (bt + bb) / 2;
            }
        }
        
        save_ppm(output_path, pixelated, save_width, save_height);
        free(pixelated);
        printf("Saved PIXELATED version to: %s\n", output_path);
    }
    
    printf("\n");
    for (int ty = 0; ty < term_h; ty++) {
        size_t y_top = (ty * 2) * png->height / (term_h * 2);
        size_t y_bot = (ty * 2 + 1) * png->height / (term_h * 2);

        unsigned char *row_top = decompressed + (y_top * row_size) + 1;
        unsigned char *row_bot = decompressed + (y_bot * row_size) + 1;

        for (int tx = 0; tx < term_w; tx++) {
            size_t x = tx * png->width / term_w;
            size_t idx = x * bpp;

            uint8_t rt, gt, bt, rb, gb, bb;
            if (bpp >= 3) {
                rt = row_top[idx]; gt = row_top[idx+1]; bt = row_top[idx+2];
                rb = row_bot[idx]; gb = row_bot[idx+1]; bb = row_bot[idx+2];
            } else {
                rt = gt = bt = row_top[idx];
                rb = gb = bb = row_bot[idx];
            }

            printf("\033[38;2;%d;%d;%dm\033[48;2;%d;%d;%dm\xe2\x96\x80", 
                   rt, gt, bt, rb, gb, bb);
        }
        printf("\033[0m\033[K\n");
    }
    printf("\033[0m");
}



char* convert_png_to_ascii_wasm(unsigned char *decompressed, Png *png, int term_w, int term_h) {
    int bpp = get_bytes_per_pixel(png->color_type);
    size_t row_size = 1 + (png->width * bpp);

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
        size_t y_top = (ty * 2) * png->height / (term_h * 2);
        size_t y_bot = (ty * 2 + 1) * png->height / (term_h * 2);

        unsigned char *row_top = decompressed + (y_top * row_size) + 1;
        unsigned char *row_bot = decompressed + (y_bot * row_size) + 1;

        for (int tx = 0; tx < term_w; tx++) {
            size_t x = tx * png->width / term_w;
            size_t idx = x * bpp;

            uint8_t rt, gt, bt, rb, gb, bb;
            if (bpp >= 3) {
                rt = row_top[idx]; gt = row_top[idx+1]; bt = row_top[idx+2];
                rb = row_bot[idx]; gb = row_bot[idx+1]; bb = row_bot[idx+2];
            } else {
                rt = gt = bt = row_top[idx];
                rb = gb = bb = row_bot[idx];
            }

            written = snprintf(write_ptr, remaining,
                "\033[38;2;%d;%d;%dm\033[48;2;%d;%d;%dm\xe2\x96\x80",
                rt, gt, bt, rb, gb, bb);   // <-- fixed: use extracted vars, not row_top/row_bot directly

            if (written < 0 || (size_t)written >= remaining) break;   // <-- fixed: written, not write
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

bool is_png_file(unsigned char *buffer) {
    return memcmp(buffer, PNG_HEADER, PNG_HEADER_SIZE) == 0;
}