# 进度 —— `media-libs/x264`

倒序追加，每次一个条目。门禁变更必须同时更新 `project.conf` 的 `GATE`，
否则主仓库看板会与事实脱节。

## 2026-11-13 · G0 planned

- 生成骨架（`rvv-port-project` Skill，模板来自 `libspeex-rvv`）
- 范围与通过标准写入 `docs/plan.md`
- 上游 URL：https://code.videolan.org/videolan/x264.git；固定点：`0480cb05fa188d37ae87e8f4fd8f1aea3711f7ee`
- 下一步（G1）：
  1. `./scripts/fetch-upstream.sh --dry-run` 确认 URL/SHA 后再拉取
  2. `src/` 初始化为 upstream 的副本（一次性提交，便于 diff）
  3. `./scripts/build.sh riscv64` + `./test/run_ab.sh` 跑通

## 2026-11-15 G3：8bit SAD / SSD RVV 内核

- `src/common/riscv/sad_rvv.c`：通用 `sad_u8_rvv` / `ssd_u8_rvv` + 7 尺寸包装 + sad_x3/x4 包装；
  `pixel.c` 同一 TU include，`x264_pixel_init` 末尾覆盖 `sad[]`/`sad_x3[]`/`sad_x4[]`/`ssd[]`。
- 功能：qemu off/on 码流 `bytes=690 fnv_bit=91e876b2b8b16d8c` 与 G2 一致。
- 真机内核：SAD16 5.97×、SAD8 3.27×、SSD16 2.02×、SSD8 1.34×。
- 真机整链：352×288 1.249×（6 轮）、640×480 1.230×（3 轮），码流逐字节一致。
- 三个新坑：configure 生成 config.h 的 `HAVE_RVV 0` 会覆盖命令行宏（改用 HAVE_RVV_KERNELS）；
  `pixel.c` 编两遍需 `BIT_DEPTH == 8` 门；`--extra-cflags` 含空格必须用数组传；
  `vwredsumu_vs_<源>_<累加器>` 命名源在前；GCC 13/14 无 `vabd` intrinsic。
