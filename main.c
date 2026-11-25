#define _GNU_SOURCE
#include "config.h"
#include "backend.h"
#include "cmd_printenv.h"
#include "cmd_setenv.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Show backend and configuration info
static int cmd_info(struct backend *b, const struct cfg *c,
                    int argc, char **argv)
{
    (void)argc; (void)argv;
    printf("backend: %s\n", b->name);
    cfg_dump(c, stdout);
    return 0;
}

int main(int argc, char **argv)
{
    struct cfg c;
    if (cfg_load(&c)) return 1;

    struct backend b;
    int rc;
    if (c.kind == BK_ENV)
        rc = backend_open_env(&b, &c);
    else if (c.kind == BK_MTD)
        rc = backend_open_mtd(&b, &c);
    else {
        fprintf(stderr, "unknown backend kind\n");
        return 1;
    }
    if (rc) return 1;

    // Dispatch based on program name (like busybox style applets)
    const char *prog = strrchr(argv[0], '/');
    prog = prog ? prog + 1 : argv[0];

    if (!strcmp(prog, "fw_printenv") || !strcmp(prog, "envread"))
        rc = cmd_printenv(&b, argc - 1, argv + 1);
    else if (!strcmp(prog, "fw_setenv") || !strcmp(prog, "envwrite"))
        rc = cmd_setenv(&b, argc - 1, argv + 1);
    else if (!strcmp(prog, "envtool"))
        rc = cmd_info(&b, &c, argc - 1, argv + 1);
    else {
        fprintf(stderr, "usage: %s (as fw_printenv|fw_setenv|envtool)\n", prog);
        rc = 1;
    }

    b.cleanup(&b);
    return rc;
}
