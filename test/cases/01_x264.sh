#!/usr/bin/env bash
# 真实包用例：x264 编码 10 帧 64x64，比 Annex-B 码流与帧数。
set -euo pipefail
: "${STAGE:=run}" "${SIDE:=A}" "${BIN:=build/native}" "${DATA:=build/ab-data}" "${OUT:=build/ab-data/out}"
mkdir -p "$DATA" "$OUT"
case "$STAGE" in
  gen) : > "$DATA/x264-input.txt" ;;
  run) "$BIN/x264_harness" "$OUT/frame.264" > "$OUT/hash.txt" ;;
  *) echo "unknown STAGE=$STAGE" >&2; exit 2 ;;
esac
