/* Thin CLI around the repository's existing packer (mini-bitplane-pack).
 *
 *   bitpack_cli <w> <h> <bpp> <index-in.raw> <packed-out.bin>
 *
 * Reads w*h bytes of palette indices, packs them with pack_rows(), writes the
 * packed stream and prints "<packed_len> <used_values>" on stdout.  The point
 * of this tool is only to let the Python report exercise the real C packer
 * instead of a reimplementation.
 */
#include "../../mini-bitplane-pack/src/bitpack.h"

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char** argv) {
    if (argc != 6) {
        fprintf(stderr, "usage: %s <w> <h> <bpp> <in.raw> <out.bin>\n", argv[0]);
        return 2;
    }
    int w = atoi(argv[1]);
    int h = atoi(argv[2]);
    int bpp = atoi(argv[3]);
    if (w <= 0 || h <= 0 || bpp <= 0 || bpp > 8) {
        fprintf(stderr, "invalid dimensions or bpp\n");
        return 2;
    }
    size_t n = (size_t)w * (size_t)h;
    uint8_t* idx = (uint8_t*)malloc(n);
    size_t cap = packed_size(w, h, bpp);
    uint8_t* packed = (uint8_t*)malloc(cap ? cap : 1);
    if (!idx || !packed) {
        fprintf(stderr, "out of memory\n");
        return 2;
    }
    FILE* in = fopen(argv[4], "rb");
    if (!in) {
        fprintf(stderr, "cannot open %s\n", argv[4]);
        return 2;
    }
    size_t got = fread(idx, 1, n, in);
    fclose(in);
    if (got != n) {
        fprintf(stderr, "short input: %zu of %zu bytes\n", got, n);
        return 2;
    }
    size_t len = 0;
    if (pack_rows(idx, w, h, bpp, packed, cap, &len) != 0) {
        fprintf(stderr, "pack_rows failed\n");
        return 1;
    }
    int used = count_used_values(idx, n, bpp);
    FILE* out = fopen(argv[5], "wb");
    if (!out) {
        fprintf(stderr, "cannot open %s\n", argv[5]);
        return 2;
    }
    if (fwrite(packed, 1, len, out) != len) {
        fclose(out);
        fprintf(stderr, "short write\n");
        return 2;
    }
    fclose(out);
    printf("%zu %d\n", len, used);
    free(idx);
    free(packed);
    return 0;
}
