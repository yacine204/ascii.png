#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdint.h>
#include <zlib.h>
#include <math.h>
#include <sys/ioctl.h>

#include "include/png.h"


uint32_t read_32_be(const unsigned char* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | 
           ((uint32_t)p[2] << 8)  | p[3];
}


int get_bytes_per_pixel(int color_type) {
    switch (color_type) {
        case 0: return 1; // grayscale
        case 2: return 3; // RGB
        case 3: return 1; // indexed (Palette)
        case 4: return 2; // grayscale + Alpha
        case 6: return 4; // RGBA
        default: return 3; // fallback
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


// extract file buffer
char* file_buffer(FILE *f){
    fseek(f,0,SEEK_END);
    size_t f_len = ftell(f);
    fseek(f,0,SEEK_SET);

    char *buffer = malloc(f_len+1);
    if(!buffer){
        return NULL;
    }

    size_t bytes_read = fread(buffer, 1, f_len, f);
    buffer[bytes_read] = '\0';
    return buffer;  
}



void convert_to_ascii(unsigned char *decompressed, Png *png) {
    int bpp = get_bytes_per_pixel(png->color_type);
    size_t row_size = 1 + (png->width * bpp);

    struct winsize w;
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
    int term_w = w.ws_col > 0 ? w.ws_col : 200;
    int term_h = w.ws_row > 0 ? w.ws_row - 1 : 50;

    printf("\n");
    for (int ty = 0; ty < term_h; ty++) {
        
        size_t y_top = (ty * 2)     * png->height / (term_h * 2);
        size_t y_bot = (ty * 2 + 1) * png->height / (term_h * 2);

        unsigned char *row_top = decompressed + (y_top * row_size) + 1;
        unsigned char *row_bot = decompressed + (y_bot * row_size) + 1;

        for (int tx = 0; tx < term_w; tx++) {
            
            size_t x = tx * png->width / term_w;
            size_t idx = x * bpp;

            uint8_t rt = row_top[idx], gt = row_top[idx+1], bt = row_top[idx+2];
            uint8_t rb = row_bot[idx], gb = row_bot[idx+1], bb = row_bot[idx+2];

            printf("\033[38;2;%d;%d;%dm\033[48;2;%d;%d;%dm\xe2\x96\x80", rt, gt, bt, rb, gb, bb);
        }
        printf("\033[0m\033[K\n");
    }
    printf("\033[0m");
}

int main(int argc, char *argv[]){

    if(argc<2){
        printf("[usage]: ./main <image path> <optional:verbose (debugging)>");
        return 1;
    }
    
    bool verbose = false;
    if(argc>=3){
        verbose = (strcmp(argv[2], "true")==0);
    }

    FILE *f = fopen(argv[1], "rb");

    if(!f){
        perror("could not open image");
        return 1;
    }

    char *buffer = file_buffer(f);
    bool is_png = verify_png_header(buffer, verbose);

    if (is_png) {  
        Png *png = extract_png_structure(buffer);
        if(verbose) debug_png_info(png);
        Idat *assembled = assemble_idat(png);
        int bpp = get_bytes_per_pixel(png->color_type);
        unsigned long expected_size = png->height * (1+(png->width * bpp));
        unsigned char *decompressed = decompress_assembled_idat(assembled,expected_size, verbose);

        if (decompressed) {
            defilter_png(decompressed, png, bpp);
            convert_to_ascii(decompressed, png);
            free(decompressed);
        }
        if (assembled) {
            if (assembled->chunk_data) free(assembled->chunk_data);
            free(assembled);
        }
        if (png->idat) free(png->idat);
        
        free(png);
    }
    free(buffer);
    fclose(f);
    return 0;
}