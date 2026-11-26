#define _GNU_SOURCE
#include "backend.h"
#include "crc32.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>

// Little endian 32-bit helpers
static uint32_t get_u32le(const unsigned char *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void put_u32le(unsigned char *p, uint32_t v)
{
    p[0] = (unsigned char)(v & 0xff);
    p[1] = (unsigned char)((v >> 8) & 0xff);
    p[2] = (unsigned char)((v >> 16) & 0xff);
    p[3] = (unsigned char)((v >> 24) & 0xff);
}

// Initialize plain env backend
static int env_init(struct backend *b)
{
    b->fd = -1;
    b->rbuf = NULL;
    b->wbuf = NULL;
    return 0;
}

// Payload size equals raw_size minus 4-byte CRC header
static size_t env_payload_size(const struct backend *b)
{
    size_t s = (size_t)b->cfg->raw_size;
    return s > sizeof(uint32_t) ? s - sizeof(uint32_t) : 0;
}

// Read CRC header, then full payload; verify CRC over full payload
static int env_read(struct backend *b, unsigned char *dst, size_t len)
{
    size_t total = (size_t)b->cfg->raw_size;
    size_t payload = env_payload_size(b);

    if (len != payload) {
        fprintf(stderr, "env: requested %zu != payload %zu\n", len, payload);
        return -1;
    }
    if (total < sizeof(uint32_t)) {
        fprintf(stderr, "env: raw_size too small (%zu)\n", total);
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

    unsigned char hdr[4];
    ssize_t hn = read(fd, hdr, sizeof(hdr));
    if (hn != (ssize_t)sizeof(hdr)) {
        fprintf(stderr, "short read hdr %zd/%zu\n", hn, sizeof(hdr));
        close(fd);
        return -1;
    }

    ssize_t n = read(fd, dst, len);
    close(fd);
    if (n != (ssize_t)len) {
        fprintf(stderr, "short read payload %zd/%zu\n", n, len);
        return -1;
    }

    // CRC over full payload, matching U-Boot behaviour
    uint32_t stored = get_u32le(hdr);
    uint32_t calc   = crc32(0, dst, len);
    if (stored != calc) {
        fprintf(stderr, "Warning: bad CRC on env (stored 0x%08x, calc 0x%08x)\n",
                stored, calc);
    }
    return 0;
}

// Write full payload padded with raw_fill; CRC computed over full payload
static int env_write(struct backend *b, const unsigned char *src, size_t len)
{
    size_t total = (size_t)b->cfg->raw_size;
    size_t payload = env_payload_size(b);

    if (len > payload) {
        fprintf(stderr, "env: payload too big (%zu > %zu)\n", len, payload);
        return -1;
    }
    if (total < sizeof(uint32_t)) {
        fprintf(stderr, "env: raw_size too small (%zu)\n", total);
        return -1;
    }

    unsigned char *buf = malloc(total);
    if (!buf) return -1;

    memset(buf, b->cfg->raw_fill, total);
    memcpy(buf + sizeof(uint32_t), src, len);

    // CRC over full payload (including zero-filled tail), as U-Boot does
    uint32_t crc = crc32(0, buf + sizeof(uint32_t), payload);
    put_u32le(buf, crc);

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
