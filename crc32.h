#ifndef CRC32_H
#define CRC32_H

#include <stdint.h>
#include <stddef.h>

// Standard CRC-32 computation
uint32_t crc32(uint32_t crc, const void *buf, size_t len);

#endif
