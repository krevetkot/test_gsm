#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "csvutility.h"
#include "csv_error.h"

#define MAX_COLS     256
#define MAX_ROWS     256
#define MAX_CELL_LEN 256
#define MAX_COL_NAME 64
#define COL_MAP_CAP  512
#define ROW_MAP_CAP  512

typedef struct {
    char key[MAX_COL_NAME];
    int  value;
} ColEntry;

typedef struct {
    int key;
    int value;
} RowEntry;

typedef struct {
    char raw[MAX_CELL_LEN];
    int  value;
    int  computed;
} Cell;

static ColEntry col_map[COL_MAP_CAP];
static RowEntry row_map[ROW_MAP_CAP];
static char     col_names[MAX_COLS][MAX_COL_NAME];
static int      row_ids[MAX_ROWS];
static Cell     table[MAX_ROWS][MAX_COLS];
static int      num_cols = 0;
static int      num_rows = 0;

static unsigned int hash_str(const char *s) {
    unsigned int h = 5381;
    while (*s)
        h = ((h << 5) + h) ^ (unsigned char)*s++;
    return h;
}

static void col_map_init(void) {
    for (int i = 0; i < COL_MAP_CAP; i++)
        col_map[i].value = -1;
}

static int col_map_insert(const char *name, int idx) {
    unsigned int h = hash_str(name) & (COL_MAP_CAP - 1);
    for (int probe = 0; probe < COL_MAP_CAP; probe++) {
        int slot = (h + probe) & (COL_MAP_CAP - 1);
        if (col_map[slot].value == -1) {
            size_t n = strlen(name);
            if (n >= MAX_COL_NAME) n = MAX_COL_NAME - 1;
            memcpy(col_map[slot].key, name, n);
            col_map[slot].key[n] = '\0';
            col_map[slot].value = idx;
            return 0;
        }
        if (strcmp(col_map[slot].key, name) == 0)
            return -1;
    }
    return -1;
}

static int col_map_find(const char *name) {
    unsigned int h = hash_str(name) & (COL_MAP_CAP - 1);
    for (int probe = 0; probe < COL_MAP_CAP; probe++) {
        int slot = (h + probe) & (COL_MAP_CAP - 1);
        if (col_map[slot].value == -1)
            return -1;
        if (strcmp(col_map[slot].key, name) == 0)
            return col_map[slot].value;
    }
    return -1;
}

static unsigned int hash_int(int k) {
    unsigned int x = (unsigned int)k;
    x = ((x >> 16) ^ x) * 0x45d9f3b;
    x = ((x >> 16) ^ x) * 0x45d9f3b;
    x = (x >> 16) ^ x;
    return x;
}

static void row_map_init(void) {
    for (int i = 0; i < ROW_MAP_CAP; i++)
        row_map[i].key = -1;
}

static int row_map_insert(int id, int idx) {
    unsigned int h = hash_int(id) & (ROW_MAP_CAP - 1);
    for (int probe = 0; probe < ROW_MAP_CAP; probe++) {
        int slot = (h + probe) & (ROW_MAP_CAP - 1);
        if (row_map[slot].key == -1) {
            row_map[slot].key   = id;
            row_map[slot].value = idx;
            return 0;
        }
        if (row_map[slot].key == id)
            return -1;
    }
    return -1;
}

static int row_map_find(int id) {
    unsigned int h = hash_int(id) & (ROW_MAP_CAP - 1);
    for (int probe = 0; probe < ROW_MAP_CAP; probe++) {
        int slot = (h + probe) & (ROW_MAP_CAP - 1);
        if (row_map[slot].key == -1)
            return -1;
        if (row_map[slot].key == id)
            return row_map[slot].value;
    }
    return -1;
}

static void trim_right(char *s) {
    int n = (int)strlen(s);
    while (n > 0 && (s[n-1] == '\r' || s[n-1] == '\n' || s[n-1] == ' '))
        s[--n] = '\0';
}

