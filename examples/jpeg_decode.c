#include "jpeg_decode.h"
#include <stdio.h>

#include <jpeglib.h>
#include <setjmp.h>
#include <stdlib.h>

struct jpeg_failure {
    struct jpeg_error_mgr base;
    jmp_buf jump;
    char message[JMSG_LENGTH_MAX];
};
struct decode_state {
    struct jpeg_decompress_struct decoder;
    struct jpeg_failure failure;
    unsigned char *pixels;
    unsigned char *row;
};
static void copy_message(char *output, size_t capacity, const char *text) {
    if (!capacity)
        return;
    size_t i = 0;
    for (; i + 1 < capacity && text[i]; ++i)
        output[i] = text[i];
    output[i] = '\0';
}
static void fail(j_common_ptr decoder) {
    struct jpeg_failure *failure = (struct jpeg_failure *)decoder->err;
    decoder->err->format_message(decoder, failure->message);
    longjmp(failure->jump, 1);
}
static void warning(j_common_ptr decoder, int level) {
    if (level < 0)
        fail(decoder);
}
unsigned char *shutter_decode_jpeg(const unsigned char *data, size_t size, unsigned *width, unsigned *height,
                                   char *message, size_t message_size) {
    struct decode_state *state = (struct decode_state *)calloc(1, sizeof(*state));
    if (!state) {
        copy_message(message, message_size, "JPEG allocation failed");
        return NULL;
    }
    state->decoder.err = jpeg_std_error(&state->failure.base);
    state->failure.base.error_exit = fail;
    state->failure.base.emit_message = warning;
    // libjpeg's longjmp stays in C, without crossing C++ objects that need destruction.
    if (setjmp(state->failure.jump)) {
        copy_message(message, message_size, state->failure.message);
        jpeg_destroy_decompress(&state->decoder);
        free(state->pixels);
        free(state->row);
        free(state);
        return NULL;
    }
    jpeg_create_decompress(&state->decoder);
    jpeg_mem_src(&state->decoder, data, (unsigned long)size);
    jpeg_read_header(&state->decoder, TRUE);
    if (!state->decoder.image_width || !state->decoder.image_height || state->decoder.image_width > 4096 ||
        state->decoder.image_height > 4096) {
        copy_message(message, message_size, "expected dimensions 1..4096");
        jpeg_destroy_decompress(&state->decoder);
        free(state);
        return NULL;
    }
    state->decoder.out_color_space = JCS_RGB;
    jpeg_start_decompress(&state->decoder);
    *width = state->decoder.output_width;
    *height = state->decoder.output_height;
    state->pixels = (unsigned char *)malloc((size_t)*width * *height * 4);
    state->row = (unsigned char *)malloc((size_t)*width * 3);
    if (!state->pixels || !state->row) {
        copy_message(message, message_size, "JPEG pixel allocation failed");
        jpeg_destroy_decompress(&state->decoder);
        free(state->pixels);
        free(state->row);
        free(state);
        return NULL;
    }
    while (state->decoder.output_scanline < state->decoder.output_height) {
        const size_t y = state->decoder.output_scanline;
        jpeg_read_scanlines(&state->decoder, &state->row, 1);
        for (size_t x = 0; x < *width; ++x) {
            unsigned char *pixel = state->pixels + (y * *width + x) * 4;
            pixel[0] = state->row[x * 3];
            pixel[1] = state->row[x * 3 + 1];
            pixel[2] = state->row[x * 3 + 2];
            pixel[3] = 255;
        }
    }
    jpeg_finish_decompress(&state->decoder);
    jpeg_destroy_decompress(&state->decoder);
    unsigned char *pixels = state->pixels;
    free(state->row);
    free(state);
    return pixels;
}
