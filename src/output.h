#ifndef LY_OUTPUT_H
#define LY_OUTPUT_H
#include "common.h"
/* Filename/output are newly allocated, caller-owned. dirfd pins launch directory. */
char *output_basename(const char *source, size_t n, size_t max_bytes);
bool output_save(int dirfd, const char *original, size_t n,
                 const Buffer *result, char **filename, char *error);
bool output_save_report(int dirfd, const Buffer *report, char **filename, char *error);
#endif
