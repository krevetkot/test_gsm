#ifndef CSVUTILITY_H
#define CSVUTILITY_H

#include "csv_error.h"

CsvError csv_load(const char *filename);
CsvError csv_eval_all(void);
void     csv_print(void);

#endif