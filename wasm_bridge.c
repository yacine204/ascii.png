#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <emscripten.h>

#include "include/jpg.h"
#include "include/png.h"


static char* make_error(const char *msg) {
    size_t len = strlen(msg) + 1;
    char *err = malloc(len);
    if (err) snprintf(err, len, "%s", msg);
    return err;
}

EMSCRIPTEN_KEEPALIVE
char *convert_to_ascii(unsigned char *data, int len, int term_w, int term_h){
    if (len<8){
        return make_error("ERROR: file too small\n");
    }

    if(is_png_file(data)){
        char *buffer = malloc(len + 1);
        memcpy(buffer, data, len);
        buffer[len] = '\0';

        if(!verify_png_header(buffer, false)){
            free(buffer);
            return make_error("ERROR: not a valid png file!\n");
        }

        Png *png = extract_png_structure(buffer);
        Idat *assembled = assemble_idat(png);
        int bpp = get_bytes_per_pixel(png->color_type);
        unsigned long expected_size = png->height * (1 + (png->width * bpp));
        unsigned char *decompressed = decompress_assembled_idat(assembled, expected_size, false);
        char *result = NULL;
        if (decompressed) {
            defilter_png(decompressed, png, bpp);
            result = convert_png_to_ascii_wasm(decompressed, png, term_w, term_h);
            free(decompressed);
        } else {
            result = malloc(48);
            if (result) snprintf(result, 48, "ERROR: PNG decompression failed\n");
        }

        if (assembled) {
            if (assembled->chunk_data) free(assembled->chunk_data);
            free(assembled);
        }
        if (png->idat) free(png->idat);
        free(png);
        free(buffer);
        return result;

    }else if(is_jpg_file(data)){
        if (!verify_jpg_header(data, len, false)) {
            char *err = malloc(48);
            if (err) snprintf(err, 48, "ERROR: not a valid JPEG file\n");
            return err;
        }

        Jpg *jpg = extract_jpg_info(data, len);
        if (!jpg || !jpg->sof || !jpg->dht || !jpg->sos) {
            char *err = malloc(48);
            if (err) snprintf(err, 48, "ERROR: missing JPEG sections\n");
            return err;
        }

        Sof *sof = decode_sof(jpg);
        Dqt *dqt = decode_dqt(jpg);
        Dht *dht = decode_dht(jpg);
        Sos *sos = decode_sos(jpg);

        char *result = NULL;
        if (sof && dht && sos) {
            size_t image_size;
            unsigned char *rgb_image = decode_jpeg(jpg, sof, dqt, dht, sos, &image_size, false);
            if (rgb_image) {
                result = convert_jpg_to_ascii_wasm(rgb_image, sof->width, sof->height, term_w, term_h);
                free(rgb_image);
            } else {
                result = malloc(48);
                if (result) snprintf(result, 48, "ERROR: JPEG decode failed\n");
            }
        } else {
            result = malloc(48);
            if (result) snprintf(result, 48, "ERROR: JPEG header decode failed\n");
        }

        free(jpg);
        if (sof) free(sof);
        if (dqt) free(dqt);
        if (dht) free(dht);
        if (sos) free(sos);
        return result;

    }else{
        return make_error("ERROR: unsupported file type\n");
    }
}

EMSCRIPTEN_KEEPALIVE
void free_ascii_buffer(char *ptr) {
    if (ptr) free(ptr);
}
