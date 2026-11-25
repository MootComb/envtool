#define _GNU_SOURCE
#include "backend.h"
#include "crc32.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>

#ifdef __linux__
#include <sys/ioctl.h>
#include <mtd/mtd-user.h>
#endif

#define MAX_SECTORS 4

// Flag values used in redundant env setups
static unsigned char active_flag = 1;
static unsigned char obsolete_flag = 0;

// Check whether a block is bad (NAND only)
static int mtd_bad_block(int fd, uint8_t mtd_type, loff_t *blockstart)
{
#ifdef __linux__
    if (mtd_type == MTD_NANDFLASH) {
        int bb = ioctl(fd, MEMGETBADBLOCK, blockstart);
        if (bb < 0) { perror("MEMGETBADBLOCK"); return bb; }
        return bb;
    }
#else
    (void)fd; (void)mtd_type; (void)blockstart;
#endif
    return 0;
}

// Read a buffer from MTD, skipping bad blocks on NAND
static int mtd_read_buf(const struct envdev *d, int fd, void *buf, size_t count,
                        off_t offset, uint8_t mtd_type)
{
    size_t blocklen = 0;
    size_t processed = 0;
    size_t readlen = count;
    off_t top_of_range;
    off_t block_seek;
    loff_t blockstart;
    int rc;

    blockstart = offset & ~((loff_t)d->erase_size - 1);
    block_seek = offset - blockstart;

    if (mtd_type == MTD_NANDFLASH) {
        blocklen = d->erase_size;
        top_of_range = (off_t)((d->devoff & ~(blocklen - 1))
                       + d->env_sectors * blocklen);
        if (readlen > blocklen - block_seek)
            readlen = blocklen - block_seek;
    } else {
        blocklen = 0;
        top_of_range = offset + count;
    }

    while (processed < count) {
        rc = mtd_bad_block(fd, mtd_type, &blockstart);
        if (rc < 0) return -1;

        if (blockstart + block_seek + readlen > top_of_range) {
            fprintf(stderr, "Too few good blocks within range\n");
            return -1;
        }
        if (rc) {
            blockstart += blocklen;
            continue;
        }

        if (lseek(fd, blockstart + block_seek, SEEK_SET) < 0) {
            perror("lseek");
            return -1;
        }
        rc = read(fd, (unsigned char *)buf + processed, readlen);
        if (rc != (int)readlen) {
            fprintf(stderr, "Read error: %s\n", strerror(errno));
            return -1;
        }
        processed += readlen;
        readlen = (count - processed < blocklen) ? count - processed : blocklen;
        block_seek = 0;
        blockstart += blocklen;
    }
    return (int)processed;
}

// Write a buffer to MTD with erase and bad block handling
static int mtd_write_buf(const struct envdev *d, int fd, void *buf, size_t count,
                         off_t offset, uint8_t mtd_type)
{
    void *data;
    struct erase_info_user erase;
    size_t blocklen = d->erase_size;
    size_t erase_len;
    size_t erasesize;
    size_t processed = 0;
    size_t write_total;
    off_t erase_offset;
    off_t block_seek;
    off_t top_of_range;
    loff_t blockstart;
    int rc;

    top_of_range = (off_t)((d->devoff & ~(blocklen - 1))
                   + d->env_sectors * blocklen);
    erase_offset = offset & ~(off_t)(blocklen - 1);
    erase_len    = top_of_range - erase_offset;
    blockstart   = erase_offset;
    block_seek   = offset - erase_offset;
    write_total  = (block_seek + count + blocklen - 1) & ~(blocklen - 1);

    // If unaligned, read full erase block first, patch, write back
    if (write_total > count) {
        data = malloc(erase_len);
        if (!data) {
            fprintf(stderr, "Cannot malloc %zu bytes\n", erase_len);
            return -1;
        }
        rc = mtd_read_buf(d, fd, data, write_total, erase_offset, mtd_type);
        if (rc != (int)write_total) {
            free(data);
            return -1;
        }
        memcpy((unsigned char *)data + block_seek, buf, count);
    } else {
        data = buf;
    }

    erasesize = (mtd_type == MTD_NANDFLASH) ? blocklen : erase_len;
    erase.length = erasesize;

    while (processed < write_total) {
        rc = mtd_bad_block(fd, mtd_type, &blockstart);
        if (rc < 0) {
            if (write_total > count) free(data);
            return -1;
        }
        if (blockstart + erasesize > top_of_range) {
            fprintf(stderr, "End of range reached\n");
            if (write_total > count) free(data);
            return -1;
        }
        if (rc) {
            blockstart += blocklen;
            continue;
        }

        erase.start = blockstart;
#ifdef __linux__
        ioctl(fd, MEMUNLOCK, &erase);
        rc = ioctl(fd, MEMERASE, &erase);
        if (rc != 0) {
            fprintf(stderr, "MTD erase error: %s\n", strerror(errno));
            if (write_total > count) free(data);
            return -1;
        }
#endif
        if (lseek(fd, blockstart, SEEK_SET) < 0) {
            perror("lseek");
            if (write_total > count) free(data);
            return -1;
        }
        if (write(fd, (unsigned char *)data + processed, erasesize)
            != (ssize_t)erasesize) {
            fprintf(stderr, "Write error: %s\n", strerror(errno));
            if (write_total > count) free(data);
            return -1;
        }
#ifdef __linux__
        ioctl(fd, MEMLOCK, &erase);
#endif
        processed += blocklen;
        block_seek = 0;
        blockstart += blocklen;
    }

    if (write_total > count) free(data);
    return (int)processed;
}

