/*
 * ahff2png.c - disunityed file tool
 * Copyright (c) 2015 The Holy Constituency of the Summer Triangle.
 * All rights reserved.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <fcntl.h>

#include "lodepng.h"

#define EXPECT_SIZE_CHK_AND_WARN() do { \
    if (info.s.datsize != expect_size) \
        printf("warning: image data is larger than expected " \
               "(%u as opposed to %u).\n" \
               "(if you want to see what lies beyond, tweak the " \
               "file's image size in a hex editor. consult the definition " \
               "of ahff_header_t for more information.)\n", info.s.datsize, expect_size); \
} while (0)

typedef union {
    unsigned char b[4];
    uint32_t n;
} byte_addressable_uint32;
typedef unsigned char byte;

// the Ad-Hoc File Format is intentionally very simple so i don't have
// to write so much java
// packed should have no effect, this is just for safety reasons
typedef union {
    byte b[16];
    struct __attribute__((packed)) {
        uint32_t width;
        uint32_t height;
        uint32_t datsize;
        uint32_t pixel_format;
    } s;
} ahff_header_t;

enum /* pixel_format_t */ {
    A8 = 1,
    RGB24 = 3,
    RGBA32 = 4,
    ARGB32 = 5,
    RGB565 = 7,
    RGBA4444 = 13,
    PVRTC_RGB4 = 32,
    PVRTC_RGBA4 = 33,
    ETC1RGB = 34,
};

#define READ_FULLY(fd, buf, size) do { \
    size_t _eval_one = (size); \
    size_t nread = read(fd, buf, _eval_one); \
    assert(nread == _eval_one || !"READ_FULLY did not read fully."); \
} while(0)

#include "pixel.c"

void flip_image_sideways(byte *buf, uint32_t width, uint32_t height) {
    byte *work = malloc(width * 4);
    byte *worp = work;

    for (int row = 0; row < height; ++row) {
        byte *crow = buf + (row * width * 4);
        worp = work;

        for (size_t i = (width - 1) * 4; i > 0; i -= 4) {
            memcpy(worp, crow + i, 4);
            worp += 4;
        }

        memcpy(crow, work, width * 4);
    }

    free(work);
}

void flip_image_upside_down(byte *buf, uint32_t width, uint32_t height) {
    byte *work = malloc(width * 4);

    for (int row = 0, target_row = height - 1; row < (height / 2); ++row, --target_row) {
        memcpy(work, buf + (target_row * width * 4), width * 4);
        memcpy(buf + (target_row * width * 4), buf + (row * width * 4), width * 4);
        memcpy(buf + (row * width * 4), work, width * 4);
    }

    free(work);
}

int main (int argc, char const *argv[]) {
    int fd = open(argv[1], O_RDONLY);
    assert(fd >= 0);

    ahff_header_t info = { 0 };
    READ_FULLY(fd, info.b, sizeof(ahff_header_t));

    unsigned char *buf = malloc(info.s.datsize);
    unsigned char *out = calloc(info.s.width * info.s.height, 4);
    assert(buf && out);

    READ_FULLY(fd, buf, info.s.datsize);

    int len = strlen(argv[1]);
    char *cp = strdup(argv[1]);
    char *fn;
    if (strcmp(cp + len - 5, ".ahff") == 0) {
        memcpy(cp + len - 5, ".png\0", 5);
        fn = cp;
    } else {
        fn = malloc(len + 5);
        snprintf(fn, len + 5, "%s.png", cp);
        free(cp);
    }
    printf("[>] %s (%d): ", fn, info.s.pixel_format);

    uint32_t point_count = info.s.width * info.s.height;
    uint32_t expect_size = 0;

    switch (info.s.pixel_format) {
        case A8:
            expect_size = point_count;
            EXPECT_SIZE_CHK_AND_WARN();
            copy_1bpp_alpha(buf, expect_size, out);
            break;
        case RGB24:
            expect_size = point_count * 3;
            EXPECT_SIZE_CHK_AND_WARN();
            copy_3bpp_rgb(buf, expect_size, out);
            break;
        case RGB565:
            expect_size = point_count * 2;
            EXPECT_SIZE_CHK_AND_WARN();
            copy_2bpp_rgb565(buf, expect_size, out);
            break;
        case RGBA4444:
            expect_size = point_count * 2;
            EXPECT_SIZE_CHK_AND_WARN();
            copy_2bpp_rgba4444(buf, expect_size, out);
            break;
        case RGBA32:
            expect_size = point_count * 4;
            EXPECT_SIZE_CHK_AND_WARN();
            memcpy(out, buf, expect_size);
            break;
        case ARGB32:
            expect_size = point_count * 4;
            EXPECT_SIZE_CHK_AND_WARN();
            copy_4bpp_argb(buf, expect_size, out);
            break;
        case PVRTC_RGB4:
        case PVRTC_RGBA4:
            expect_size = point_count / 2;
            EXPECT_SIZE_CHK_AND_WARN();
            copy_pvrtc4_rgba(buf, out, info.s.width, info.s.height);
            //copy_pvrtc_rgba(buf, expect_size, out, info.s.width, info.s.height);
            break;
        case ETC1RGB:
            /* ETC1 encodes 4x4 blocks.
             * So w and h must be multiples of 4. */
            assert(info.s.width % 4 == 0);
            assert(info.s.height % 4 == 0);
            expect_size = point_count / 2;
            EXPECT_SIZE_CHK_AND_WARN();
            copy_etc1_rgb(buf, out, info.s.width, info.s.height);
            //copy_etc1_rgb(buf, expect_size, out, info.s.width);
            break;
        default:
            fprintf(stderr, "unknown pixel format %d\n", info.s.pixel_format);
            goto end;
    }

    // flip_image_sideways(out, info.width, info.height);
    flip_image_upside_down(out, info.s.width, info.s.height);
    int ret = lodepng_encode32_file(fn, out, info.s.width, info.s.height);
    printf("%d\n", ret);

  end:
    free(fn);
    free(buf);
    free(out);
    return 0;
}
