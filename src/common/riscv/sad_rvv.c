/* x264 SAD / SSD 的 RVV 内核（8bit）。
 *
 * 设计沿用 libvpx / openh264 已验过的两处修正：
 *  1) 归约只在整块末尾做一次（u8 差用 vwaddu.wv 累进 u16，末尾 vwredsumu），
 *     不要每行一次归约 —— 那是 v1 的坑，行归约开销把 SIMD 收益吃光。
 *  2) 不假设 VLEN/MVL：vl 直接取块宽（4/8/16），宽度 ≤16 时全部落在 u8m1 的
 *     前 16 个 lane，VLEN=128 与 VLEN=256 都正确。
 *
 * 规则来源：x264 里 SAD 走 pixf->sad[] / sad_x3[] / sad_x4[]，但这三张表在
 * 纯 C 构建下都由 PIXEL_SAD_C 静态函数填充，所以只要覆盖指针即可，不改 Makefile。
 */
#include <riscv_vector.h>

static int sad_u8_rvv( const uint8_t *pix1, ptrdiff_t s1,
                       const uint8_t *pix2, ptrdiff_t s2, int w, int h )
{
    const size_t vl = (size_t) w;
    vuint16m2_t vAcc = __riscv_vmv_v_x_u16m2( 0, vl );

    for( int y = 0; y < h; y++ )
    {
        vuint8m1_t v1 = __riscv_vle8_v_u8m1( pix1 + y*s1, vl );
        vuint8m1_t v2 = __riscv_vle8_v_u8m1( pix2 + y*s2, vl );
        /* GCC 13/14 没有 vabd intrinsic，用 max/min/sub 组合（与 libvpx 同法）。 */
        vuint8m1_t hi = __riscv_vmaxu_vv_u8m1( v1, v2, vl );
        vuint8m1_t lo = __riscv_vminu_vv_u8m1( v1, v2, vl );
        vuint8m1_t vd = __riscv_vsub_vv_u8m1( hi, lo, vl );
        vAcc = __riscv_vwaddu_wv_u16m2( vAcc, vd, vl );
    }

    /* 命名是 vwredsumu_vs_<源类型>_<累加器类型>（累加器 SEW 是源的两倍）。 */
    vuint32m1_t vZero = __riscv_vmv_v_x_u32m1( 0, vl );
    vuint32m1_t vSum  = __riscv_vwredsumu_vs_u16m2_u32m1( vAcc, vZero, vl );
    return (int) __riscv_vmv_x_s_u32m1_u32( vSum );
}

static int ssd_u8_rvv( const uint8_t *pix1, ptrdiff_t s1,
                       const uint8_t *pix2, ptrdiff_t s2, int w, int h )
{
    const size_t vl = (size_t) w;
    vuint32m4_t vAcc = __riscv_vmv_v_x_u32m4( 0, vl );

    for( int y = 0; y < h; y++ )
    {
        vuint8m1_t v1 = __riscv_vle8_v_u8m1( pix1 + y*s1, vl );
        vuint8m1_t v2 = __riscv_vle8_v_u8m1( pix2 + y*s2, vl );
        vuint8m1_t hi = __riscv_vmaxu_vv_u8m1( v1, v2, vl );
        vuint8m1_t lo = __riscv_vminu_vv_u8m1( v1, v2, vl );
        vuint8m1_t vd = __riscv_vsub_vv_u8m1( hi, lo, vl );
        /* |d|^2 == d^2，所以先取绝对差再平方，避免有符号乘法要换类型。
           vwmulu 只接无符号操作数（两个 u16m2）。 */
        vuint16m2_t vd16 = __riscv_vzext_vf2_u16m2( vd, vl );
        vuint32m4_t vSq  = __riscv_vwmulu_vv_u32m4( vd16, vd16, vl );
        vAcc = __riscv_vadd_vv_u32m4( vAcc, vSq, vl );
    }

    vuint64m1_t vZero = __riscv_vmv_v_x_u64m1( 0, vl );
    vuint64m1_t vSum  = __riscv_vwredsumu_vs_u32m4_u64m1( vAcc, vZero, vl );
    return (int) __riscv_vmv_x_s_u64m1_u64( vSum );
}

#define SAD_WRAP( size, w, h )                                                    \
int x264_pixel_sad_##size##_rvv( pixel *pix1, intptr_t i_stride_pix1,             \
                                 pixel *pix2, intptr_t i_stride_pix2 )            \
{                                                                                 \
    return sad_u8_rvv( pix1, i_stride_pix1, pix2, i_stride_pix2, w, h );          \
}                                                                                 \
int x264_pixel_ssd_##size##_rvv( pixel *pix1, intptr_t i_stride_pix1,             \
                                 pixel *pix2, intptr_t i_stride_pix2 )            \
{                                                                                 \
    return ssd_u8_rvv( pix1, i_stride_pix1, pix2, i_stride_pix2, w, h );          \
}

SAD_WRAP( 16x16, 16, 16 )
SAD_WRAP( 16x8,  16,  8 )
SAD_WRAP( 8x16,   8, 16 )
SAD_WRAP( 8x8,    8,  8 )
SAD_WRAP( 8x4,    8,  4 )
SAD_WRAP( 4x8,    4,  8 )
SAD_WRAP( 4x4,    4,  4 )

/* sad_x3 / sad_x4：x264 的 C 版在 SAD_X 宏里直接调静态 C 函数，不走指针，
 * 所以这两张表要单独给一份包装，内部仍走通用内核。 */
#define SAD_X_WRAP( size, w, h )                                                  \
void x264_pixel_sad_x3_##size##_rvv( pixel *fenc, pixel *pix0, pixel *pix1,       \
                                     pixel *pix2, intptr_t stride, int scores[3] )\
{                                                                                 \
    scores[0] = sad_u8_rvv( fenc, FENC_STRIDE, pix0, stride, w, h );              \
    scores[1] = sad_u8_rvv( fenc, FENC_STRIDE, pix1, stride, w, h );              \
    scores[2] = sad_u8_rvv( fenc, FENC_STRIDE, pix2, stride, w, h );              \
}                                                                                 \
void x264_pixel_sad_x4_##size##_rvv( pixel *fenc, pixel *pix0, pixel *pix1,       \
                                     pixel *pix2, pixel *pix3, intptr_t stride,   \
                                     int scores[4] )                              \
{                                                                                 \
    scores[0] = sad_u8_rvv( fenc, FENC_STRIDE, pix0, stride, w, h );              \
    scores[1] = sad_u8_rvv( fenc, FENC_STRIDE, pix1, stride, w, h );              \
    scores[2] = sad_u8_rvv( fenc, FENC_STRIDE, pix2, stride, w, h );              \
    scores[3] = sad_u8_rvv( fenc, FENC_STRIDE, pix3, stride, w, h );              \
}

SAD_X_WRAP( 16x16, 16, 16 )
SAD_X_WRAP( 16x8,  16,  8 )
SAD_X_WRAP( 8x16,   8, 16 )
SAD_X_WRAP( 8x8,    8,  8 )
SAD_X_WRAP( 8x4,    8,  4 )
SAD_X_WRAP( 4x8,    4,  8 )
SAD_X_WRAP( 4x4,    4,  4 )
