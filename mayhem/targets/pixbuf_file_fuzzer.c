// pixbuf_file_fuzzer — fuzzes gdk_pixbuf_new_from_file() (the primary "decode a whole image
// file" entry point, dispatching through the builtin PNG/JPEG/GIF/BMP/... loaders) plus the
// scale/rotate/option-get/set path. Ported from google/oss-fuzz's gdk-pixbuf project
// (projects/gdk-pixbuf/targets/pixbuf_file_fuzzer.c, Apache-2.0).
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <gdk-pixbuf/gdk-pixbuf.h>

#include "fuzzer_temp_file.h"

// A few-KB GIF/PNG/BMP header can declare a canvas of tens of thousands of pixels per side;
// gdk_pixbuf_new() then callocs gigabytes. Whether that trips libFuzzer's 2048 MB limit, gets
// the process SIGKILLed by a container memory cap, or fails the calloc and returns cleanly
// depends on the host, so the "crash" does not replay. Inputs declaring more than this many
// pixels (64 MB of RGBA) are skipped before any allocation. (gdk_pixbuf_get_file_info() would
// give the size but trips UBSan's function-type check on its own info_cb, so the declared
// size is read from a GdkPixbufLoader's size-prepared signal instead.)
#define MAX_DECLARED_PIXELS ((gint64) 1 << 24)

static void size_prepared_cb(gpointer loader, gint width, gint height, gpointer user_data) {
    *(gint64 *) user_data = (gint64) width * height;
}

static int declares_too_many_pixels(const uint8_t *data, size_t size) {
    gint64 pixels = -1;
    GdkPixbufLoader *loader = gdk_pixbuf_loader_new();
    g_signal_connect(loader, "size-prepared", G_CALLBACK(size_prepared_cb), &pixels);
    for (size_t off = 0; off < size && pixels < 0; off += 64) {
        size_t n = size - off < 64 ? size - off : 64;
        if (!gdk_pixbuf_loader_write(loader, data + off, n, NULL)) break;
    }
    gdk_pixbuf_loader_close(loader, NULL);
    g_object_unref(loader);
    return pixels > MAX_DECLARED_PIXELS;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 1) {
        return 0;
    }
    GdkPixbuf *pixbuf, *rotated, *scaled;
    GError *error = NULL;

    char *tmpfile = fuzzer_get_tmpfile(data, size);
    if (declares_too_many_pixels(data, size)) {
        fuzzer_release_tmpfile(tmpfile);
        return 0;
    }
    pixbuf = gdk_pixbuf_new_from_file(tmpfile, &error);
    if (error != NULL) {
        g_clear_error(&error);
        fuzzer_release_tmpfile(tmpfile);
        return 0;
    }

    char *buf = (char *) calloc(size + 1, sizeof(char));
    memcpy(buf, data, size);
    buf[size] = '\0';

    gdk_pixbuf_get_width(pixbuf);
    gdk_pixbuf_get_height(pixbuf);
    gdk_pixbuf_get_bits_per_sample(pixbuf);

    scaled = gdk_pixbuf_scale_simple(pixbuf,
            gdk_pixbuf_get_width(pixbuf) / 4,
            gdk_pixbuf_get_height(pixbuf) / 4,
            GDK_INTERP_NEAREST);
    if (scaled) g_object_unref(scaled);

    unsigned int rot_amount = ((unsigned int) data[0]) % 4;
    rotated = gdk_pixbuf_rotate_simple(pixbuf, rot_amount * 90);
    g_object_unref(pixbuf);
    pixbuf = rotated;

    if (pixbuf != NULL) {
        gdk_pixbuf_set_option(pixbuf, buf, buf);
        gdk_pixbuf_get_option(pixbuf, buf);
    }

    free(buf);
    g_clear_object(&pixbuf);
    fuzzer_release_tmpfile(tmpfile);
    return 0;
}
