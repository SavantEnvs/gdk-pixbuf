// pixbuf_cons_fuzzer — fuzzes GdkPixbuf CONSTRUCTION directly from a raw in-memory pixel
// buffer (g_object_new(GDK_TYPE_PIXBUF, "pixel-bytes", ...)) plus the transform ops
// (scale/rotate/flip/composite). No file I/O anywhere in this target — pure in-memory bytes
// in, straight through GObject construction. Ported from google/oss-fuzz's gdk-pixbuf
// project (projects/gdk-pixbuf/targets/pixbuf_cons_fuzzer.c, Apache-2.0).
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <gdk-pixbuf/gdk-pixbuf.h>

#define WIDTH 10
#define HEIGHT 20
#define ROWSTRIDE (WIDTH * 4)

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (!(size >= WIDTH * HEIGHT * 4)) {
        return 0;
    }
    const gchar *profile;
    GdkPixbuf *pixbuf, *tmp, *tmp2;
    GBytes *bytes;
    bytes = g_bytes_new_static(data, size);
    pixbuf = g_object_new(GDK_TYPE_PIXBUF,
            "width", WIDTH,
            "height", HEIGHT,
            "rowstride", ROWSTRIDE,
            "bits-per-sample", 8, "n-channels", 3,
            "has-alpha", FALSE,
            "pixel-bytes", bytes,
            NULL);
    if (pixbuf == NULL) {
        g_bytes_unref(bytes);
        return 0;
    }

    tmp = gdk_pixbuf_scale_simple(pixbuf,
            gdk_pixbuf_get_width(pixbuf) / 4,
            gdk_pixbuf_get_height(pixbuf) / 4,
            GDK_INTERP_NEAREST);
    if (tmp) g_object_unref(tmp);

    unsigned int rot_amount = ((unsigned int) data[0]) % 4;
    tmp = gdk_pixbuf_rotate_simple(pixbuf, rot_amount * 90);
    if (tmp) g_object_unref(tmp);

    tmp = gdk_pixbuf_flip(pixbuf, TRUE);
    if (tmp) g_object_unref(tmp);

    tmp = gdk_pixbuf_composite_color_simple(pixbuf,
            gdk_pixbuf_get_width(pixbuf) / 4,
            gdk_pixbuf_get_height(pixbuf) / 4,
            GDK_INTERP_NEAREST,
            128,
            8,
            G_MAXUINT32,
            G_MAXUINT32/2);
    if (tmp) g_object_unref(tmp);

    char *buf = (char *) calloc(size + 1, sizeof(char));
    memcpy(buf, data, size);
    buf[size] = '\0';

    gdk_pixbuf_set_option(pixbuf, buf, buf);
    profile = gdk_pixbuf_get_option(pixbuf, buf);
    (void) profile;
    tmp = gdk_pixbuf_new_from_data(gdk_pixbuf_get_pixels(pixbuf),
            GDK_COLORSPACE_RGB,
            FALSE,
            gdk_pixbuf_get_bits_per_sample(pixbuf),
            gdk_pixbuf_get_width(pixbuf),
            gdk_pixbuf_get_height(pixbuf),
            gdk_pixbuf_get_rowstride(pixbuf),
            NULL,
            NULL);
    tmp2 = gdk_pixbuf_flip(tmp, TRUE);
    if (tmp) g_object_unref(tmp);

    free(buf);
    g_bytes_unref(bytes);
    g_object_unref(pixbuf);
    if (tmp2) g_object_unref(tmp2);
    return 0;
}
