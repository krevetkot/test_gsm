#ifndef CSV_ERROR_H
#define CSV_ERROR_H

#include <stdio.h>

typedef enum {
    CSV_OK         = 0,
    CSV_ERR_IO     = 1,
    CSV_ERR_FORMAT = 2,
    CSV_ERR_EVAL   = 3,
} CsvError;

#define CSV_FAIL_IO(fmt, ...) (fprintf(stderr, "IO error: " fmt "\n", ##__VA_ARGS__), CSV_ERR_IO)

#define CSV_FAIL_FORMAT(fmt, ...) (fprintf(stderr, "Format error: " fmt "\n", ##__VA_ARGS__), CSV_ERR_FORMAT)

#define CSV_FAIL_EVAL(fmt, ...) (fprintf(stderr, "Eval error: " fmt "\n", ##__VA_ARGS__), CSV_ERR_EVAL)

#endif