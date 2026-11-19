#ifndef BITPACK_H
#define BITPACK_H

#include <stddef.h>
#include <stdint.h>

/* ------------------------------------------------------------------ *
 * Legacy row API (kept byte-for-byte compatible).
 *
 * Stream model: rows are concatenated without per-row padding.  Index
 * (y, x) occupies stream bits [(y*w + x)*bpp, (y*w + x + 1)*bpp).
 * Bits are emitted most-significant-bit first and fill each byte from
 * bit 7 down to bit 0 (the "MSB-first" order below).
 *
 * On success these functions return 0.  On any error they return -1;
 * use the extended API for distinguishishable error codes.
 * ------------------------------------------------------------------ */
int pack_rows(const uint8_t* idx, int w, int h, int bpp,
              uint8_t* out, size_t out_cap, size_t* out_len);
int unpack_rows(const uint8_t* packed, size_t packed_len, int w, int h, int bpp,
                uint8_t* idx);
size_t packed_size(int w, int h, int bpp);
int count_used_values(const uint8_t* idx, size_t n, int bpp);

/* ------------------------------------------------------------------ *
 * Generalized stream API.
 * ------------------------------------------------------------------ */

/* Order in which consecutive stream bits fill one byte.
 *
 *   BITPACK_MSB_FIRST: stream bit 0 -> byte 0 bit 7, then bit 6 ... bit 0.
 *   BITPACK_LSB_FIRST: stream bit 0 -> byte 0 bit 0, then bit 1 ... bit 7.
 *
 * In BOTH orders an index value is emitted most-significant-bit first
 * into consecutive stream positions.  Therefore the LSB-first byte
 * stream is exactly the per-byte bit reversal ("mirror") of the
 * MSB-first byte stream for the same indices and layout; see
 * bitpack_mirror_byte().
 */
typedef enum {
    BITPACK_MSB_FIRST = 0,
    BITPACK_LSB_FIRST = 1
} bitpack_order;

/* Error codes.  All failures are negative and mutually distinct. */
enum bitpack_error {
    BITPACK_OK         = 0,
    BITPACK_ERR_ARG    = -1,  /* NULL pointer / invalid argument        */
    BITPACK_ERR_BPP    = -2,  /* bit depth outside 1..8                  */
    BITPACK_ERR_CAP    = -3,  /* output / index buffer too small         */
    BITPACK_ERR_RANGE  = -4,  /* index value > (1<<bpp)-1                */
    BITPACK_ERR_LEN    = -5,  /* packed input shorter than requested     */
    BITPACK_ERR_OFFSET = -6,  /* bit offset / bit count overflow         */
    BITPACK_ERR_ORDER  = -7   /* unknown bitpack_order                   */
};

/* Upper bound promised for internal scratch memory.  The implementation
 * performs no heap allocation at all, so actual scratch is 0 bytes. */
#define BITPACK_BLOCK_BYTES 4096u

/* A write segment: count indices from idx are placed at absolute stream
 * bit offset bit_off.  Segments may overlap; later segments win.  Bits
 * outside all segments are left untouched. */
typedef struct {
    uint64_t bit_off;
    const uint8_t* idx;
    size_t count;
} bitpack_write_seg;

/* A read segment: count indices are decoded from absolute stream bit
 * offset bit_off into idx. */
typedef struct {
    uint64_t bit_off;
    uint8_t* idx;
    size_t count;
} bitpack_read_seg;

/* Write one or more segments into out.
 *
 * All segments share bpp and order.  On success *out_bits is the stream
 * length: the largest bit_off + count*bpp among non-empty segments (0
 * when there are no bits).  Zero-length segments are ignored and do not
 * extend the stream.
 *
 * Validation is completed before any bit is modified: when an error is
 * returned, out is unchanged.  Bits inside a segment are assigned (0 or
 * 1); bits outside all segments keep their previous value.  For a fresh
 * stream, zero out[0 .. ceil(*out_bits/8)) first.
 */
int bitpack_write(const bitpack_write_seg* segs, size_t nsegs,
                  int bpp, bitpack_order order,
                  uint8_t* out, size_t out_cap, uint64_t* out_bits);

/* Convenience wrapper for a single segment. */
int bitpack_write_run(const uint8_t* idx, size_t count,
                      int bpp, bitpack_order order, uint64_t bit_off,
                      uint8_t* out, size_t out_cap, uint64_t* out_bits);

/* Decode one or more segments.  packed_len is the byte length of the
 * input; every requested bit range must lie inside packed_len*8 bits.
 * A single bitpack_read_run() of count indices may cross any number of
 * previously written segment boundaries.
 */
int bitpack_read(const uint8_t* packed, size_t packed_len,
                 const bitpack_read_seg* segs, size_t nsegs,
                 int bpp, bitpack_order order);

/* Convenience wrapper for a single read segment. */
int bitpack_read_run(const uint8_t* packed, size_t packed_len,
                     uint64_t bit_off, size_t count,
                     int bpp, bitpack_order order, uint8_t* idx);

/* Decode a whole stream whose exact valid bit length is avail_bits.
 *
 * Only complete indices are returned:
 *     count = avail_bits / bpp
 *     tail  = avail_bits % bpp    (0 .. bpp-1)
 * A trailing partial index (tail != 0) is not decoded, is not an error,
 * and is reported through out_tail_bits (may be NULL).
 *
 * packed_len must be at least ceil(avail_bits/8).  idx_cap is the number
 * of indices idx can hold.
 */
int bitpack_read_all(const uint8_t* packed, size_t packed_len,
                     uint64_t avail_bits, int bpp, bitpack_order order,
                     uint8_t* idx, size_t idx_cap,
                     size_t* out_count, unsigned* out_tail_bits);

/* Reverse the bits of one byte: 0babcdefgh -> 0b hgfedcba. */
uint8_t bitpack_mirror_byte(uint8_t b);

/* Per-byte bit reversal of n bytes.  in == out (in-place) is allowed. */
int bitpack_mirror_bytes(const uint8_t* in, size_t n, uint8_t* out);

#endif
