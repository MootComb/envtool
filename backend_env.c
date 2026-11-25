#define _GNU_SOURCE
#include "backend.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>

// Initialize plain env backend
static int env_init(struct backend *b)
{
    b->fd = -1;
    b->rbuf = NULL;
    b->wbuf = NULL;
    return 0;
}

// Payload size equals full raw size for plain env device
static size_t env_payload_size(const struct backend *b)
{
    return (size_t)b->cfg->raw_size;
}

// Read raw payload from device at configured offset
static int env_read(struct backend *b, unsigned char *dst, size_t len)
{
    if (len > b->cfg->raw_size) {
        fprintf(stderr, "env: requested %zu > raw_size %llu\n",
                len, (unsigned long long)b->cfg->raw_size);
        return -1;
    }

    int fd = open(b->cfg->dev[0].devname, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "open %s: %s\n", b->cfg->dev[0].devname, strerror(errno));
        return -1;
    }
    if (lseek(fd, (off_t)b->cfg->raw_off, SEEK_SET) < 0) {
        perror("lseek");
        close(fd);
        return -1;
    }
    ssize_t n = read(fd, dst, len);
    close(fd);
    if (n != (ssize_t)len) {
        fprintf(stderr, "short read %zd/%zu\n", n, len);
        return -1;
    }
    return 0;
}

// Write payload with fill byte padding to full raw size
static int env_write(struct backend *b, const unsigned char *src, size_t len)
{
    if (len > b->cfg->raw_size) {
        fprintf(stderr, "env: payload too big (%zu > %llu)\n",
                len, (unsigned long long)b->cfg->raw_size);
        return -1;
    }

    size_t total = (size_t)b->cfg->raw_size;
    unsigned char *buf = malloc(total);
    if (!buf) return -1;
    memset(buf, b->cfg->raw_fill, total);
    memcpy(buf, src, len);

    int fd = open(b->cfg->dev[0].devname, O_WRONLY);
    if (fd < 0) {
        fprintf(stderr, "open %s: %s\n", b->cfg->dev[0].devname, strerror(errno));
        free(buf);
        return -1;
    }
    if (lseek(fd, (off_t)b->cfg->raw_off, SEEK_SET) < 0) {
        perror("lseek");
        free(buf);
        close(fd);
        return -1;
    }
    ssize_t n = write(fd, buf, total);
    close(fd);
    free(buf);
    if (n != (ssize_t)total) {
        fprintf(stderr, "short write %zd/%zu\n", n, total);
        return -1;
    }
    return 0;
}

// Cleanup plain env backend
static void env_cleanup(struct backend *b)
{
    if (b->fd >= 0) close(b->fd);
    free(b->rbuf);
    free(b->wbuf);
    b->rbuf = b->wbuf = NULL;
}

int backend_open_env(struct backend *b, const struct cfg *c)
{
    memset(b, 0, sizeof(*b));
    b->name = "env";
    b->cfg  = c;
    b->fd   = -1;
    b->init    = env_init;
    b->read    = env_read;
    b->write   = env_write;
    b->payload_size = env_payload_size;
    b->cleanup = env_cleanup;
    return b->init(b);
}