// Mark obsolete flag in NOR redundant layout
static int mtd_flag_obsolete(int fd, off_t offset)
{
    if (lseek(fd, offset, SEEK_SET) < 0) return -1;
    if (write(fd, &obsolete_flag, 1) != 1) {
        perror("write obsolete flag");
        return -1;
    }
    return 0;
}

// Compute payload size (env_size minus CRC, minus flag if redundant)
static size_t mtd_payload_size(const struct backend *b)
{
    size_t s = (size_t)b->cfg->dev[0].env_size;
    if (s < sizeof(uint32_t)) return 0;
    s -= sizeof(uint32_t);
    if (b->cfg->have_redundant) {
        if (s == 0) return 0;
        s -= 1;
    }
    return s;
}

// Little endian 32-bit helpers
static uint32_t get_u32(const unsigned char *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void put_u32(unsigned char *p, uint32_t v)
{
    p[0] = (unsigned char)(v & 0xff);
    p[1] = (unsigned char)((v >> 8) & 0xff);
    p[2] = (unsigned char)((v >> 16) & 0xff);
    p[3] = (unsigned char)((v >> 24) & 0xff);
}

// Pick active copy in redundant setup by CRC and flags
static int mtd_select_active(struct backend *b)
{
#ifdef __linux__
    const struct cfg *c = b->cfg;
    size_t env_sz = (size_t)c->dev[0].env_size;
    unsigned char *img0 = malloc(env_sz);
    unsigned char *img1 = NULL;
    if (!img0) return -1;

    int fd = open(c->dev[0].devname, O_RDONLY);
    if (fd < 0) { free(img0); return -1; }
    if (mtd_read_buf(&c->dev[0], fd, img0, env_sz, c->dev[0].devoff,
                     c->dev[0].mtd_type) != (int)env_sz) {
        free(img0); close(fd); return -1;
    }
    close(fd);

    uint32_t crc0 = get_u32(img0);
    size_t payload = mtd_payload_size(b);
    unsigned char *d0 = img0 + sizeof(uint32_t) + (c->have_redundant ? 1 : 0);
    uint32_t calc0 = crc32(0, d0, (uint32_t)payload);
    int ok0 = (crc0 == calc0);

    if (!c->have_redundant) {
        free(img0);
        b->active_dev = 0;
        if (!ok0)
            fprintf(stderr, "Warning: bad CRC on env\n");
        return 0;
    }

    img1 = malloc(env_sz);
    if (!img1) { free(img0); return -1; }
    fd = open(c->dev[1].devname, O_RDONLY);
    if (fd < 0) { free(img0); free(img1); return -1; }
    if (mtd_read_buf(&c->dev[1], fd, img1, env_sz, c->dev[1].devoff,
                     c->dev[1].mtd_type) != (int)env_sz) {
        free(img0); free(img1); close(fd); return -1;
    }
    close(fd);

    uint32_t crc1 = get_u32(img1);
    unsigned char *d1 = img1 + sizeof(uint32_t) + 1;
    uint32_t calc1 = crc32(0, d1, (uint32_t)payload);
    int ok1 = (crc1 == calc1);

    unsigned char f0 = img0[4];
    unsigned char f1 = img1[4];

    // Prefer the copy with a good CRC
    if (ok0 && !ok1)      b->active_dev = 0;
    else if (!ok0 && ok1) b->active_dev = 1;
    else if (!ok0 && !ok1) {
        fprintf(stderr, "Warning: bad CRC on both env copies\n");
        b->active_dev = 0;
    } else {
        // Both copies valid: decide by flags
        if (c->dev[0].mtd_type == MTD_NORFLASH) {
            if (f0 == active_flag && f1 == obsolete_flag)      b->active_dev = 0;
            else if (f0 == obsolete_flag && f1 == active_flag) b->active_dev = 1;
            else if (f0 == f1)  b->active_dev = 0;
            else if (f0 == 0xff) b->active_dev = 0;
            else if (f1 == 0xff) b->active_dev = 1;
            else b->active_dev = 0;
        } else {
            // NAND: use monotonically increasing counter
            if ((f0 == 255 && f1 == 0) || f1 > f0) b->active_dev = 1;
            else if ((f1 == 255 && f0 == 0) || f0 > f1) b->active_dev = 0;
            else b->active_dev = 0;
        }
    }

    free(img0);
    free(img1);
    return 0;
#else
    b->active_dev = 0;
    return 0;
#endif
}

// Initialize MTD backend: detect type, pick active copy
static int mtd_init(struct backend *b)
{
    b->fd = -1;
#ifdef __linux__
    int fd = open(b->cfg->dev[0].devname, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "Can't open %s: %s\n",
                b->cfg->dev[0].devname, strerror(errno));
        return -1;
    }
    struct mtd_info_user mi;
    if (ioctl(fd, MEMGETINFO, &mi) < 0) {
        perror("MEMGETINFO");
        close(fd);
        return -1;
    }
    close(fd);
    b->mtd_type = mi.type;

    if (mtd_select_active(b)) return -1;
#endif
    return 0;
}

