#ifndef BITPACK_H
#define BITPACK_H
#include <stddef.h>
#include <stdint.h>
int pack_rows(const uint8_t* idx, int w, int h, int bpp, uint8_t* out, size_t out_cap, size_t* out_len);
int unpack_rows(const uint8_t* packed, size_t packed_len, int w, int h, int bpp, uint8_t* idx);

/* unpack_rows_at 错误码（可区分） */
enum {
    UNPACK_AT_OK = 0,
    UNPACK_AT_ERR_ARG = -1,   /* 空指针或几何参数非法 */
    UNPACK_AT_ERR_START = -2, /* 起始位越界（start_bit >= 总位数） */
    UNPACK_AT_ERR_NEG = -3,   /* 长度为负 */
    UNPACK_AT_ERR_SPAN = -4   /* start_bit + nbits 超出总位数 */
};

/* 按位偏移定位的局部解码：从第 start_bit 位起读 nbits 位，解出完整索引写入 idx。
 * 位流为 MSB 优先、行间无填充，故起始位可落在任意字节/行边界之间。
 * nbits 不是 bpp 的整数倍时，末尾不足一个索引的部分按截断处理；
 * 实际读到的索引数量通过 out_count 报告（nbits < bpp 时为 0，仍返回 OK）。
 * 校验顺序：参数/几何(-1) → 长度为负(-3) → 起始位越界(-2) → 超界(-4)。 */
int unpack_rows_at(const uint8_t* packed, size_t packed_len, int w, int h, int bpp,
                   size_t start_bit, long nbits, uint8_t* idx, size_t* out_count);
size_t packed_size(int w, int h, int bpp);
int count_used_values(const uint8_t* idx, size_t n, int bpp);
#endif
