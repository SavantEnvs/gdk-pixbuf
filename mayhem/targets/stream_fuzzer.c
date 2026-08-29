// stream_fuzzer — fuzzes gdk_pixbuf_new_from_stream() over a GFileInputStream, i.e. the
// GIO-stream loading path (distinct code path from gdk_pixbuf_new_from_file: goes through
// GdkPixbufLoader's incremental-write internals driven by a GInputStream reader, rather than
// gdk-pixbuf's own fopen()-based reader). Ported from google/oss-fuzz's gdk-pixbuf project
// (projects/gdk-pixbuf/targets/stream_fuzzer.c, Apache-2.0).
#include <stdint.h>
#include <gdk-pixbuf/gdk-pixbuf.h>

#include "fuzzer_temp_file.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    GError *error = NULL;
    GdkPixbuf *pixbuf;
    GFile *file;
    GInputStream *stream;

    char *tmpfile = fuzzer_get_tmpfile(data, size);
    file = g_file_new_for_path(tmpfile);
    stream = (GInputStream *) g_file_read(file, NULL, &error);
    if (error != NULL) {
        g_clear_error(&error);
        g_object_unref(file);
        fuzzer_release_tmpfile(tmpfile);
        return 0;
    }

    pixbuf = gdk_pixbuf_new_from_stream(stream, NULL, &error);
    if (pixbuf != NULL) {
        g_object_unref(pixbuf);
    }

    g_clear_error(&error);
    g_object_unref(stream);
    g_object_unref(file);
    fuzzer_release_tmpfile(tmpfile);
    return 0;
}
