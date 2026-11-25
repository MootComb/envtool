#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

// Backend kinds: none, plain env device, MTD device
enum backend_kind { BK_NONE = 0, BK_ENV, BK_MTD };

// One environment device descriptor
struct envdev {
    char     devname[64];   // device path
    uint64_t devoff;        // offset of env inside device
    uint64_t env_size;      // total env size (including CRC)
    uint64_t erase_size;    // erase block size (0 if unknown)
    uint64_t env_sectors;   // number of sectors used by env
    uint8_t  mtd_type;      // MTD type (MTD_NORFLASH, etc.)
};

// Full parsed configuration
struct cfg {
    enum backend_kind kind;      // backend selection
    int  have_redundant;         // 1 if two redundant copies exist
    int  n_devs;                 // number of devices (1 or 2)
    struct envdev dev[2];        // device descriptors

    uint64_t raw_off;            // offset for plain env backend
    uint64_t raw_size;           // size for plain env backend
    unsigned char raw_fill;      // fill byte for unused area
};

// Load configuration from default locations or autodetect
int  cfg_load(struct cfg *c);
// Load configuration from a specific file
int  cfg_load_file(struct cfg *c, const char *path);
// Autodetect configuration by probing devices
int  cfg_autodetect(struct cfg *c);
// Dump configuration to a stream
void cfg_dump(const struct cfg *c, FILE *out);

#endif
