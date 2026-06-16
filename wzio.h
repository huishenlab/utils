#ifndef _WZIO_H
#define _WZIO_H

#include <zlib.h>
#include "wzmisc.h"
#include "wvec.h"

/*******************************
 ** Open file for reading and **
 ** error handling            **
 *******************************/
static inline gzFile wzopen(char *path) {
  gzFile fh;
  if (strcmp(path, "-") == 0) {
    fh = gzdopen(fileno(stdin), "r");
  } else {
    fh = gzopen(path, "r");
    if (!fh) {
      fprintf(stderr, "[%s:%d] Fatal, cannot open file: %s\n",
              __func__, __LINE__, path);
      fflush(stderr);
      exit(1);
    }
  }
  return fh;
}

static inline FILE *wzopen_out(char *path) {
   FILE *out;
   if (path) {
      out = fopen(path, "w");
      if (!out) {
         fprintf(stderr, "[%s:%d] Fatal, cannot open file: %s\n",
                 __func__, __LINE__, path);
         fflush(stderr);
         exit(1);
      }
   } else {
      out = stdout;
   }
   return out;
}

#define wzclose gzclose

/*****************************
 ** Read one line from file **
 *****************************

 * Usage:
 * char *line;
 * gzFile_read_line(fh, &line);
 *
 * "*s" is either NULL or 
 * previously allocated c-string
 * returns 1 if hitting \n 0 if EOF */
static inline int gzFile_read_line(gzFile fh, char **s) {

  if (s == NULL) {
    fprintf(stderr, "[%s:%d] Fatal, empty string construct.\n", __func__, __LINE__);
    fflush(stderr);
    exit(1);
  }
  
  /* Establish a known floor for the buffer on every call. Starting from a
   * reasonable size (rather than 10) means typical lines never trigger the
   * geometric growth below, and re-requesting the same size on each call is a
   * cheap no-op in the allocator once the buffer has reached it -- so a buffer
   * reused across many calls (e.g. reading every record of a VCF) is allocated
   * once and grown only for the occasional over-long line. */
  int m = 4096, l = 0;          /* memory and string length */
  *s = realloc(*s, m);

  /* read until '\n' or EOF */
  while (1) {
    int c = gzgetc(fh);
    if (l > m-2) { m <<= 1; *s = realloc(*s, m); }
    if (c == '\n') {(*s)[l] = '\0'; return 1;}
    if (c == EOF) {(*s)[l] = '\0'; return 0;}
    (*s)[l++] = c;
  }
  return 0;                     /* should not come here */
}

/*****************************************
 ** Buffered line reader                **
 *****************************************
 * gzFile_read_line() above reads one byte at a time via gzgetc(), which is a
 * large cost when scanning big files line by line. This reader pulls the
 * stream in large blocks (via gzread) and splits lines in memory with memchr,
 * eliminating the per-byte overhead.
 *
 * It is line-compatible with gzFile_read_line(): gzbuf_read_line() returns 1
 * for each '\n'-terminated line (the '\n' is stripped, the result is
 * NUL-terminated) and 0 once the stream is exhausted. A trailing line without
 * a newline is not returned (return 0), matching gzFile_read_line(). */

#define GZBUF_READER_CAP (1 << 20)    /* 1 MiB read block */

typedef struct gzbuf_reader_t {
  gzFile fh;
  unsigned char *buf;           /* read block */
  int beg, end;                 /* valid bytes live in buf[beg, end) */
  int eof;                      /* stream exhausted */
} gzbuf_reader_t;

static inline void gzbuf_init(gzbuf_reader_t *r, gzFile fh) {
  r->fh = fh;
  r->buf = malloc(GZBUF_READER_CAP);
  r->beg = r->end = 0;
  r->eof = 0;
}

static inline void gzbuf_free(gzbuf_reader_t *r) {
  free(r->buf);
  r->buf = NULL;
}

/* Read one line into *s (capacity tracked in *cap, grown as needed and reused
 * across calls). Returns 1 on a line, 0 at end of stream. */
static inline int gzbuf_read_line(gzbuf_reader_t *r, char **s, size_t *cap) {
  size_t l = 0;
  if (*cap < 1) { *cap = 4096; *s = realloc(*s, *cap); }
  while (1) {
    if (r->beg >= r->end) {     /* block consumed: refill */
      if (r->eof) { (*s)[l] = '\0'; return 0; }
      int n = gzread(r->fh, r->buf, GZBUF_READER_CAP);
      if (n <= 0) { r->eof = 1; (*s)[l] = '\0'; return 0; }
      r->beg = 0; r->end = n;
    }
    unsigned char *win = r->buf + r->beg;
    int avail = r->end - r->beg;
    unsigned char *nl = memchr(win, '\n', avail);
    size_t chunk = nl ? (size_t)(nl - win) : (size_t)avail;
    if (l + chunk + 1 > *cap) {
      while (l + chunk + 1 > *cap) *cap <<= 1;
      *s = realloc(*s, *cap);
    }
    memcpy(*s + l, win, chunk);
    l += chunk;
    r->beg += chunk;
    if (nl) {                   /* found end of line */
      r->beg++;                 /* skip the '\n' */
      (*s)[l] = '\0';
      return 1;
    }
    /* no newline in this block: keep accumulating across refills */
  }
}

/****************************
 ** Get one field by index **
 ****************************
 field_index is 0-based
 result creates a new allocated object,
 return 0 if there are not enough fields, 1 if success */
static inline int line_get_field(const char *line, int field_index, const char *sep, char **field) {

  char *working = calloc(strlen(line) + 1, sizeof(char));
  strcpy(working, line);
  char *tok;

  tok = strtok(working, sep);
  int i;
  for (i=0; i<field_index; ++i)
    tok = strtok(NULL, sep);

  if (tok == NULL) {            /* not enough fields */
    free(working);
    return 0;
  }

  *field = strdup(tok);
  free(working);
  return 1;
}

/********************************
 ** Get all fields of one line **
 ********************************
Usage:
   char **fields; int nfields;
   line_get_fields("my line", " ", &fields, &nfields);
   free_fields(fields, nfields);

   Note: separators/delimiters are not merged - the most likely use-case. */
#define free_fields(fds, nfds) free_char_array(fds, nfds)
static inline void line_get_fields(const char *line, const char *sep, char ***fields, int *nfields) {

  *nfields = 1;
  const char *s = line;
  while ((s = strpbrk(s, sep)) != NULL) { (*nfields)++; s++; }

  *fields = calloc(*nfields, sizeof(char *));
  char *working = calloc(strlen(line) + 1, sizeof(char));
  strcpy(working, line);
  char *tok; int i;

  tok = strtok(working, sep);
  for (i=0; tok != NULL; ++i) {
    (*fields)[i] = strdup(tok);
    tok = strtok(NULL, sep);
  }
  free(working);
}


#endif /* _WZIO_H */
