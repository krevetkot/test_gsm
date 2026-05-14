#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "csvutility.h"
#include "csv_error.h"

#define INITIAL_CAPACITY 16
#define MAX_CELL_LEN     256
#define MAX_COL_NAME     64

typedef struct {
    char *key;
    int value;
    int used;
} ColEntry;

typedef struct {
    int key;
    int value;
    int used;
} RowEntry;

typedef struct {
    ColEntry *data;
    size_t size;
    size_t capacity;
} ColMap;

typedef struct {
    RowEntry *data;
    size_t size;
    size_t capacity;
} RowMap;

typedef struct {
    char raw[MAX_CELL_LEN];
    int value;
    int computed;
} Cell;

static ColMap col_map;
static RowMap row_map;

static char **col_names = NULL;
static int *row_ids = NULL;
static Cell *table = NULL;

static int num_cols = 0;
static int num_rows = 0;

static int rows_capacity = 0;

#define CELL(r,c) table[(r) * num_cols + (c)]

static char *strdup_local(const char *s) {
    size_t len = strlen(s) + 1;
    char *copy = malloc(len);

    if (!copy)
        return NULL;

    memcpy(copy, s, len);
    return copy;
}

static unsigned int hash_str(const char *s) {
    unsigned int h = 5381;
    while (*s)
        h = ((h << 5) + h) ^ (unsigned char)*s++;
    return h;
}

static unsigned int hash_int(int k) {
    return (unsigned int)k;
}

static int col_map_init(void) {
    col_map.capacity = INITIAL_CAPACITY;
    col_map.size = 0;
    col_map.data = calloc(col_map.capacity, sizeof(ColEntry));
    return col_map.data ? 0 : -1;
}

static int row_map_init(void) {
    row_map.capacity = INITIAL_CAPACITY;
    row_map.size = 0;
    row_map.data = calloc(row_map.capacity, sizeof(RowEntry));
    return row_map.data ? 0 : -1;
}

static int col_map_rehash(void) {
    size_t new_cap = col_map.capacity * 2;
    ColEntry *new_data = calloc(new_cap, sizeof(ColEntry));

    if (!new_data)
        return -1;

    for (size_t i = 0; i < col_map.capacity; i++) {
        if (!col_map.data[i].used)
            continue;

        unsigned int h = hash_str(col_map.data[i].key);

        for (size_t probe = 0; probe < new_cap; probe++) {
            size_t slot = (h + probe) % new_cap;

            if (!new_data[slot].used) {
                new_data[slot] = col_map.data[i];
                break;
            }
        }
    }

    free(col_map.data);
    col_map.data = new_data;
    col_map.capacity = new_cap;
    return 0;
}

static int row_map_rehash(void) {
    size_t new_cap = row_map.capacity * 2;
    RowEntry *new_data = calloc(new_cap, sizeof(RowEntry));

    if (!new_data)
        return -1;

    for (size_t i = 0; i < row_map.capacity; i++) {
        if (!row_map.data[i].used)
            continue;

        unsigned int h = hash_int(row_map.data[i].key);

        for (size_t probe = 0; probe < new_cap; probe++) {
            size_t slot = (h + probe) % new_cap;

            if (!new_data[slot].used) {
                new_data[slot] = row_map.data[i];
                break;
            }
        }
    }

    free(row_map.data);
    row_map.data = new_data;
    row_map.capacity = new_cap;

    return 0;
}

static int col_map_insert(const char *name, int idx) {
    double load = (double)col_map.size / (double)col_map.capacity;

    if (load > 0.7) {
        if (col_map_rehash() != 0)
            return -1;
    }

    unsigned int h = hash_str(name);

    for (size_t probe = 0; probe < col_map.capacity; probe++) {
        size_t slot = (h + probe) % col_map.capacity;

        if (!col_map.data[slot].used) {
            col_map.data[slot].key = strdup_local(name);

            if (!col_map.data[slot].key)
                return -1;

            col_map.data[slot].value = idx;
            col_map.data[slot].used = 1;
            col_map.size++;

            return 0;
        }

        if (strcmp(col_map.data[slot].key, name) == 0) {
            return -1;
        }
    }
    return -1;
}

