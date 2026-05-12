#include <stdio.h>
#include "csvutility.h"
#include "csv_error.h"

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <file.csv>\n", argv[0]);
        return 1;
    }

    CsvError err;

    err = csv_load(argv[1]);
    if (err != CSV_OK)
        return err;

    err = csv_eval_all();
    if (err != CSV_OK)
        return err;

    csv_print();
    return 0;
}