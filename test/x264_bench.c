/* 整链基准：<宽> <高> <帧数> [reps]，编码同一条合成画面序列，打印码流哈希与耗时。
 * 与 test/pkg_x264.c 的哈希口径保持一致（bytes + fnv_bit），便于和 G2 基线对齐。 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <x264.h>

static unsigned long long fnv1a(const void *buf, size_t n)
{
	const unsigned char *p = (const unsigned char *)buf;
	unsigned long long h = 0xcbf29ce484222325ULL;
	for (size_t i = 0; i < n; i++) { h ^= p[i]; h *= 0x100000001b3ULL; }
	return h;
}
static long long now_us(void)
{
	struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
	return (long long)ts.tv_sec * 1000000LL + ts.tv_nsec / 1000;
}

int main(int argc, char **argv)
{
	int W = argc > 1 ? atoi(argv[1]) : 352;
	int H = argc > 2 ? atoi(argv[2]) : 288;
	int N = argc > 3 ? atoi(argv[3]) : 30;
	int reps = argc > 4 ? atoi(argv[4]) : 1;

	x264_param_t p;
	x264_param_default_preset(&p, "veryfast", "psnr");
	p.i_csp = X264_CSP_I420;
	p.i_width = W; p.i_height = H;
	p.i_fps_num = 25; p.i_fps_den = 1;
	p.i_threads = 1;
	p.b_annexb = 1;
	p.i_keyint_max = 30;
	p.rc.i_rc_method = X264_RC_CRF;
	p.rc.f_rf_constant = 23.0f;
	p.analyse.i_me_method = X264_ME_HEX;

	x264_t *h = x264_encoder_open(&p);
	if (!h) { fprintf(stderr, "open failed\n"); return 2; }

	x264_picture_t pic_in, pic_out;
	x264_picture_alloc(&pic_in, X264_CSP_I420, W, H);

	unsigned long long bytes = 0, hbit = 0, hdr = 0;
	int nnal; x264_nal_t *nal;
	x264_encoder_headers(h, &nal, &nnal);
	for (int i = 0; i < nnal; i++) { hdr ^= fnv1a(nal[i].p_payload, (size_t)nal[i].i_payload); }

	long long best = -1;
	for (int r = 0; r < reps; r++) {
		bytes = 0; hbit = 0;
		long long t0 = now_us();
		for (int n = 0; n < N; n++) {
			for (int y = 0; y < H; y++)
				for (int x = 0; x < W; x++)
					pic_in.img.plane[0][y * pic_in.img.i_stride[0] + x] =
						(unsigned char)((x * 3 + y * 5 + (n % 5) * 7) & 0xff);
			for (int y = 0; y < H / 2; y++)
				for (int x = 0; x < W / 2; x++) {
					pic_in.img.plane[1][y * pic_in.img.i_stride[1] + x] =
						(unsigned char)((x * 5 + y * 3 + (n % 5) * 11) & 0xff);
					pic_in.img.plane[2][y * pic_in.img.i_stride[2] + x] =
						(unsigned char)((x * 7 + y * 2 + (n % 5) * 13) & 0xff);
				}
			pic_in.i_pts = n;
			x264_encoder_encode(h, &nal, &nnal, &pic_in, &pic_out);
			for (int i = 0; i < nnal; i++) {
				bytes += (unsigned long long)nal[i].i_payload;
				hbit ^= fnv1a(nal[i].p_payload, (size_t)nal[i].i_payload);
			}
		}
		long long dt = now_us() - t0;
		if (best < 0 || dt < best) best = dt;
	}
	printf("frames=%d bytes=%llu fnv_bit=%016llx fnv_hdr=%016llx total_us=%lld\n",
	       N, bytes, hbit, hdr, best);
	x264_encoder_close(h);
	return 0;
}