static int row_map_insert(int id, int idx) {
    double load = (double)row_map.size / (double)row_map.capacity;

    if (load > 0.7) {
        if (row_map_rehash() != 0)
            return -1;
    }

    unsigned int h = hash_int(id);

    for (size_t probe = 0; probe < row_map.capacity; probe++) {
        size_t slot = (h + probe) % row_map.capacity;

        if (!row_map.data[slot].used) {
            row_map.data[slot].key = id;
            row_map.data[slot].value = idx;
            row_map.data[slot].used = 1;
            row_map.size++;

            return 0;
        }

        if (row_map.data[slot].key == id)
            return -1;
    }
    return -1;
}

static int col_map_find(const char *name) {
    unsigned int h = hash_str(name);

    for (size_t probe = 0; probe < col_map.capacity; probe++) {
        size_t slot = (h + probe) % col_map.capacity;

        if (!col_map.data[slot].used)
            return -1;

        if (strcmp(col_map.data[slot].key, name) == 0) {
            return col_map.data[slot].value;
        }
    }

    return -1;
}

static int row_map_find(int id) {
    unsigned int h = hash_int(id);

    for (size_t probe = 0; probe < row_map.capacity; probe++) {
        size_t slot = (h + probe) % row_map.capacity;

        if (!row_map.data[slot].used)
            return -1;

        if (row_map.data[slot].key == id)
            return row_map.data[slot].value;
    }
    return -1;
}

static void trim_right(char *s) {
    int n = (int)strlen(s);

    while (n > 0 && (s[n - 1] == '\r' || s[n - 1] == '\n' || s[n - 1] == ' ')) {
        s[--n] = '\0';
    }
}

static int split_csv(char *buf, char *out[]) {
    int count = 0;
    char *p = buf;

    while (1) {
        out[count++] = p;
        char *comma = strchr(p, ',');

        if (!comma)
            break;

        *comma = '\0';
        p = comma + 1;
    }
    return count;
}

static CsvError eval_cell(int ri, int ci, int *out_val);

static CsvError parse_ref(const char *ref, int *row_out, int *col_out) {
    int len = (int)strlen(ref);
    int i = len - 1;

    while (i >= 0 && isdigit((unsigned char)ref[i])) {
        i--;
    }

    if (i < 0 || i == len - 1)
        return CSV_ERR_EVAL;

    char col_name[MAX_COL_NAME];
    strncpy(col_name, ref, i + 1);
    col_name[i + 1] = '\0';

    int row_id = atoi(ref + i + 1);
    if (row_id <= 0)
        return CSV_ERR_EVAL;

    int ci = col_map_find(col_name);

    if (ci < 0)
        return CSV_ERR_EVAL;

    int ri = row_map_find(row_id);

    if (ri < 0)
        return CSV_ERR_EVAL;

    *row_out = ri;
    *col_out = ci;

    return CSV_OK;
}

static CsvError parse_arg(const char *arg, int *val) {
    const char *p = arg;

    if (*p == '-')
        p++;

    int all_digits = (*p != '\0');
    while (*p) {
        if (!isdigit((unsigned char)*p)) {
            all_digits = 0;
            break;
        }
        p++;
    }
    if (all_digits) {
        *val = atoi(arg);
        return CSV_OK;
    }
    int r, c;
    CsvError err = parse_ref(arg, &r, &c);

    if (err != CSV_OK)
        return err;

    return eval_cell(r, c, val);
}

