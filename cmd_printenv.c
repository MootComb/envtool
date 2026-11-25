#define _GNU_SOURCE
#include "cmd_printenv.h"
#include "env_model.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Implementation of printenv logic
int cmd_printenv(struct backend *b, int argc, char **argv)
{
    int only_val = 0;
    // -n prints only the value, not "name="
    if (argc > 0 && !strcmp(argv[0], "-n")) {
        only_val = 1;
        argc--;
        argv++;
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

    int rc = 0;
    if (argc == 0) {
        env_print_all(&e, stdout);
    } else {
        for (int i = 0; i < argc; i++) {
            if (env_print_one(&e, argv[i], !only_val, stdout)) rc = 1;
        }
    }

    env_free_model(&e);
    free(raw);
    return rc;
}
