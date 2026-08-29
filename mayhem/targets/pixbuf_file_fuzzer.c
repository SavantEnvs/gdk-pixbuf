// pixbuf_file_fuzzer — fuzzes gdk_pixbuf_new_from_file() (the primary "decode a whole image
// file" entry point, dispatching through the builtin PNG/JPEG/GIF/BMP/... loaders) plus the
// scale/rotate/option-get/set path. Ported from google/oss-fuzz's gdk-pixbuf project
// (projects/gdk-pixbuf/targets/pixbuf_file_fuzzer.c, Apache-2.0).
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <gdk-pixbuf/gdk-pixbuf.h>

#include "fuzzer_temp_file.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 1) {
        return 0;
    }
    GdkPixbuf *pixbuf, *rotated, *scaled;
    GError *error = NULL;

    char *tmpfile = fuzzer_get_tmpfile(data, size);
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