static CsvError eval_cell(int ri, int ci, int *out_val) {
    Cell *cell = &CELL(ri, ci);

    if (cell->computed == 1) {
        *out_val = cell->value;
        return CSV_OK;
    }

    if (cell->computed == -1) {
        return CSV_FAIL_EVAL(
            "circular reference at %s%d",
            col_names[ci],
            row_ids[ri]
        );
    }

    if (cell->raw[0] != '=') {
        char *end;
        long v = strtol(cell->raw, &end, 10);

        if (*end != '\0') {
            return CSV_FAIL_EVAL(
                "invalid cell value '%s' at %s%d",
                cell->raw,
                col_names[ci],
                row_ids[ri]
            );
        }

        cell->value = (int)v;
        cell->computed = 1;
        *out_val = cell->value;
        return CSV_OK;
    }

    cell->computed = -1;

    const char *expr = cell->raw + 1;
    char arg1[MAX_CELL_LEN];
    char arg2[MAX_CELL_LEN];
    char op = 0;
    int op_pos = -1;
    int start = (expr[0] == '-') ? 1 : 0;
    for (int k = start; expr[k] != '\0'; k++) {
        char ch = expr[k];
        if (ch == '+' || ch == '-' || ch == '*' || ch == '/') {
            op = ch;
            op_pos = k;
            break;
        }
    }

    if (op_pos < 0) {
        cell->computed = 0;
        return CSV_FAIL_EVAL(
            "no operator in formula '%s' at %s%d",
            cell->raw,
            col_names[ci],
            row_ids[ri]
        );
    }
    if (op_pos == 0 || expr[op_pos + 1] == '\0') {
        cell->computed = 0;
        return CSV_FAIL_EVAL(
            "malformed formula '%s' at %s%d",
            cell->raw,
            col_names[ci],
            row_ids[ri]
        );
    }

    strncpy(arg1, expr, op_pos);
    arg1[op_pos] = '\0';
    strcpy(arg2, expr + op_pos + 1);

    int v1, v2;
    CsvError err;

    err = parse_arg(arg1, &v1);
    if (err != CSV_OK) {
        cell->computed = 0;
        return CSV_FAIL_EVAL(
            "cannot resolve '%s' in '%s' at %s%d",
            arg1,
            cell->raw,
            col_names[ci],
            row_ids[ri]
        );
    }

    err = parse_arg(arg2, &v2);
    if (err != CSV_OK) {
        cell->computed = 0;
        return CSV_FAIL_EVAL(
            "cannot resolve '%s' in '%s' at %s%d",
            arg2,
            cell->raw,
            col_names[ci],
            row_ids[ri]
        );
    }

    int result;
    switch (op) {
        case '+':
            result = v1 + v2;
            break;
        case '-':
            result = v1 - v2;
            break;
        case '*':
            result = v1 * v2;
            break;
        case '/':
            if (v2 == 0) {
                cell->computed = 0;
                return CSV_FAIL_EVAL(
                    "division by zero in '%s' at %s%d",
                    cell->raw,
                    col_names[ci],
                    row_ids[ri]
                );
            }
            result = v1 / v2;
            break;
        default:
            cell->computed = 0;
            return CSV_FAIL_EVAL(
                "unknown operator '%c' at %s%d",
                op,
                col_names[ci],
                row_ids[ri]
            );
    }

    cell->value = result;
    cell->computed = 1;
    *out_val = result;
    return CSV_OK;
}

