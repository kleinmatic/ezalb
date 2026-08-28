/* roms.c — the built-in firmware images: gzip blobs linked in by roms.S,
 * inflated on demand. */
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <zlib.h>

#include "host/host.h"

#define ROM(sym) extern const uint8_t sym[], sym##_end[];
ROM(rom_vt420) ROM(rom_vt420_14) ROM(rom_vt420_b14)
ROM(rom_vt510) ROM(rom_vt520) ROM(rom_vt525)
#undef ROM
#define ROM(sym) sym, sym##_end

const builtin_rom builtin_roms[] = {
    { "vt420",      "23-068E9-00", "VT420 V1.3", ROM(rom_vt420) },
    { "vt420-1.4",  "23-208E9-00", "VT420 V1.4", ROM(rom_vt420_14) },
    { "vt420-b1.4", "2E-C394A-01", "VT420 B1.4", ROM(rom_vt420_b14) },
    { "vt510",      "23-032ED-00", "VT510",      ROM(rom_vt510) },
    { "vt520",      "23-010ED-00", "VT520 V2.10", ROM(rom_vt520) },
    { "vt525",      "23-011ED-00", "VT525 V2.10", ROM(rom_vt525) },
};
const size_t builtin_roms_count = sizeof builtin_roms / sizeof builtin_roms[0];

const builtin_rom *builtin_rom_find(const char *name)
{
    for (size_t i = 0; i < builtin_roms_count; i++)
        if (strcasecmp(builtin_roms[i].name, name) == 0 ||
            strcasecmp(builtin_roms[i].part, name) == 0)
            return &builtin_roms[i];
    return NULL;
}

uint8_t *builtin_rom_load(const builtin_rom *r, size_t *out_len)
{
    size_t zlen = (size_t)(r->z_end - r->z);
    const uint8_t *isize = r->z_end - 4; /* gzip trailer: uncompressed size */
    uLongf len = (uLongf)isize[0] | (uLongf)isize[1] << 8 |
                 (uLongf)isize[2] << 16 | (uLongf)isize[3] << 24;
    uint8_t *buf = malloc(len ? len : 1);
    z_stream zs = { 0 };

    if (!buf)
        return NULL;
    zs.next_in = (Bytef *)r->z;
    zs.avail_in = (uInt)zlen;
    zs.next_out = buf;
    zs.avail_out = (uInt)len;
    if (inflateInit2(&zs, 16 + MAX_WBITS) != Z_OK) {
        free(buf);
        return NULL;
    }
    int rc = inflate(&zs, Z_FINISH);
    inflateEnd(&zs);
    if (rc != Z_STREAM_END || zs.total_out != len) {
        free(buf);
        return NULL;
    }
    *out_len = len;
    return buf;
}
