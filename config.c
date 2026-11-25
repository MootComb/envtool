#define _GNU_SOURCE
#include "config.h"
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

// Parse fw_env.config style file: "dev off size [erase] [sectors]"
static int parse_fw_env_config(struct cfg *c, FILE *fp)
{
    char line[512];
    int idx = 0;

    memset(c, 0, sizeof(*c));
    c->kind = BK_NONE;

    while (idx < 2 && fgets(line, sizeof(line), fp)) {
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '#' || *p == '\n' || *p == 0) continue;

        char dev[64];
        unsigned long off = 0, esz = 0, ersz = 0, sect = 0;
        int rc = sscanf(p, "%63s %lx %lx %lx %lx",
                        dev, &off, &esz, &ersz, &sect);
        if (rc < 3) continue;

        if (rc < 4) ersz = esz;
        if (rc < 5) sect = 1;

        struct envdev *d = &c->dev[idx];
        strncpy(d->devname, dev, sizeof(d->devname) - 1);
        d->devoff     = off;
        d->env_size   = esz;
        d->erase_size = ersz;
        d->env_sectors = sect ? sect : 1;
        d->mtd_type   = MTD_ABSENT;
        idx++;
    }

    if (idx == 0) return -1;
    c->n_devs = idx;
    c->have_redundant = (idx > 1);

    // Special case: plain env device named env or /dev/env
    if (!strcmp(c->dev[0].devname, "/dev/env") ||
        !strcmp(c->dev[0].devname, "env")) {
        c->kind = BK_ENV;
        c->raw_off  = c->dev[0].devoff;
        c->raw_size = c->dev[0].env_size;
        c->raw_fill = 0x00;
        c->have_redundant = 0;
        c->n_devs = 1;
    } else {
        c->kind = BK_MTD;
    }
    return 0;
}

// Parse legacy envtool.conf style file (key value pairs)
static int parse_legacy_envtool(struct cfg *c, FILE *fp)
{
    char line[256];

    memset(c, 0, sizeof(*c));
    c->kind      = BK_ENV;
    c->raw_off   = 4;
    c->raw_size  = 0x10000;
    c->raw_fill  = 0x00;
    strcpy(c->dev[0].devname, "/dev/env");
    c->n_devs = 1;

    while (fgets(line, sizeof(line), fp)) {
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '#' || *p == '\n' || *p == 0) continue;

        char key[64], val[192];
        if (sscanf(p, "%63s %191s", key, val) != 2) continue;

        if (!strcmp(key, "device")) {
            strncpy(c->dev[0].devname, val, sizeof(c->dev[0].devname) - 1);
        } else if (!strcmp(key, "size")) {
            c->raw_size = (uint64_t)strtoull(val, NULL, 0);
        } else if (!strcmp(key, "offset")) {
            c->raw_off = (uint64_t)strtoull(val, NULL, 0);
        } else if (!strcmp(key, "fill")) {
            c->raw_fill = (unsigned char)strtoul(val, NULL, 0);
        }
    }
    c->dev[0].devoff    = c->raw_off;
    c->dev[0].env_size  = c->raw_size;
    c->dev[0].erase_size = 0;
    c->dev[0].env_sectors = 1;
    c->dev[0].mtd_type  = MTD_ABSENT;
    return 0;
}

int cfg_load_file(struct cfg *c, const char *path)
{
    FILE *fp = fopen(path, "r");
    if (!fp) return -1;

    int rc;
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;

    // Choose parser based on file name
    if (!strncmp(base, "envtool", 7))
        rc = parse_legacy_envtool(c, fp);
    else
        rc = parse_fw_env_config(c, fp);

    fclose(fp);
    return rc;
}

// Verify CRC at given offset/size on an MTD device
static int try_mtd_crc(const char *dev, uint64_t off, uint64_t size)
{
#ifdef __linux__
    int fd = open(dev, O_RDONLY);
    if (fd < 0) return 0;

    unsigned char hdr[4];
    if (lseek(fd, (off_t)off, SEEK_SET) < 0) { close(fd); return 0; }
    if (read(fd, hdr, sizeof(hdr)) != (ssize_t)sizeof(hdr)) { close(fd); return 0; }

    uint32_t stored = (uint32_t)hdr[0]
                    | ((uint32_t)hdr[1] << 8)
                    | ((uint32_t)hdr[2] << 16)
                    | ((uint32_t)hdr[3] << 24);

    if (size < 5) { close(fd); return 0; }
    unsigned char *buf = malloc(size - 4);
    if (!buf) { close(fd); return 0; }
    if (lseek(fd, (off_t)(off + 4), SEEK_SET) < 0) { free(buf); close(fd); return 0; }
    ssize_t n = read(fd, buf, size - 4);
    close(fd);
    if (n != (ssize_t)(size - 4)) { free(buf); return 0; }

    uint32_t calc = crc32(0, buf, (uint32_t)(size - 4));
    free(buf);
    return calc == stored;
#else
    (void)dev; (void)off; (void)size;
    return 0;
#endif
}

