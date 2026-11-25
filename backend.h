#ifndef BACKEND_H
#define BACKEND_H

#include <stddef.h>
#include "config.h"

// Common backend interface
struct backend {
    const char *name;        // "env" or "mtd"
    const struct cfg *cfg;   // pointer to configuration

    int  (*init)(struct backend *b);          // initialize backend
    int  (*read)(struct backend *b, unsigned char *dst, size_t len);   // read payload
    int  (*write)(struct backend *b, const unsigned char *src, size_t len); // write payload
    size_t (*payload_size)(const struct backend *b);  // current payload capacity
    void (*cleanup)(struct backend *b);       // release resources

    int fd;              // cached file descriptor
    int mtd_type;        // detected MTD type
    int active_dev;      // active device index (0 or 1)
    unsigned char *rbuf; // reserved read buffer
    unsigned char *wbuf; // reserved write buffer
};

// Open backend for plain env device
int backend_open_env(struct backend *b, const struct cfg *c);
// Open backend for MTD device
int backend_open_mtd(struct backend *b, const struct cfg *c);

#endif
