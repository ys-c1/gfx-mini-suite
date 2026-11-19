#ifndef LEGACY_REF_H
#define LEGACY_REF_H
#include <stddef.h>
#include <stdint.h>
int ref_pack_rows(const uint8_t* idx, int w, int h, int bpp, uint8_t* out, size_t out_cap, size_t* out_len);
int ref_unpack_rows(const uint8_t* packed, size_t packed_len, int w, int h, int bpp, uint8_t* idx);
size_t ref_packed_size(int w, int h, int bpp);
int ref_count_used_values(const uint8_t* idx, size_t n, int bpp);
#endif
