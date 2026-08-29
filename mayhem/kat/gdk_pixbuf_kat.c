// gdk_pixbuf_kat.c — known-answer-test probe for mayhem/test.sh's behavioral oracle.
//
// Loads a fixed, baked-in PNG fixture entirely through GdkPixbufLoader (the in-memory
// buffer API — no file I/O at all: bytes come from a read of the baked fixture path, which
// is fine per SPEC/PORTING (an absolute path here reads baked-in IMAGE data, not upstream
// source; it is never used to WRITE and never used as a seed path), decodes it, and prints a
// single deterministic line: the image's width, height, channel count, alpha flag, and a
// checksum of every pixel byte (respecting rowstride, so padding is included exactly as
// gdk_pixbuf_get_pixels() lays it out). It then feeds the SAME loader a truncated prefix of
// the fixture and reports whether closing it yields a decode error.
//
// test.sh runs this binary directly from bash and greps its stdout for the exact expected
// line — NOT just its exit code — so a sabotaged/neutered binary (killed before it prints
// anything) makes the comparison fail loudly, per SPEC section 6.3's anti-reward-hacking
// requirement.
//
// This is a plain, non-fuzzer C program: mayhem/build.sh compiles it against the CLEAN
// (non-sanitized, non-fuzzer) oracle build of gdk-pixbuf, so it is an honest, dynamically
// linked assertion of real library behavior.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <gdk-pixbuf/gdk-pixbuf.h>

static int decode_via_loader(const char *path, size_t truncate_to) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "KAT: cannot open fixture %s\n", path);
        return 2;
    }
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (fsize <= 0) {
        fprintf(stderr, "KAT: empty fixture %s\n", path);
        fclose(f);
        return 2;
    }
    unsigned char *buf = malloc((size_t) fsize);
    if (fread(buf, 1, (size_t) fsize, f) != (size_t) fsize) {
        fprintf(stderr, "KAT: short read on fixture %s\n", path);
        fclose(f);
        free(buf);
        return 2;
    }
    fclose(f);

    size_t use = truncate_to && truncate_to < (size_t) fsize ? truncate_to : (size_t) fsize;

    GError *error = NULL;
    GdkPixbufLoader *loader = gdk_pixbuf_loader_new();
    gboolean write_ok = gdk_pixbuf_loader_write(loader, buf, use, &error);
    gboolean close_ok = gdk_pixbuf_loader_close(loader, write_ok ? &error : NULL);

    if (truncate_to) {
        /* Truncated-input case: we only care whether the decode was rejected. */
        printf("KAT truncated: error=%d\n", (!write_ok || !close_ok) ? 1 : 0);
        if (error) g_clear_error(&error);
        g_object_unref(loader);
        free(buf);
        return 0;
    }

    if (!write_ok || !close_ok) {
        fprintf(stderr, "KAT: full fixture failed to decode: %s\n",
                error ? error->message : "(no error set)");
        if (error) g_clear_error(&error);
        g_object_unref(loader);
        free(buf);
        return 3;
    }

    GdkPixbuf *pixbuf = gdk_pixbuf_loader_get_pixbuf(loader);
    if (!pixbuf) {
        fprintf(stderr, "KAT: no pixbuf after successful close\n");
        g_object_unref(loader);
        free(buf);
        return 3;
    }

    int width = gdk_pixbuf_get_width(pixbuf);
    int height = gdk_pixbuf_get_height(pixbuf);
    int n_channels = gdk_pixbuf_get_n_channels(pixbuf);
    int has_alpha = gdk_pixbuf_get_has_alpha(pixbuf);
    int rowstride = gdk_pixbuf_get_rowstride(pixbuf);
    guchar *pixels = gdk_pixbuf_get_pixels(pixbuf);

    unsigned long long checksum = 0;
    for (int y = 0; y < height; y++) {
        guchar *row = pixels + (size_t) y * (size_t) rowstride;
        for (int x = 0; x < rowstride; x++) {
            checksum = checksum * 131 + row[x];
        }
    }

    printf("KAT full: width=%d height=%d n_channels=%d has_alpha=%d checksum=%llu\n",
           width, height, n_channels, has_alpha, checksum);

    g_object_unref(loader);
    free(buf);
    return 0;
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s <fixture.png>\n", argv[0]);
        return 2;
    }
    int rc1 = decode_via_loader(argv[1], 0);
    int rc2 = decode_via_loader(argv[1], 64);
    return rc1 != 0 ? rc1 : rc2;
}
