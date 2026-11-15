#!/usr/bin/env bash
# 构建上游 x264（自带 configure）→ <out>/prefix。
# 基线两侧都 --disable-asm：aarch64 NEON 与 riscv 一字一汇编的码流必然不同，
# 基线阶段先只比"纯 C 路径"，汇编/RVV 留给内核阶段。
set -euo pipefail
SRC=$1
OUT=$2
rm -rf "${OUT}"
mkdir -p "${OUT}/prefix" "${OUT}/src"
cp -R "${SRC}" "${OUT}/src/pkg"
cd "${OUT}/src/pkg"
HOST_ARG=""
[ -n "${HOST:-}" ] && HOST_ARG="--host=${HOST}"

# EXTRA_C_FLAGS：额外编译参数（走后 '-DHAVE_RVV=1 -march=rv64gcv' 这类）。
# 必须整体作为一个 configure 参数传，不能靠 shell 拆词。
# 用 bash 数组：EXTRA_C_FLAGS 里通常含空格（"-Dx=1 -march=..."），
# 写成字符串再展开会被 shell 拆成多个参数，后面的 flag 会被 configure 丢掉。
EXTRA_ARGS=()
if [ -n "${EXTRA_C_FLAGS:-}" ]; then
  EXTRA_ARGS+=("--extra-cflags=${EXTRA_C_FLAGS}")
fi
./configure --prefix="${OUT}/prefix" ${HOST_ARG} \
  --enable-static --disable-cli --disable-asm --disable-opencl --disable-lavf \
  --disable-swscale --disable-thread "${EXTRA_ARGS[@]}" >/dev/null
make -j"$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)" >/dev/null
make install >/dev/null
echo "== 上游 x264 构建完成：${OUT}/prefix"
