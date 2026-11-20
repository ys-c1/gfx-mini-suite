#ifndef PLANAR_H
#define PLANAR_H
#include <stddef.h>
#include <stdint.h>

/*
 * 位平面布局 <-> 像素交错布局 原地互转。
 *
 * 像素交错（chunky）：每像素 1 字节，共 w*h 字节，值为颜色索引（<= (1<<bpp)-1）。
 * 位平面（planar）：bpp 个平面，平面 p 位于 buf[p*ps, (p+1)*ps)，ps = ceil(w/8)*h。
 *   像素 (x,y) 的第 p 位 = 平面 p 第 y 行第 x 位，字节内 MSB 优先。
 *   宽度非 8 倍数时每行尾部补位：转换写出时恒为 0，读入时忽略；
 *   因此 planar->chunky->planar 往返对补位为 0 的规范数据逐字节一致，
 *   非零补位会被规范化为 0。
 *
 * 转换为原地完成：buf 同时担任源与目标，调用方需保证 buf_len >=
 * planar_buf_bytes(w,h,bpp) = max(w*h, bpp*ps)。scratch 恰好一个平面
 *（ps 字节）；传 NULL 则内部 malloc/free 一个平面，除此之外无其他堆分配。
 *
 * 错误码（三类，互不相同）：
 *   BP_ERR_PLANES  平面数/几何参数非法：bpp 不在 1..8，或 w/h <= 0
 *   BP_ERR_LENGTH  长度不符：buf_len 不足，或一个平面的暂存分配失败
 *   BP_ERR_RANGE   索引范围越界：像素值 > (1<<bpp)-1
 * 出错时 buf 内容未定义。
 */
#define BP_OK         0
#define BP_ERR_PLANES 1
#define BP_ERR_LENGTH 2
#define BP_ERR_RANGE  3

size_t planar_plane_bytes(int w, int h);        /* 一个平面的字节数 ceil(w/8)*h */
size_t planar_bytes(int w, int h, int bpp);     /* 位平面布局总字节数 bpp*ps */
size_t chunky_bytes(int w, int h);              /* 像素交错布局字节数 w*h */
size_t planar_buf_bytes(int w, int h, int bpp); /* 原地转换所需缓冲 max(两者) */

int chunky_to_planar(uint8_t* buf, size_t buf_len, int w, int h, int bpp, uint8_t* scratch);
int planar_to_chunky(uint8_t* buf, size_t buf_len, int w, int h, int bpp, uint8_t* scratch);

#endif