static int split_csv(char *buf, char *out[]) {
    int count = 0;
    char *p = buf;
    while (count < MAX_COLS + 1) {
        out[count++] = p;
        char *comma = strchr(p, ',');
        if (!comma) break;
        *comma = '\0';
        p = comma + 1;
    }
    return count;
}

static CsvError eval_cell(int ri, int ci, int *out_val);

static CsvError parse_ref(const char *ref, int *row_out, int *col_out) {
    int len = (int)strlen(ref);
    int i = len - 1;
    while (i >= 0 && isdigit((unsigned char)ref[i]))
        i--;

    if (i < 0 || i == len - 1)
        return CSV_ERR_EVAL;

    char col_name[MAX_COL_NAME];
    if (i + 1 >= MAX_COL_NAME)
        return CSV_ERR_EVAL;
    strncpy(col_name, ref, i + 1);
    col_name[i + 1] = '\0';

    int row_id = atoi(ref + i + 1);
    if (row_id <= 0)
        return CSV_ERR_EVAL;

    int ci = col_map_find(col_name);
    if (ci < 0) return CSV_ERR_EVAL;

    int ri = row_map_find(row_id);
    if (ri < 0) return CSV_ERR_EVAL;

    *col_out = ci;
    *row_out = ri;
    return CSV_OK;
}

static CsvError parse_arg(const char *arg, int *val) {
    const char *p = arg;
    if (*p == '-') p++;
    int all_digits = (*p != '\0');
    while (*p) {
        if (!isdigit((unsigned char)*p)) { all_digits = 0; break; }
        p++;
    }
    if (all_digits) {
        *val = atoi(arg);
        return CSV_OK;
    }
    int r, c;
    CsvError err = parse_ref(arg, &r, &c);
    if (err != CSV_OK) return err;
    return eval_cell(r, c, val);
}

static CsvError eval_cell(int ri, int ci, int *out_val) {
    Cell *cell = &table[ri][ci];

    if (cell->computed == 1)  { *out_val = cell->value; return CSV_OK; }
    if (cell->computed == -1)
        return CSV_FAIL_EVAL("circular reference at %s%d", col_names[ci], row_ids[ri]);

    if (cell->raw[0] != '=') {
        char *end;
        long v = strtol(cell->raw, &end, 10);
        if (*end != '\0')
            return CSV_FAIL_EVAL("invalid cell value '%s' at %s%d",
                                 cell->raw, col_names[ci], row_ids[ri]);
        cell->value    = (int)v;
        cell->computed = 1;
        *out_val = cell->value;
        return CSV_OK;
    }

    cell->computed = -1;

    const char *expr = cell->raw + 1;
    char arg1[MAX_CELL_LEN], arg2[MAX_CELL_LEN];
    char op     = 0;
    int  op_pos = -1;

    int start = (expr[0] == '-') ? 1 : 0;
    for (int k = start; expr[k] != '\0'; k++) {
        char ch = expr[k];
        if (ch == '+' || ch == '-' || ch == '*' || ch == '/') {
            op = ch; op_pos = k; break;
        }
    }

    if (op_pos < 0) {
        cell->computed = 0;
        return CSV_FAIL_EVAL("no operator in formula '%s' at %s%d",
                             cell->raw, col_names[ci], row_ids[ri]);
    }
    if (op_pos == 0 || expr[op_pos + 1] == '\0') {
        cell->computed = 0;
        return CSV_FAIL_EVAL("malformed formula '%s' at %s%d",
                             cell->raw, col_names[ci], row_ids[ri]);
    }

    strncpy(arg1, expr, op_pos);
    arg1[op_pos] = '\0';
    strcpy(arg2, expr + op_pos + 1);

    int v1, v2;
    CsvError err;

    err = parse_arg(arg1, &v1);
    if (err != CSV_OK) {
        cell->computed = 0;
        return CSV_FAIL_EVAL("cannot resolve '%s' in '%s' at %s%d",
                             arg1, cell->raw, col_names[ci], row_ids[ri]);
    }

    err = parse_arg(arg2, &v2);
    if (err != CSV_OK) {
        cell->computed = 0;
        return CSV_FAIL_EVAL("cannot resolve '%s' in '%s' at %s%d",
                             arg2, cell->raw, col_names[ci], row_ids[ri]);
    }

    int result;
    switch (op) {
        case '+': result = v1 + v2; break;
        case '-': result = v1 - v2; break;
        case '*': result = v1 * v2; break;
        case '/':
            if (v2 == 0) {
                cell->computed = 0;
                return CSV_FAIL_EVAL("division by zero in '%s' at %s%d",
                                     cell->raw, col_names[ci], row_ids[ri]);
            }
            result = v1 / v2;
            break;
        default:
            cell->computed = 0;
            return CSV_FAIL_EVAL("unknown operator '%c' at %s%d",
                                 op, col_names[ci], row_ids[ri]);
    }

    cell->value    = result;
    cell->computed = 1;
    *out_val = result;
    return CSV_OK;
}

