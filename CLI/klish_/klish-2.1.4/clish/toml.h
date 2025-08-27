#ifndef SIMPLE_TOML_H
#define SIMPLE_TOML_H

#include <stdio.h>

typedef struct toml_table_t toml_table_t;

typedef struct {
    int ok;
    union {
        char *s;
    } u;
} toml_datum_t;

toml_table_t *toml_parse_file(FILE *fp, char *errbuf, int errbufsz);
const char *toml_table_key(const toml_table_t *tab, int idx);
toml_table_t *toml_table_in(const toml_table_t *tab, const char *key);
toml_datum_t toml_string_in(const toml_table_t *tab, const char *key);
void toml_free(toml_table_t *tab);

#endif
