#define _GNU_SOURCE
#include "env_model.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int env_init_model(struct env *e, size_t cap)
{
    e->buf = calloc(1, cap);
    if (!e->buf) return -1;
    e->cap  = cap;
    e->used = 0;
    return 0;
}

void env_free_model(struct env *e)
{
    free(e->buf);
    e->buf = NULL;
    e->cap = e->used = 0;
}

// Parse null-separated "name=value" pairs into the buffer
int env_parse(struct env *e, const unsigned char *data, size_t len)
{
    size_t plen = 0;
    const unsigned char *p = data;
    const unsigned char *end = data + len;

    while (p < end && *p) {
        size_t l = strnlen((const char *)p, end - p);
        if (plen + l + 1 > e->cap) return -1;
        memcpy(e->buf + plen, p, l);
        plen += l;
        e->buf[plen++] = 0;
        p += l + 1;
    }
    if (plen + 1 > e->cap) return -1;
    e->buf[plen++] = 0;
    e->used = plen;
    return 0;
}

// Find a "name=value" entry and return pointer to the whole string
static const char *find_entry(const struct env *e, const char *name, size_t *out_len)
{
    size_t nlen = strlen(name);
    size_t off = 0;
    while (off < e->used) {
        const char *s = (const char *)e->buf + off;
        size_t l = strlen(s);
        if (!strncmp(s, name, nlen) && s[nlen] == '=') {
            if (out_len) *out_len = l;
            return s;
        }
        off += l + 1;
        if (off < e->used && e->buf[off] == 0) break;
    }
    return NULL;
}

int env_print_all(const struct env *e, FILE *out)
{
    size_t off = 0;
    while (off < e->used) {
        const char *s = (const char *)e->buf + off;
        size_t l = strlen(s);
        if (l == 0) break;
        fprintf(out, "%s\n", s);
        off += l + 1;
    }
    return 0;
}

int env_print_one(const struct env *e, const char *name, int print_name, FILE *out)
{
    size_t l = 0;
    const char *s = find_entry(e, name, &l);
    if (!s) {
        fprintf(stderr, "## Error: \"%s\" not defined\n", name);
        return -1;
    }
    if (print_name) fprintf(out, "%s=", name);
    fprintf(out, "%s\n", s + strlen(name) + 1);
    (void)l;
    return 0;
}

int env_get(const struct env *e, const char *name, const char **val)
{
    size_t l = 0;
    const char *s = find_entry(e, name, &l);
    if (!s) return -1;
    *val = s + strlen(name) + 1;
    return 0;
}

// Remove existing entry with given name
static int rebuild_skip(struct env *e, const char *name)
{
    size_t nlen = strlen(name);
    unsigned char *out = malloc(e->cap);
    if (!out) return -1;
    size_t plen = 0;
    size_t off = 0;
    while (off < e->used) {
        const char *s = (const char *)e->buf + off;
        size_t l = strlen(s);
        if (l == 0) break;
        int hit = (!strncmp(s, name, nlen) && s[nlen] == '=');
        if (!hit) {
            if (plen + l + 1 > e->cap) { free(out); return -1; }
            memcpy(out + plen, s, l);
            plen += l;
            out[plen++] = 0;
        }
        off += l + 1;
    }
    if (plen + 1 > e->cap) { free(out); return -1; }
    out[plen++] = 0;
    memcpy(e->buf, out, plen);
    e->used = plen;
    free(out);
    return 0;
}

// Set variable to joined argv values, or delete if no values
int env_set(struct env *e, const char *name, int argc, char **argv)
{
    if (rebuild_skip(e, name)) return -1;

    if (argc == 0) return 0;

    size_t nlen = strlen(name);
    size_t need = nlen + 1;
    for (int i = 0; i < argc; i++) need += strlen(argv[i]) + 1;
    need += 1;

    if (e->used - 1 + need > e->cap) {
        fprintf(stderr, "Error: environment overflow, \"%s\" deleted\n", name);
        return -1;
    }

    unsigned char *p = e->buf + e->used - 1;
    memcpy(p, name, nlen);
    p += nlen;
    *p++ = '=';
    for (int i = 0; i < argc; i++) {
        size_t l = strlen(argv[i]);
        memcpy(p, argv[i], l);
        p += l;
        if (i + 1 < argc) *p++ = ' ';
    }
    *p++ = 0;
    *p++ = 0;
    e->used = (size_t)(p - e->buf);
    return 0;
}

int env_serialize(const struct env *e, unsigned char *out, size_t outcap, size_t *outlen)
{
    size_t n = e->used;
    if (n > outcap) return -1;
    memcpy(out, e->buf, n);
    *outlen = n;
    return 0;
}
