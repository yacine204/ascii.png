#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>

#include "include/png.h"
#include "include/jpg.h"

#define OUTPUT_DIR "output_tests"

int save_ppm(const char *filename, unsigned char* rgb_data, int width, int height){
    FILE *f = fopen(filename, "wb");   
    if (!f){
        perror("couldnt open output file");
        return 1;
    }

    fprintf(f, "P6\n%d %d\n255\n", width, height);

    size_t data_size = width*height*3;
    size_t written = fwrite(rgb_data, 1, data_size, f);
    fclose(f);

    if(written!=data_size){
        printf("Warning: only wrote %zu of %zu bytes\n", written, data_size);
        return 1;
    }
    printf("Saved PPM image to: %s\n", filename);
    return 0;
}


static char *build_output_path(const char *name) {
    if (mkdir(OUTPUT_DIR, 0755) != 0 && errno != EEXIST) {
        perror("could not create output_test directory");
        return NULL;
    }

    const char *base = strrchr(name, '/');
    base = base ? base + 1 : name;

    size_t base_len = strlen(base);
    bool has_ext = base_len >= 4 && strcmp(base + base_len - 4, ".ppm") == 0;

    size_t path_len = strlen(OUTPUT_DIR) + 1 + base_len + (has_ext ? 0 : 4) + 1;
    char *path = malloc(path_len);
    if (!path) return NULL;

    snprintf(path, path_len, has_ext ? "%s/%s" : "%s/%s.ppm", OUTPUT_DIR, base);
    return path;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("usage: %s <image_file> [verbose] [output_name]\n", argv[0]);
        printf("Supports PNG and JPG files\n");
        printf("verbose: pass 'true' to enable verbose output\n");
        printf("output_name: if given, saves image as output_test/<output_name>.ppm\n");
        return 1;
    }

    bool verbose = false;
    char *output_name = NULL;

    // Accept verbose and output_name in either order, both optional
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "true") == 0 || strcmp(argv[i], "false") == 0) {
            verbose = (strcmp(argv[i], "true") == 0);
        } else {
            output_name = argv[i];
        }
    }

    char *output_path = NULL;
    if (output_name) {
        output_path = build_output_path(output_name);
        if (!output_path) {
            printf("Failed to prepare output path\n");
            return 1;
        }
    }

    FILE *f = fopen(argv[1], "rb");
    if (!f) {
        perror("could not open image");
        free(output_path);
        return 1;
    }

    unsigned char header[12];
    size_t header_read = fread(header, 1, 12, f);
    fseek(f, 0, SEEK_SET);

    if (header_read < 8) {
        printf("File too small\n");
        fclose(f);
        free(output_path);
        return 1;
    }

    if (is_png_file(header)) {
        char *buffer = file_buffer_png(f);
        fclose(f);

        if (!buffer) { printf("Failed to read PNG file\n"); free(output_path); return 1; }

        if (!verify_png_header(buffer, verbose)) {
            printf("Not a valid PNG file\n");
            free(buffer); free(output_path);
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
            convert_png_to_ascii(decompressed, png, output_path);
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
        size_t file_size;
        unsigned char *buffer = file_buffer_jpg(f, &file_size);
        fclose(f);

        if (!buffer) { printf("Failed to read JPG file\n"); free(output_path); return 1; }

        if (!verify_jpg_header(buffer, file_size, verbose)) {
            printf("Not a valid JPEG file\n");
            free(buffer); free(output_path);
            return 1;
        }

        Jpg *jpg = extract_jpg_info(buffer, file_size);
        if (!jpg) { printf("Failed to extract JPEG info\n"); free(buffer); free(output_path); return 1; }

        if (!jpg->sof || !jpg->dht || !jpg->sos) {
            printf("Missing JPEG sections\n");
            free(buffer); free(jpg); free(output_path);
            return 1;
        }

        Sof *sof = decode_sof(jpg);
        if (!sof) {
            printf("Failed to decode SOF\n");
            free(buffer); free(jpg); free(output_path);
            return 1;
        }

        if (verbose) {
            printf("Image: %dx%d, %d components\n", sof->width, sof->height, sof->num_components);
        }

        Dqt *dqt = decode_dqt(jpg);
        Dht *dht = decode_dht(jpg);
        if (!dht) {
            printf("Failed to decode DHT\n");
            free(buffer); free(jpg); free(sof); if (dqt) free(dqt); free(output_path);
            return 1;
        }

        Sos *sos = decode_sos(jpg);
        if (!sos) {
            printf("Failed to decode SOS\n");
            free(buffer); free(jpg); free(sof); if (dqt) free(dqt); free(dht); free(output_path);
            return 1;
        }

        size_t image_size;
        unsigned char *rgb_image = decode_jpeg(jpg, sof, dqt, dht, sos, &image_size, verbose);

        if (rgb_image) {
            convert_jpg_to_ascii(rgb_image, sof->width, sof->height, output_path);
            free(rgb_image);
        } else {
            printf("Failed to decode image\n");
        }

        free(buffer); free(jpg); free(sof); if (dqt) free(dqt); free(dht); free(sos);

    } else {
        printf("Unsupported file format. Only PNG and JPG are supported.\n");
        fclose(f);
        free(output_path);
        return 1;
    }

    free(output_path);
    return 0;
}