CsvError csv_load(const char *filename) {
    FILE *f = fopen(filename, "r");
    if (!f)
        return CSV_FAIL_IO("cannot open '%s'", filename);

    col_map_init();
    row_map_init();

    char  line[MAX_COLS * MAX_CELL_LEN];
    char *tokens[MAX_COLS + 1];

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
    for (int i = 0; i < num_cols; i++) {
        const char *name = tokens[i + 1];
        if (strlen(name) == 0) {
            fclose(f);
            return CSV_FAIL_FORMAT("empty column name at position %d", i + 1);
        }
        strncpy(col_names[i], name, MAX_COL_NAME - 1);
        col_names[i][MAX_COL_NAME - 1] = '\0';
        if (col_map_insert(col_names[i], i) != 0) {
            fclose(f);
            return CSV_FAIL_FORMAT("duplicate column name '%s'", col_names[i]);
        }
    }

    num_rows = 0;
    while (fgets(line, sizeof(line), f)) {
        trim_right(line);
        if (strlen(line) == 0) continue;

        if (num_rows >= MAX_ROWS) {
            fclose(f);
            return CSV_FAIL_FORMAT("too many rows, max is %d", MAX_ROWS);
        }

        int ntok2 = split_csv(line, tokens);
        if (ntok2 < 1) continue;

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
            return CSV_FAIL_FORMAT("row %ld has %d columns, expected %d",
                                   rid, data_cols, num_cols);
        }

        for (int i = 0; i < num_cols; i++) {
            strncpy(table[num_rows][i].raw, tokens[i + 1], MAX_CELL_LEN - 1);
            table[num_rows][i].raw[MAX_CELL_LEN - 1] = '\0';
            table[num_rows][i].computed = 0;
            table[num_rows][i].value    = 0;
        }
        num_rows++;
    }

    fclose(f);

    if (num_rows == 0)
        return CSV_FAIL_FORMAT("no data rows%s", "");

    return CSV_OK;
}

CsvError csv_eval_all(void) {
    CsvError first_err = CSV_OK;
    for (int ri = 0; ri < num_rows; ri++) {
        for (int ci = 0; ci < num_cols; ci++) {
            if (table[ri][ci].computed == 0) {
                int val;
                CsvError err = eval_cell(ri, ci, &val);
                if (err != CSV_OK && first_err == CSV_OK)
                    first_err = err;
            }
        }
    }
    return first_err;
}

void csv_print(void) {
    printf(",");
    for (int ci = 0; ci < num_cols; ci++) {
        printf("%s", col_names[ci]);
        if (ci < num_cols - 1) printf(",");
    }
    printf("\n");

    for (int ri = 0; ri < num_rows; ri++) {
        printf("%d", row_ids[ri]);
        for (int ci = 0; ci < num_cols; ci++)
            printf(",%d", table[ri][ci].value);
        printf("\n");
    }
}