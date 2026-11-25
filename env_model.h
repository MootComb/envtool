#ifndef ENV_MODEL_H
#define ENV_MODEL_H

#include <stddef.h>
#include <stdio.h>

// In-memory environment representation
struct env {
    unsigned char *buf;   // raw buffer (key=val\0key=val\0\0)
    size_t cap;           // capacity of buf
    size_t used;          // bytes currently used
};

int  env_init_model(struct env *e, size_t cap);
void env_free_model(struct env *e);

int  env_parse(struct env *e, const unsigned char *data, size_t len);
int  env_print_all(const struct env *e, FILE *out);
int  env_print_one(const struct env *e, const char *name, int print_name, FILE *out);
int  env_get(const struct env *e, const char *name, const char **val);
int  env_set(struct env *e, const char *name, int argc, char **argv);
int  env_serialize(const struct env *e, unsigned char *out, size_t outcap, size_t *outlen);

#endif
