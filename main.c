#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#include "include/png.h"
#include "include/jpg.h"

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("usage: %s <image_file> [verbose]\n", argv[0]);
        printf("Supports PNG and JPG files\n");
        return 1;
    }
    
    bool verbose = false;
    if (argc >= 3) {
        verbose = (strcmp(argv[2], "true") == 0);
    }

    FILE *f = fopen(argv[1], "rb");
    if (!f) {
        perror("could not open image");
        return 1;
    }

    // Read header to determine file type
    unsigned char header[12];
    size_t header_read = fread(header, 1, 12, f);
    fseek(f, 0, SEEK_SET);

    if (header_read < 8) {
        printf("File too small\n");
        fclose(f);
        return 1;
    }

    // Check file type
    if (is_png_file(header)) {
        // PNG processing
        char *buffer = file_buffer_png(f);
        fclose(f);
        
        if (!buffer) {
            printf("Failed to read PNG file\n");
            return 1;
        }
        
        if (!verify_png_header(buffer, verbose)) {
            printf("Not a valid PNG file\n");
            free(buffer);
            return 1;
        }
        
        Png *png = extract_png_structure(buffer);
        if (verbose) debug_png_info(png);
        
        Idat *assembled = assemble_idat(png);
        int bpp = get_bytes_per_pixel(png->color_type);
        unsigned long expected_size = png->height * (1 + (png->width * bpp));
        unsigned char *decompressed = decompress_assembled_idat(assembled, expected_size, verbose);

        if (decompressed) {
            defilter_png(decompressed, png, bpp);
            convert_png_to_ascii(decompressed, png);
            free(decompressed);
        }
        
        if (assembled) {
            if (assembled->chunk_data) free(assembled->chunk_data);
            free(assembled);
        }
        if (png->idat) free(png->idat);
        free(png);
        free(buffer);
        
    } else if (is_jpg_file(header)) {
        // JPG processing
        size_t file_size;
        unsigned char *buffer = file_buffer_jpg(f, &file_size);
        fclose(f);
        
        if (!buffer) {
            printf("Failed to read JPG file\n");
            return 1;
        }

        if (!verify_jpg_header(buffer, file_size, verbose)) {
            printf("Not a valid JPEG file\n");
            free(buffer);
            return 1;
        }

        Jpg *jpg = extract_jpg_info(buffer, file_size);
        if (!jpg) {
            printf("Failed to extract JPEG info\n");
            free(buffer);
            return 1;
        }

        if (!jpg->sof || !jpg->dht || !jpg->sos) {
            printf("Missing JPEG sections\n");
            free(buffer);
            free(jpg);
            return 1;
        }

        Sof *sof = decode_sof(jpg);
        if (!sof) {
            printf("Failed to decode SOF\n");
            free(buffer);
            free(jpg);
            return 1;
        }
        
        if (verbose) {
            printf("Image: %dx%d, %d components\n", sof->width, sof->height, sof->num_components);
        }

        Dqt *dqt = decode_dqt(jpg);
        Dht *dht = decode_dht(jpg);
        if (!dht) {
            printf("Failed to decode DHT\n");
            free(buffer);
            free(jpg);
            free(sof);
            if (dqt) free(dqt);
            return 1;
        }

        Sos *sos = decode_sos(jpg);
        if (!sos) {
            printf("Failed to decode SOS\n");
            free(buffer);
            free(jpg);
            free(sof);
            if (dqt) free(dqt);
            free(dht);
            return 1;
        }

        size_t image_size;
        unsigned char *rgb_image = decode_jpeg(jpg, sof, dqt, dht, sos, &image_size);

        if (rgb_image) {
            convert_jpg_to_ascii(rgb_image, sof->width, sof->height);
            free(rgb_image);
        } else {
            printf("Failed to decode image\n");
        }

        free(buffer);
        free(jpg);
        free(sof);
        if (dqt) free(dqt);
        free(dht);
        free(sos);
        
    } else {
        printf("Unsupported file format. Only PNG and JPG are supported.\n");
        fclose(f);
        return 1;
    }

    return 0;
}