#include "toml.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

typedef struct toml_kv_s {
    char *key;
    char *val;
    struct toml_kv_s *next;
} toml_kv_t;

struct toml_table_t {
    char *name;
    toml_kv_t *kvs;
    struct toml_table_t *next;
};

static char *ltrim(char *s)
{
    while (*s && isspace((unsigned char)*s))
        s++;
    return s;
}

static char *rtrim(char *s)
{
    char *p = s + strlen(s);
    while (p > s && isspace((unsigned char)*(p - 1)))
        *--p = '\0';
    return s;
}

static char *trim(char *s)
{
    return rtrim(ltrim(s));
}

toml_table_t *toml_parse_file(FILE *fp, char *errbuf, int errbufsz)
{
    (void)errbuf;
    (void)errbufsz;
    toml_table_t *root = calloc(1, sizeof(*root));
    toml_table_t *current = NULL;
    char line[256];

    while (fgets(line, sizeof(line), fp)) {
        char *p = trim(line);
        if (!*p || *p == '#')
            continue;
        if (*p == '[') {
            char *end = strchr(p, ']');
            if (!end)
                continue;
            *end = '\0';
            toml_table_t *tab = calloc(1, sizeof(*tab));
            tab->name = strdup(p + 1);
            tab->next = root->next;
            root->next = tab;
            current = tab;
        } else {
            if (!current)
                continue;
            char *eq = strchr(p, '=');
            if (!eq)
                continue;
            *eq = '\0';
            char *key = trim(p);
            char *val = trim(eq + 1);
            if (*val == '"') {
                val++;
                char *q = strchr(val, '"');
                if (q)
                    *q = '\0';
            }
            toml_kv_t *kv = calloc(1, sizeof(*kv));
            kv->key = strdup(key);
            kv->val = strdup(val);
            kv->next = current->kvs;
            current->kvs = kv;
        }
    }

    return root;
}

const char *toml_table_key(const toml_table_t *tab, int idx)
{
    const toml_table_t *cur = tab->next;
    for (int i = 0; cur && i < idx; i++)
        cur = cur->next;
    return cur ? cur->name : NULL;
}

toml_table_t *toml_table_in(const toml_table_t *tab, const char *key)
{
    toml_table_t *cur;
    for (cur = tab->next; cur; cur = cur->next) {
        if (cur->name && strcmp(cur->name, key) == 0)
            return cur;
    }
    return NULL;
}

toml_datum_t toml_string_in(const toml_table_t *tab, const char *key)
{
    toml_kv_t *kv;
    for (kv = tab->kvs; kv; kv = kv->next) {
        if (kv->key && strcmp(kv->key, key) == 0) {
            toml_datum_t d;
            d.ok = 1;
            d.u.s = strdup(kv->val);
            return d;
        }
    }
    toml_datum_t bad;
    bad.ok = 0;
    bad.u.s = NULL;
    return bad;
}

void toml_free(toml_table_t *tab)
{
    while (tab) {
        toml_table_t *next_tab = tab->next;
        if (tab->name)
            free(tab->name);
        toml_kv_t *kv = tab->kvs;
        while (kv) {
            toml_kv_t *next_kv = kv->next;
            free(kv->key);
            free(kv->val);
            free(kv);
            kv = next_kv;
        }
        free(tab);
        tab = next_tab;
    }
}
