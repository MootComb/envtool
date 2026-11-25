#define _GNU_SOURCE
#include "cmd_setenv.h"
#include "env_model.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Implementation of setenv logic
int cmd_setenv(struct backend *b, int argc, char **argv)
{
    if (argc < 1) {
        fprintf(stderr, "usage: fw_setenv NAME [VALUE...]\n");
        return 1;
    }

    size_t payload = b->payload_size(b);
    unsigned char *raw = malloc(payload);
    if (!raw) return 1;

    if (b->read(b, raw, payload)) { free(raw); return 1; }

    struct env e;
    if (env_init_model(&e, payload + 2)) { free(raw); return 1; }
    if (env_parse(&e, raw, payload)) {
        fprintf(stderr, "Error: environment not terminated\n");
        env_free_model(&e);
        free(raw);
        return 1;
    }

    int rc = env_set(&e, argv[0], argc - 1, argv + 1);
    if (rc == 0) {
        unsigned char *out = malloc(payload);
        size_t outlen = 0;
        if (!out || env_serialize(&e, out, payload, &outlen)) {
            rc = 1;
        } else {
            rc = b->write(b, out, outlen);
        }
        free(out);
    }

    env_free_model(&e);
    free(raw);
    return rc ? 1 : 0;
}
