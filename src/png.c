#include <stdio.h>
#include "stdbool.h"
#include "inttypes.h"
#include "zlib.h"
#include "../include/png.h"


unsigned char PNG_HEADER[] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
#define PNG_HEADER_SIZE 8
#define PNG_IHDR_SIZE 13

bool verify_png_header(char* buffer, bool verbose){
    bool is_png = memcmp(buffer, PNG_HEADER, PNG_HEADER_SIZE) == 0;
    if(verbose) printf("is png\n");
    return is_png;
}

Png *extract_png_structure(char *buffer){
    Png *png_info = malloc(sizeof(Png));

    if(!png_info){
        perror("couldnt allocate memory for png_info");
        return NULL;
    }

    unsigned char *ihdr = (unsigned char*)buffer + 16;
    //enfore big edian
    png_info->width  = read_32_be(ihdr);
    png_info->height = read_32_be(ihdr + 4);
    png_info->bit_depth          = ihdr[8];
    png_info->color_type         = ihdr[9];
    png_info->compression_method = ihdr[10];
    png_info->filter_method      = ihdr[11];
    png_info->interlace_method   = ihdr[12];

    unsigned char *current_chunk = (unsigned char*)buffer + 33;
    unsigned char *tmp = current_chunk;

    int idat_count = 0;
    size_t total_size = 0;

    // count idat chunks
    while (1) {
        uint32_t chunk_len = read_32_be(tmp);
        uint32_t chunk_type = read_32_be(tmp + 4);

        if(chunk_type==0x49454E44){
            break;
        }

        if(chunk_type==0x49444154){
            idat_count++;
            total_size += chunk_len;
        }
        tmp += 12 + chunk_len;
    }
    png_info->total_idat = idat_count;
    if(idat_count>0){
        png_info->idat = malloc(sizeof(Idat) * idat_count);
        if(!png_info->idat){
            perror("couldnt allocate memory for idat");
        }

        tmp = current_chunk;
        int index = 0;
        size_t offset = 0;

        while(1){
            uint32_t chunk_len = read_32_be(tmp);
            uint32_t chunk_type = read_32_be(tmp+4);

            if (chunk_type == 0x49454E44){
                break;
            }

            if(chunk_type == 0x49444154){
                png_info->idat[index].length = chunk_len;
                png_info->idat[index].chunk_type = chunk_type;
                png_info->idat[index].chunk_data = tmp+8;
                png_info->idat[index].crc = read_32_be(tmp+8+chunk_len);
                offset+=chunk_len;
                index++;
            }
            tmp+= 12+chunk_len;

        }
    }
    return png_info;
}

// squish all idat in png to a one idat to decompress after
Idat *assemble_idat(Png *png){
    Idat *assembled = malloc(sizeof(Idat));

    assembled->length = 0;
    assembled->chunk_data = NULL;

    if (!assembled){
        perror("couldnt allocate memory for assembled idat");
    }
    uint32_t total_size = 0;
    for(int i=0; i<png->total_idat; i++){
        total_size += png->idat[i].length;
    }

    assembled->chunk_data = malloc(total_size);
    if (!assembled->chunk_data && total_size == 0){
        perror("could not allocate memory for chunk data");
        free(assembled);
        return NULL;
    }

    uint32_t current_offset = 0;
    for (int i=0; i<png->total_idat; i++){
        memcpy(assembled->chunk_data+current_offset, png->idat[i].chunk_data, png->idat[i].length);
        current_offset+=png->idat[i].length;
    }

    assembled->length = total_size;
    return assembled;
}

unsigned char* decompress_assembled_idat(Idat *assembled_idat, unsigned long expected_uncompressed_size, bool verbose){

    if (!assembled_idat || !assembled_idat->chunk_data || assembled_idat->length == 0) {
        if(verbose) printf("No valid IDAT data to decompress\n");
        return NULL;
    }

    if(verbose) {
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
        if(verbose) printf("Decompression successful! Bytes written: %lu\n", decompressed_len);
        return decompressed_data;
    } else if (status == Z_BUF_ERROR) {
        if(verbose) printf("Buffer to small. Needed: %lu, Available: %lu\n", decompressed_data, expected_uncompressed_size);
        free(decompressed_data);
        return NULL;
    }else{
        if(verbose) printf("Decompression failed with error code: %d\n", status);
        free(decompressed_data);
        return NULL;
    }

}

void debug_png_info(Png *png){
    printf("width: %zu\n",png->width);
    printf("height: %zu\n", png->height);
    printf("bit depth: %d\n", png->bit_depth);
    printf("color type: %d\n", png->color_type);
    printf("compression type: %d\n", png->compression_method);
    printf("filter method: %d\n", png->filter_method);
    printf("interlace method: %d\n", png->interlace_method);

    for(int i=0; i<png->total_idat; i++){
        printf("idat[%d]: chunk_type: 0x%X, chunk_data: %p, crc: %d\n", i, png->idat[i].chunk_type, (void*)png->idat[i].chunk_data, png->idat[i].crc);
    }
}

void defilter_png(unsigned char *decompressed, Png *png, int bpp){
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

