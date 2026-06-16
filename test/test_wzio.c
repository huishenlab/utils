/* Tests for the line readers in wzio.h.
 *
 * The buffered block reader gzbuf_read_line() is meant to be a faster, drop-in
 * equivalent of the byte-at-a-time gzFile_read_line(). The core test is
 * differential: feed the same input through both readers and assert they
 * produce an identical sequence of lines. gzFile_read_line() is the trusted
 * reference, so anything it does (e.g. dropping a trailing line that has no
 * newline) the block reader must do too.
 *
 * Edge cases covered: empty lines, embedded '\r' (CRLF), a line longer than
 * the reader's block size (forcing reassembly across blocks and buffer growth),
 * and end-of-file both with and without a trailing newline.
 *
 * Test data is generated programmatically into a temp file; nothing is read
 * from disk fixtures.
 *
 * Build/run (from the repository root):
 *   gcc -g -Wall test/test_wzio.c -I. -lz -o test/test_wzio_bin && ./test/test_wzio_bin
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
#include "wzio.h"

static int failures = 0;

#define CHECK(cond, ...) do {                          \
    if (!(cond)) {                                     \
      ++failures;                                      \
      fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); \
      fprintf(stderr, __VA_ARGS__);                    \
      fprintf(stderr, "\n");                           \
    }                                                  \
  } while (0)

/* A growable list of strings. */
typedef struct {
  char **items;
  size_t n, cap;
} strlist_t;

static void strlist_push(strlist_t *l, const char *s, size_t len) {
  if (l->n == l->cap) {
    l->cap = l->cap ? l->cap * 2 : 16;
    l->items = realloc(l->items, l->cap * sizeof(char *));
  }
  char *copy = malloc(len + 1);
  memcpy(copy, s, len);
  copy[len] = '\0';
  l->items[l->n++] = copy;
}

static void strlist_free(strlist_t *l) {
  size_t i;
  for (i = 0; i < l->n; ++i) free(l->items[i]);
  free(l->items);
  l->items = NULL;
  l->n = l->cap = 0;
}

static void write_file(const char *path, const char *data, size_t len) {
  FILE *f = fopen(path, "wb");
  if (!f) { perror(path); exit(2); }
  if (len) fwrite(data, 1, len, f);
  fclose(f);
}

/* Read every line of a file with the byte-at-a-time reader. */
static void read_all_bytewise(const char *path, strlist_t *out) {
  gzFile fh = gzopen(path, "r");
  char *line = NULL;
  while (gzFile_read_line(fh, &line)) strlist_push(out, line, strlen(line));
  free(line);
  gzclose(fh);
}

/* Read every line of a file with the buffered block reader. */
static void read_all_buffered(const char *path, strlist_t *out) {
  gzFile fh = gzopen(path, "r");
  gzbuf_reader_t r;
  gzbuf_init(&r, fh);
  char *line = NULL;
  size_t cap = 0;
  while (gzbuf_read_line(&r, &line, &cap)) strlist_push(out, line, strlen(line));
  free(line);
  gzbuf_free(&r);
  gzclose(fh);
}

/* Assert the two readers return the same lines for the given input. */
static void assert_readers_agree(const char *name, const char *data, size_t len) {
  const char *path = "test_wzio.tmp";
  write_file(path, data, len);

  strlist_t a = {0}, b = {0};
  read_all_bytewise(path, &a);
  read_all_buffered(path, &b);

  CHECK(a.n == b.n, "%s: line count differs (bytewise=%zu buffered=%zu)", name, a.n, b.n);
  size_t i, n = a.n < b.n ? a.n : b.n;
  for (i = 0; i < n; ++i)
    CHECK(strcmp(a.items[i], b.items[i]) == 0,
          "%s: line %zu differs (bytewise len=%zu buffered len=%zu)",
          name, i, strlen(a.items[i]), strlen(b.items[i]));

  strlist_free(&a);
  strlist_free(&b);
  remove(path);
}

/* Convenience for NUL-terminated literals. */
static void agree(const char *name, const char *data) {
  assert_readers_agree(name, data, strlen(data));
}

int main(void) {
  agree("simple", "alpha\nbeta\ngamma\n");
  agree("trailing newline absent", "alpha\nbeta\ngamma");
  agree("empty lines", "\n\nalpha\n\nbeta\n\n");
  agree("crlf preserved", "alpha\r\nbeta\r\n");
  agree("single newline only", "\n");
  agree("one line no newline", "solo");
  assert_readers_agree("empty file", "", 0);

  /* A line longer than the block size forces the buffered reader to reassemble
   * across multiple gzread() blocks and to grow its line buffer. */
  {
    size_t big = (GZBUF_READER_CAP * 2) + 12345;
    char *data = malloc(big + 64);
    size_t pos = 0;
    pos += snprintf(data + pos, 8, "head\n");
    memset(data + pos, 'X', big);
    pos += big;
    pos += snprintf(data + pos, 8, "\ntail\n");
    assert_readers_agree("line spanning blocks", data, pos);
    free(data);
  }

  /* A line that ends exactly on the block boundary. */
  {
    size_t n = GZBUF_READER_CAP;
    char *data = malloc(n + 16);
    memset(data, 'Y', n - 1);
    data[n - 1] = '\n';            /* newline is the last byte of the block */
    size_t pos = n;
    pos += snprintf(data + pos, 8, "after\n");
    assert_readers_agree("newline on block boundary", data, pos);
    free(data);
  }

  if (failures == 0) {
    printf("test_wzio: all checks passed\n");
    return 0;
  }
  fprintf(stderr, "test_wzio: %d check(s) failed\n", failures);
  return 1;
}
