// fuzzer_temp_file.h — adapter from fuzzer bytes to a temporary file, for gdk-pixbuf
// entry points that take a file path rather than an in-memory buffer.
//
// Ported (logic otherwise unmodified) from google/oss-fuzz's gdk-pixbuf project
// (projects/gdk-pixbuf/targets/fuzzer_temp_file.h, Apache-2.0), which is the upstream
// OSS-Fuzz harness set for this library. The temp file is created fresh under $TMPDIR
// (falling back to /tmp if unset, matching mkstemp's own convention elsewhere in this
// fleet) and removed before returning — no fixed/relative repo path is ever read, so
// this does not hit the "harness reads a relative testdata path" trap: the bytes it
// reads back are exactly the bytes the fuzzer just wrote, nothing pre-seeded on disk.

#ifndef FUZZER_TEMP_FILE_H_
#define FUZZER_TEMP_FILE_H_

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static char *fuzzer_get_tmpfile(const uint8_t *data, size_t size) {
  const char *tmpdir = getenv("TMPDIR");
  if (!tmpdir || !*tmpdir) tmpdir = "/tmp";
  char *filename_buffer = malloc(PATH_MAX);
  if (!filename_buffer) {
    perror("Failed to allocate file name buffer.");
    abort();
  }
  if (snprintf(filename_buffer, PATH_MAX, "%s/gdk_pixbuf_fuzz.XXXXXX", tmpdir) >=
      (int)PATH_MAX) {
    fprintf(stderr, "TMPDIR path too long\n");
    free(filename_buffer);
    abort();
  }
  const int file_descriptor = mkstemp(filename_buffer);
  if (file_descriptor < 0) {
    perror("Failed to make temporary file.");
    abort();
  }
  FILE *file = fdopen(file_descriptor, "wb");
  if (!file) {
    perror("Failed to open file descriptor.");
    close(file_descriptor);
    abort();
  }
  const size_t bytes_written = fwrite(data, sizeof(uint8_t), size, file);
  if (bytes_written < size) {
    close(file_descriptor);
    fprintf(stderr, "Failed to write all bytes to file (%zu out of %zu)",
            bytes_written, size);
    abort();
  }
  fclose(file);
  return filename_buffer;
}

static void fuzzer_release_tmpfile(char *filename) {
  if (unlink(filename) != 0) {
    perror("WARNING: Failed to delete temporary file.");
  }
  free(filename);
}

#endif // FUZZER_TEMP_FILE_H_
