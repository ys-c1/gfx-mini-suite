#ifndef BITPACK_H
#define BITPACK_H
#include <stddef.h>
#include <stdint.h>

/* 整幅入口（保留） */
int pack_rows(const uint8_t* idx, int w, int h, int bpp, uint8_t* out, size_t out_cap, size_t* out_len);
int unpack_rows(const uint8_t* packed, size_t packed_len, int w, int h, int bpp, uint8_t* idx);
size_t packed_size(int w, int h, int bpp);
int count_used_values(const uint8_t* idx, size_t n, int bpp);

/* 逐行流式入口：峰值内存 = 目标缓冲 + 一行索引，不需要整幅索引图。
 * 用法：pack_begin -> 按行号 0..h-1 顺序 pack_submit_row -> pack_finish。
 * 输出位布局与 pack_rows 完全一致（最后一个字节也相同）。 */
typedef struct {
    int w, h, bpp;
    int next_row;      /* 下一个期望提交的行号 */
    uint8_t* out;
    size_t out_len;    /* packed_size(w, h, bpp) */
    size_t row_bits;   /* w * bpp */
    uint8_t vmax;
} bitpack_stream;

#define BITPACK_OK              0
#define BITPACK_ERR_ARG        (-1) /* 参数/缓冲容量非法 */
#define BITPACK_ERR_ROW_RANGE  (-2) /* 行号越界 [0, h) */
#define BITPACK_ERR_ROW_DUP    (-3) /* 重复提交已写入的行 */
#define BITPACK_ERR_ROW_ORDER  (-4) /* 乱序：跳过了尚未提交的行 */
#define BITPACK_ERR_VALUE      (-5) /* 像素值 >= 2^bpp */
#define BITPACK_ERR_INCOMPLETE (-6) /* finish 时仍有行未提交 */

int pack_begin(bitpack_stream* s, int w, int h, int bpp, uint8_t* out, size_t out_cap);
int pack_submit_row(bitpack_stream* s, int y, const uint8_t* row);
int pack_finish(bitpack_stream* s, size_t* out_len);

#endif
