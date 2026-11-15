#!/usr/bin/env bash
# G3 接线验证：off/on 双配置交叉构建 + qemu 跑同一探针，比对码流哈希。
# 两侧同为 -march=rv64gcv，唯一变量是 -DHAVE_RVV=1（SAD/SSD 的 RVV 内核）。
set -euo pipefail
cd "$(dirname "$0")/.."
IMAGE=${IMAGE:-rvv-cross:2}
SIZES=(${SIZES:-16 8})
REPS=${REPS:-200000}

docker run --rm -v "$PWD":/work -w /work "${IMAGE}" bash -lc "
set -e
mkdir -p build/g3-ab/bin
for side in off on; do
  if [ \$side = on ]; then EXTRA='-DHAVE_RVV_KERNELS=1 -march=rv64gcv'; else EXTRA='-march=rv64gcv'; fi
  echo \"=== 构建 \$side（EXTRA_C_FLAGS: \$EXTRA）===\"
  HOST=riscv64-linux-gnu CC=riscv64-linux-gnu-gcc EXTRA_C_FLAGS=\"\$EXTRA\" \
    ./cmake/pkg-build.sh /work/src /work/build/g3-ab/\$side/pkg > /tmp/x264-\$side.log 2>&1 \
    || { tail -40 /tmp/x264-\$side.log; exit 1; }
  riscv64-linux-gnu-gcc -march=rv64gcv -O2 -static \
    -I build/g3-ab/\$side/pkg/prefix/include test/pkg_x264.c \
    build/g3-ab/\$side/pkg/prefix/lib/libx264.a -lpthread -lm \
    -o build/g3-ab/bin/harness_\$side
  mkdir -p build/g3-ab/\$side/out
  qemu-riscv64 -L /usr/riscv64-linux-gnu build/g3-ab/bin/harness_\$side \
    build/g3-ab/\$side/out/frame.264 | tee /tmp/hash-\$side.txt
done
echo '=== 比对 ==='
if diff -q /tmp/hash-off.txt /tmp/hash-on.txt >/dev/null; then
  echo 'PASS：开/关 RVV SAD/SSD 的码流哈希完全一致'
else
  echo 'FAIL：两侧码流不同'; diff /tmp/hash-off.txt /tmp/hash-on.txt; exit 1
fi
" 2>&1 | tail -20
