#include <stdint.h>
#include <stddef.h>

static uint32_t crc_table[256];
static int crc_table_ready = 0;

// Initialize CRC-32 lookup table
static void crc32_init(void)
{
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int k = 0; k < 8; k++)
            c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        crc_table[i] = c;
    }
    crc_table_ready = 1;
}

uint32_t crc32(uint32_t crc, const void *buf, size_t len)
{
    if (!crc_table_ready) crc32_init();
    const uint8_t *p = buf;
    crc = ~crc;
    while (len--) crc = crc_table[(crc ^ *p++) & 0xFF] ^ (crc >> 8);
    return ~crc;
}
