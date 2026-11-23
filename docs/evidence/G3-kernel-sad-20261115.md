# x264 G3：8bit SAD / SSD 的 RVV 内核

- 日期：2026-11-15
- 上游：`videolan/x264` @ `0480cb05fa188d37ae87e8f4fd8f1aea3711f7ee`
- 目标板：SpacemiT K3 / <distro>，`Linux k3 6.18.3 riscv64`，RVV 1.0
- 变量：`-DHAVE_RVV_KERNELS=1 -march=rv64gcv`（两侧同为 `-march=rv64gcv`，唯一变量是该宏）

## 1. 选型理由

x264 的 SSE/NEON 汇编里，SAD/SSD 是运动估计（ME）主循环的核心算子：HEX/DIA 每轮搜索
每候选 MV 都要算一次 SAD，16x16/8x8 是频率最高的两档。这也是本工程已经验过三次的
内核族（libvpx 4.28×、openh264 2.91×），设计可以复用、风险最低。

## 2. 接线方式

x264 的函数表有三张：`pixf->sad[]`、`pixf->sad_x3[]`、`pixf->sad_x4[]`。纯 C 构建下
它们由 `PIXEL_SAD_C` 生成的静态函数填充，所以**只要覆盖指针**即可，不需要改 Makefile：

- `src/common/riscv/sad_rvv.c` 新增通用内核 `sad_u8_rvv` / `ssd_u8_rvv` + 7 个尺寸包装
  + `sad_x3`/`sad_x4` 包装；
- `src/common/pixel.c` 里以 `#if HAVE_RVV_KERNELS && BIT_DEPTH == 8` 直接把该文件当
  同一 TU `#include` 进来，并在 `x264_pixel_init` 末尾用 `INIT7_NAME(...)` 覆盖三张表。

两个必须注意的门：

1. **不能用 `HAVE_RVV` 这个宏名**。x264 的 `configure` 会生成 `config.h` 并写死
   `#define HAVE_RVV 0`（只在 `--enable-asm` 且 `.option arch, +v` 探测通过时才置 1），
   命令行 `-DHAVE_RVV=1` 会被 config.h 覆盖，只报一条 redefined 警告，内核静默不编译。
   这就是"符号数为 0"的真正原因。
2. **必须限定 `BIT_DEPTH == 8`**。`pixel.c` 会被编两遍（`pixel-8.o` / `pixel-10.o`），
   高比特深度下 `pixel` 是 `uint16_t`，8bit 内核套不上去。

## 3. 内核设计

- **归约只在整块末尾做一次**：u8 绝对差（`vmaxu/vminu/vsub`，GCC 13/14 没有 `vabd`
  intrinsic）用 `vwaddu.wv` 累进 u16 累加器，最后 `vwredsumu.vs` 一次归约。
  这是 libvpx v1 踩过的坑——每行一次归约会把 SIMD 收益吃光。
- **不假设 VLEN/MVL**：vl 直接取块宽（4/8/16），宽度 ≤16 落在 u8m1 的前 16 个 lane，
  VLEN=128（qemu）与 VLEN=256（K3 实测推断）都正确。
- SSD 用"先取绝对差再平方"（`|d|² == d²`），避开有符号乘法要换类型的麻烦；
  `vwmulu` 只接无符号操作数。
- 归约 intrinsic 的命名是 `vwredsum_u_vs_<源类型>_<累加器类型>`（**源在前**，累加器 SEW
  是源的两倍）。写反了编译器只会报 implicit declaration。

## 4. 功能验证（qemu，逐字节）

`./scripts/g3-wire-ab.sh`（off/on 双配置交叉构建 + 同一探针）：

```
off: enc frames=10 bytes=690 fnv_bit=91e876b2b8b16d8c
     headers fnv=9022402ce7cab272
on : enc frames=10 bytes=690 fnv_bit=91e876b2b8b16d8c
     headers fnv=9022402ce7cab272
PASS
```

与 G2 基线哈希完全一致。内核级另有 `test/kernel_sad.c`：同一二进制里先拿本地复刻的
`PIXEL_SAD_C` 循环比对（sad/ssd 数值必须相等）才计时。

## 5. 真机性能（K3）

### 5.1 内核级（100k 次/档，3 轮）

| 算子 | 尺寸 | C | RVV | 比值 |
|---|---|---|---|---|
| SAD | 16x16 | 340.4–348.9 ns | 57.8–58.9 ns | **5.81–5.98×** |
| SAD | 8x8 | 115.8–118.0 ns | 35.4 ns | **3.27–3.33×** |
| SSD | 16x16 | 290.4–291.5 ns | 142.7–143.7 ns | **2.02–2.04×** |
| SSD | 8x8 | 104.1–104.7 ns | 77.5–77.6 ns | **1.34–1.35×** |

### 5.2 整链（`veryfast` + HEX ME + CRF 23，单线程，每轮 2 次取最优）

| 分辨率/帧数 | off | on | 比值 |
|---|---|---|---|
| 352×288 / 30 帧（6 轮） | 416.7–419.8 ms | 332.8–339.8 ms | **1.24–1.26×**（均值 1.249×） |
| 640×480 / 20 帧（3 轮） | 665.9–672.9 ms | 540.4–547.9 ms | **1.228–1.232×** |

两侧码流逐字节一致（352×288：`bytes=242393 fnv_bit=2c6e38b64b86a1cc`；
640×480：`bytes=461071 fnv_bit=9be77e95e98aedb9`）。

这是本工程到目前**整链收益最大**的一个内核，原因很直接：SAD 在 x264 的 ME 里占比高
（与 libde265 的 IDCT 占解码 <1% 形成对照），而且 16x16 内核本身有 6×。

## 6. 尚未做

- `satd`（Hadamard 变换域代价）未向量化：它比 SAD 更热（`--subme >= 4` 时主导），
  但需要 4x4/8x8 Hadamard 与 `abs2` 打包，是下一个候选。
- 4x16 尺寸未接（`INIT8` 里那一档），10bit 路径未接。
- 无运行时 CPU 探测：现在靠编译期宏，与 libvpx 的 `CONFIG_RVV_SAD` 同性质。

## 7. 复现

- 功能：`./scripts/g3-wire-ab.sh`
- 内核：`/root/rvv-k/x264/kernel_sad <16|8> <reps>`
- 整链：`/root/rvv-k/x264/enc_off|enc_on <W> <H> <frames> [reps]`
