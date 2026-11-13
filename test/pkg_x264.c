/* x264 探针：10 帧 64x64 YUV420 纯 C 路径编码，输出 Annex-B 码流并取哈希。
   基线阶段不看像素（--disable-asm，两侧同一条 C 路径），比的是码流逐字节。 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <x264.h>

static unsigned long long fnv1a(const void *buf, size_t n)
{
	const unsigned char *p = (const unsigned char *)buf;
	unsigned long long h = 0xcbf29ce484222325ULL;
	for (size_t i = 0; i < n; i++) { h ^= p[i]; h *= 0x100000001b3ULL; }
	return h;
}

int main(int argc, char **argv)
{
	if (argc < 2) { fprintf(stderr, "usage: %s <out.264>\n", argv[0]); return 2; }
	const int W = 64, H = 64, FRAMES = 10;

	x264_param_t p;
	x264_param_default_preset(&p, "veryfast", "psnr");
	p.i_csp = X264_CSP_I420;
	p.i_width = W; p.i_height = H;
	p.i_fps_num = 25; p.i_fps_den = 1;
	p.i_threads = 1;
	p.b_annexb = 1;
	p.i_keyint_max = 10;
	p.rc.i_rc_method = X264_RC_CRF;
	p.rc.f_rf_constant = 23.0f;
	p.analyse.i_me_method = X264_ME_DIA;
	p.analyse.i_subpel_refine = 2;

	x264_t *h = x264_encoder_open(&p);
	if (!h) { fprintf(stderr, "x264_encoder_open failed\n"); return 2; }

	FILE *f = fopen(argv[1], "wb");
	if (!f) { perror("fopen"); return 2; }

	int frames = 0, bytes = 0;
	unsigned long long payload_hash = 0;
	x264_nal_t *nal = NULL;
	int nnal = 0;
	x264_encoder_headers(h, &nal, &nnal);
	for (int i = 0; i < nnal; i++) {
		fwrite(nal[i].p_payload, 1, (size_t)nal[i].i_payload, f);
		bytes += nal[i].i_payload;
		payload_hash ^= fnv1a(nal[i].p_payload, (size_t)nal[i].i_payload);
	}

	x264_picture_t pic_in, pic_out;
	x264_picture_alloc(&pic_in, X264_CSP_I420, W, H);
	for (int n = 0; n < FRAMES; n++) {
		for (int y = 0; y < H; y++)
			for (int x = 0; x < W; x++)
				pic_in.img.plane[0][y * pic_in.img.i_stride[0] + x] =
					(unsigned char)((x * 3 + y * 5 + n * 7) & 0xff);
		for (int y = 0; y < H / 2; y++)
			for (int x = 0; x < W / 2; x++) {
				pic_in.img.plane[1][y * pic_in.img.i_stride[1] + x] = (unsigned char)(128 + n);
				pic_in.img.plane[2][y * pic_in.img.i_stride[2] + x] = (unsigned char)(128 - n);
			}
		pic_in.i_pts = n;
		int ret = x264_encoder_encode(h, &nal, &nnal, &pic_in, &pic_out);
		if (ret < 0) { fprintf(stderr, "encode failed at %d\n", n); return 2; }
		for (int i = 0; i < nnal; i++) {
			fwrite(nal[i].p_payload, 1, (size_t)nal[i].i_payload, f);
			bytes += nal[i].i_payload;
		}
		frames++;
	}
	fclose(f);
	x264_picture_clean(&pic_in);
	x264_encoder_close(h);

	unsigned long long h_all = fnv1a(NULL, 0);
	{
		FILE *r = fopen(argv[1], "rb");
		unsigned char buf[4096];
		size_t n;
		h_all = 0xcbf29ce484222325ULL;
		while ((n = fread(buf, 1, sizeof(buf), r)) > 0) {
			for (size_t i = 0; i < n; i++) { h_all ^= buf[i]; h_all *= 0x100000001b3ULL; }
		}
		fclose(r);
	}
	printf("enc frames=%d bytes=%d fnv_bit=%016llx\n", frames, bytes, h_all);
	printf("headers fnv=%016llx\n", payload_hash);
	return 0;
}