// Read payload from the active MTD copy
static int mtd_read(struct backend *b, unsigned char *dst, size_t len)
{
#ifdef __linux__
    const struct cfg *c = b->cfg;
    int i = b->active_dev;
    size_t payload = mtd_payload_size(b);
    if (len != payload) {
        fprintf(stderr, "mtd: len %zu != payload %zu\n", len, payload);
        return -1;
    }

    size_t env_sz = (size_t)c->dev[i].env_size;
    unsigned char *img = malloc(env_sz);
    if (!img) return -1;

    int fd = open(c->dev[i].devname, O_RDONLY);
    if (fd < 0) { free(img); return -1; }
    int rc = mtd_read_buf(&c->dev[i], fd, img, env_sz,
                          c->dev[i].devoff, c->dev[i].mtd_type);
    close(fd);
    if (rc != (int)env_sz) { free(img); return -1; }

    size_t hdr = sizeof(uint32_t) + (c->have_redundant ? 1 : 0);
    memcpy(dst, img + hdr, payload);
    free(img);
    return 0;
#else
    (void)b; (void)dst; (void)len;
    return -1;
#endif
}

// Write payload into the inactive MTD copy and swap active
static int mtd_write(struct backend *b, const unsigned char *src, size_t len)
{
#ifdef __linux__
    const struct cfg *c = b->cfg;
    size_t payload = mtd_payload_size(b);
    if (len > payload) {
        fprintf(stderr, "mtd: payload too big (%zu > %zu)\n", len, payload);
        return -1;
    }

    int cur = b->active_dev;
    int tgt = c->have_redundant ? !cur : cur;

    size_t env_sz = (size_t)c->dev[tgt].env_size;
    unsigned char *img = calloc(1, env_sz);
    if (!img) return -1;

    size_t hdr = sizeof(uint32_t) + (c->have_redundant ? 1 : 0);
    if (hdr > env_sz) { free(img); return -1; }
    memcpy(img + hdr, src, len);

    // Set flags for NOR or counter for NAND
    if (c->have_redundant) {
        if (c->dev[tgt].mtd_type == MTD_NORFLASH)
            img[4] = active_flag;
        else
            img[4] = 0;
    }

    uint32_t crc = crc32(0, img + hdr, (uint32_t)payload);
    put_u32(img, crc);

    int fd = open(c->dev[tgt].devname, O_RDWR);
    if (fd < 0) { free(img); return -1; }
    int rc = mtd_write_buf(&c->dev[tgt], fd, img, env_sz,
                           c->dev[tgt].devoff, c->dev[tgt].mtd_type);
    if (rc < 0) { close(fd); free(img); return -1; }

    // Mark old copy obsolete (NOR) or increment its counter (NAND)
    if (c->have_redundant && c->dev[cur].mtd_type == MTD_NORFLASH) {
        off_t off = (off_t)c->dev[cur].devoff + (off_t)sizeof(uint32_t);
        int fdc = open(c->dev[cur].devname, O_RDWR);
        if (fdc >= 0) {
            mtd_flag_obsolete(fdc, off);
            close(fdc);
        }
    } else if (c->have_redundant) {
        unsigned char *tmp = malloc(env_sz);
        if (tmp) {
            int fdc = open(c->dev[cur].devname, O_RDWR);
            if (fdc >= 0 &&
                mtd_read_buf(&c->dev[cur], fdc, tmp, env_sz,
                             c->dev[cur].devoff, c->dev[cur].mtd_type) == (int)env_sz) {
                tmp[4]++;
                mtd_write_buf(&c->dev[cur], fdc, tmp, env_sz,
                              c->dev[cur].devoff, c->dev[cur].mtd_type);
            }
            if (fdc >= 0) close(fdc);
            free(tmp);
        }
    }

    close(fd);
    free(img);
    return 0;
#else
    (void)b; (void)src; (void)len;
    return -1;
#endif
}

static void mtd_cleanup(struct backend *b)
{
    if (b->fd >= 0) close(b->fd);
    b->fd = -1;
}

int backend_open_mtd(struct backend *b, const struct cfg *c)
{
    memset(b, 0, sizeof(*b));
    b->name = "mtd";
    b->cfg  = c;
    b->fd   = -1;
    b->init    = mtd_init;
    b->read    = mtd_read;
    b->write   = mtd_write;
    b->payload_size = mtd_payload_size;
    b->cleanup = mtd_cleanup;
    return b->init(b);
}