CsvError csv_load(const char *filename) {
    FILE *f = fopen(filename, "r");
    if (!f)
        return CSV_FAIL_IO("cannot open '%s'", filename);

    if (col_map_init() != 0 || row_map_init() != 0) {
        fclose(f);
        return CSV_FAIL_IO("memory allocation failed%s", "");
    }

    char line[8192];
    char *tokens[4096];

    if (!fgets(line, sizeof(line), f)) {
        fclose(f);
        return CSV_FAIL_IO("file is empty%s", "");
    }
    trim_right(line);

    int ntok = split_csv(line, tokens);
    if (ntok < 2 || strlen(tokens[0]) != 0) {
        fclose(f);
        return CSV_FAIL_FORMAT("bad header: first token must be empty and at least one column required%s", "");
    }

    num_cols = ntok - 1;
    col_names = malloc(num_cols * sizeof(char*));

    if (!col_names) {
        fclose(f);
        return CSV_FAIL_IO("memory allocation failed%s", "");
    }

    for (int i = 0; i < num_cols; i++) {
        col_names[i] = strdup_local(tokens[i + 1]);

        if (!col_names[i]) {
            fclose(f);
            return CSV_FAIL_IO("memory allocation failed%s", "");
        }

        if (col_map_insert(col_names[i], i) != 0) {
            fclose(f);
            return CSV_FAIL_FORMAT("duplicate column name '%s'", col_names[i]);
        }
    }

    rows_capacity = INITIAL_CAPACITY;
    row_ids = malloc(rows_capacity * sizeof(int));
    table = malloc(rows_capacity * num_cols * sizeof(Cell));

    if (!row_ids || !table) {
        fclose(f);
        return CSV_FAIL_IO("memory allocation failed%s", "");
    }

    num_rows = 0;
    while (fgets(line, sizeof(line), f)) {
        trim_right(line);

        if (strlen(line) == 0)
            continue;

        if (num_rows >= rows_capacity) {
            rows_capacity *= 2;
            int *new_rows = realloc(row_ids, rows_capacity * sizeof(int));
            Cell *new_table = realloc(table, rows_capacity * num_cols * sizeof(Cell));

            if (!new_rows || !new_table) {
                fclose(f);
                return CSV_FAIL_IO("realloc failed%s", "");
            }

            row_ids = new_rows;
            table = new_table;
        }

        int ntok2 = split_csv(line, tokens);

        if (ntok2 < 1)
            continue;

        char *end;
        long rid = strtol(tokens[0], &end, 10);
        if (*end != '\0' || rid <= 0) {
            fclose(f);
            return CSV_FAIL_FORMAT("invalid row id '%s'", tokens[0]);
        }

        if (row_map_insert((int)rid, num_rows) != 0) {
            fclose(f);
            return CSV_FAIL_FORMAT("duplicate row id %ld", rid);
        }
        row_ids[num_rows] = (int)rid;

        int data_cols = ntok2 - 1;
        if (data_cols != num_cols) {
            fclose(f);
            return CSV_FAIL_FORMAT("row %ld has %d columns, expected %d", rid, data_cols, num_cols);
        }

        for (int i = 0; i < num_cols; i++) {
            strncpy(CELL(num_rows, i).raw, tokens[i + 1], MAX_CELL_LEN - 1);
            CELL(num_rows, i).raw[MAX_CELL_LEN - 1] = '\0';
            CELL(num_rows, i).computed = 0;
            CELL(num_rows, i).value = 0;
        }
        num_rows++;
    }

    fclose(f);

    if (num_rows == 0) {
        return CSV_FAIL_FORMAT("no data rows%s", "");
    }
    return CSV_OK;
}

CsvError csv_eval_all(void) {
    CsvError first_err = CSV_OK;
    for (int ri = 0; ri < num_rows; ri++) {
        for (int ci = 0; ci < num_cols; ci++) {
            if (CELL(ri, ci).computed == 0) {
                int val;
                CsvError err = eval_cell(ri, ci, &val);
                if (err != CSV_OK && first_err == CSV_OK) {
                    first_err = err;
                }
            }
        }
    }
    return first_err;
}

void csv_print(void) {
    printf(",");

    for (int ci = 0; ci < num_cols; ci++) {
        printf("%s", col_names[ci]);

        if (ci < num_cols - 1)
            printf(",");
    }

    printf("\n");

    for (int ri = 0; ri < num_rows; ri++) {
        printf("%d", row_ids[ri]);

        for (int ci = 0; ci < num_cols; ci++) {
            printf(",%d", CELL(ri, ci).value);
        }

        printf("\n");
    }
}