// Query MTD info for a device
static int probe_mtd_info(const char *dev, uint8_t *mtd_type, uint64_t *erasesize)
{
#ifdef __linux__
    int fd = open(dev, O_RDONLY);
    if (fd < 0) return -1;
    struct mtd_info_user mi;
    if (ioctl(fd, MEMGETINFO, &mi) < 0) { close(fd); return -1; }
    close(fd);
    if (mtd_type) *mtd_type = mi.type;
    if (erasesize) *erasesize = mi.erasesize;
    return 0;
#else
    (void)dev; (void)mtd_type; (void)erasesize;
    return -1;
#endif
}

// Try to autodetect a plain env device
static int autodetect_env(struct cfg *c)
{
    if (access("/dev/env", R_OK | W_OK) == 0) {
        memset(c, 0, sizeof(*c));
        c->kind     = BK_ENV;
        c->raw_off  = 4;
        c->raw_size = 0x10000;
        c->raw_fill = 0x00;
        strcpy(c->dev[0].devname, "/dev/env");
        c->dev[0].devoff   = c->raw_off;
        c->dev[0].env_size = c->raw_size;
        c->n_devs = 1;
        return 0;
    }
    return -1;
}

// Try to autodetect an MTD-based environment
static int autodetect_mtd(struct cfg *c)
{
    static const char *cands[] = {
        "/dev/mtd0", "/dev/mtd1", "/dev/mtd2", "/dev/mtd3",
        "/dev/mtd/0", "/dev/mtd/1", "/dev/mtd/2", "/dev/mtd/3",
        NULL
    };
    static const uint64_t offsets[] = { 0x0, 0x4000, 0x20000, 0x3ef200 };
    static const uint64_t sizes[]   = { 0x4000, 0x2000, 0xc00, 0x10000 };

    for (int i = 0; cands[i]; i++) {
        uint8_t t = 0;
        uint64_t ersz = 0;
        if (probe_mtd_info(cands[i], &t, &ersz)) continue;
        if (t != MTD_NORFLASH && t != MTD_NANDFLASH) continue;

        for (size_t s = 0; s < sizeof(sizes)/sizeof(sizes[0]); s++) {
            for (size_t o = 0; o < sizeof(offsets)/sizeof(offsets[0]); o++) {
                if (try_mtd_crc(cands[i], offsets[o], sizes[s])) {
                    memset(c, 0, sizeof(*c));
                    c->kind = BK_MTD;
                    c->n_devs = 1;
                    c->have_redundant = 0;
                    strncpy(c->dev[0].devname, cands[i],
                            sizeof(c->dev[0].devname) - 1);
                    c->dev[0].devoff     = offsets[o];
                    c->dev[0].env_size   = sizes[s];
                    c->dev[0].erase_size = ersz ? ersz : sizes[s];
                    c->dev[0].env_sectors = 1;
                    c->dev[0].mtd_type   = t;
                    return 0;
                }
            }
        }
    }
    return -1;
}

int cfg_autodetect(struct cfg *c)
{
    if (autodetect_env(c) == 0) return 0;
    if (autodetect_mtd(c) == 0) return 0;
    return -1;
}

int cfg_load(struct cfg *c)
{
    static const char *files[] = {
        "fw_env.config", "/etc/fw_env.config", "/etc/fw_env.conf",
        "envtool.conf",  "/etc/envtool.conf",  "/tmp/envtool.conf",
        NULL
    };

    // Environment variable takes priority
    const char *env = getenv("ENVTOOL_CONFIG");
    if (env && cfg_load_file(c, env) == 0) return 0;

    for (int i = 0; files[i]; i++) {
        if (access(files[i], R_OK) == 0 && cfg_load_file(c, files[i]) == 0)
            return 0;
    }

    if (cfg_autodetect(c) == 0) return 0;

    fprintf(stderr, "No configuration found and no environment device detected\n");
    return -1;
}

void cfg_dump(const struct cfg *c, FILE *out)
{
    fprintf(out, "kind=%d n_devs=%d redundant=%d\n",
            (int)c->kind, c->n_devs, c->have_redundant);
    if (c->kind == BK_ENV) {
        fprintf(out, "raw: dev=%s off=%llu size=%llu fill=0x%02x\n",
                c->dev[0].devname,
                (unsigned long long)c->raw_off,
                (unsigned long long)c->raw_size,
                c->raw_fill);
    } else {
        for (int i = 0; i < c->n_devs; i++) {
            fprintf(out, "mtd[%d]: dev=%s off=0x%llx size=0x%llx erase=0x%llx sectors=%llu type=%u\n",
                    i, c->dev[i].devname,
                    (unsigned long long)c->dev[i].devoff,
                    (unsigned long long)c->dev[i].env_size,
                    (unsigned long long)c->dev[i].erase_size,
                    (unsigned long long)c->dev[i].env_sectors,
                    c->dev[i].mtd_type);
        }
    }
}
