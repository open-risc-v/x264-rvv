/* x264 SAD/SSD 内核基准（8bit）。用法：kernel_sad <16|8|4> <reps> [v]
 *
 * 两侧都直接调库里的内核符号：off 二进制里链接的是 C，on 二进制里是 RVV
 * （同名符号由 HAVE_RVV 决定…… 为了两侧共用本文件，C 侧用本文件内复刻的
 *  PIXEL_SAD_C 循环做参照与计时，RVV 侧调用 *_rvv 符号）。
 * 这里采用"同一二进制内比对 + 只给 RVV 计时"的做法，比值由两个二进制分别
 * 打印的 ns 得出，避免跨进程噪点干扰正确性判断。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <stddef.h>

typedef unsigned char pixel;
#define FSTRIDE 16

int  x264_pixel_sad_16x16_rvv(pixel*, ptrdiff_t, pixel*, ptrdiff_t);
int  x264_pixel_sad_16x8_rvv (pixel*, ptrdiff_t, pixel*, ptrdiff_t);
int  x264_pixel_sad_8x16_rvv (pixel*, ptrdiff_t, pixel*, ptrdiff_t);
int  x264_pixel_sad_8x8_rvv  (pixel*, ptrdiff_t, pixel*, ptrdiff_t);
int  x264_pixel_sad_8x4_rvv  (pixel*, ptrdiff_t, pixel*, ptrdiff_t);
int  x264_pixel_ssd_16x16_rvv(pixel*, ptrdiff_t, pixel*, ptrdiff_t);
int  x264_pixel_ssd_8x8_rvv  (pixel*, ptrdiff_t, pixel*, ptrdiff_t);

static uint32_t st = 777u;
static int rnd(void) { st = st * 1103515245u + 12345u; return (int)(st >> 8) & 0xff; }

static long long now_us(void)
{
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000000LL + ts.tv_nsec / 1000;
}

/* C 参照：与 pixel.c 的 PIXEL_SAD_C / PIXEL_SSD_C 同式 */
static int sad_c(pixel *a, intptr_t s1, pixel *b, intptr_t s2, int w, int h)
{
    int sum = 0;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) sum += abs(a[x] - b[x]);
        a += s1; b += s2;
    }
    return sum;
}
static int ssd_c(pixel *a, intptr_t s1, pixel *b, intptr_t s2, int w, int h)
{
    int sum = 0;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) { int d = a[x] - b[x]; sum += d * d; }
        a += s1; b += s2;
    }
    return sum;
}

static pixel fenc[48 * FSTRIDE];
static pixel buf[48 * 32];

int main(int argc, char **argv)
{
    int size = argc > 1 ? atoi(argv[1]) : 16;
    int reps = argc > 2 ? atoi(argv[2]) : 200000;
    int verb = argc > 3 ? atoi(argv[3]) : 0;
    int w = size, h = size;

    for (int i = 0; i < 48 * FSTRIDE; i++) fenc[i] = (pixel)rnd();
    for (int i = 0; i < 48 * 32; i++) buf[i] = (pixel)rnd();

    int (*rvv_sad)(pixel*, ptrdiff_t, pixel*, ptrdiff_t);
    int (*rvv_ssd)(pixel*, ptrdiff_t, pixel*, ptrdiff_t);
    switch (size) {
    case 16: rvv_sad = x264_pixel_sad_16x16_rvv; rvv_ssd = x264_pixel_ssd_16x16_rvv; break;
    case 8:  rvv_sad = x264_pixel_sad_8x8_rvv;   rvv_ssd = x264_pixel_ssd_8x8_rvv;   break;
    default: rvv_sad = 0; rvv_ssd = 0; break;
    }
    if (!rvv_sad) { printf("size %d 未接内核\n", size); return 2; }

    int cs = sad_c(fenc, FSTRIDE, buf, 48, w, h);
    int vs = rvv_sad(fenc, FSTRIDE, buf, 48);
    int cd = ssd_c(fenc, FSTRIDE, buf, 48, w, h);
    int vd = rvv_ssd(fenc, FSTRIDE, buf, 48);
    if (cs != vs) { printf("FAIL sad %dx%d c=%d rvv=%d\n", w, h, cs, vs); return 1; }
    if (cd != vd) { printf("FAIL ssd %dx%d c=%d rvv=%d\n", w, h, cd, vd); return 1; }
    if (verb) printf("verify: %dx%d sad/ssd 与 C 一致（sad=%d ssd=%d）\n", w, h, cs, cd);

    long long t0, dt;
    volatile int sink = 0;

    t0 = now_us();
    for (int i = 0; i < reps; i++) sink += sad_c(fenc, FSTRIDE, buf, 48, w, h);
    dt = now_us() - t0;
    printf("op=sad size=%dx%d side=c   ns_per_call=%.1f\n", w, h, (double)dt * 1000.0 / reps);

    t0 = now_us();
    for (int i = 0; i < reps; i++) sink += rvv_sad(fenc, FSTRIDE, buf, 48);
    dt = now_us() - t0;
    printf("op=sad size=%dx%d side=rvv ns_per_call=%.1f\n", w, h, (double)dt * 1000.0 / reps);

    t0 = now_us();
    for (int i = 0; i < reps; i++) sink += ssd_c(fenc, FSTRIDE, buf, 48, w, h);
    dt = now_us() - t0;
    printf("op=ssd size=%dx%d side=c   ns_per_call=%.1f\n", w, h, (double)dt * 1000.0 / reps);

    t0 = now_us();
    for (int i = 0; i < reps; i++) sink += rvv_ssd(fenc, FSTRIDE, buf, 48);
    dt = now_us() - t0;
    printf("op=ssd size=%dx%d side=rvv ns_per_call=%.1f\n", w, h, (double)dt * 1000.0 / reps);
    if (sink == 12345) printf("(sink)\n");
    return 0;
}
