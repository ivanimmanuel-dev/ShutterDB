#pragma once
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
// The caller owns the returned RGBA buffer and releases it with free().
unsigned char *shutter_decode_jpeg(const unsigned char *data, size_t size, unsigned *width, unsigned *height,
                                   char *message, size_t message_size);
#ifdef __cplusplus
}
#endif
