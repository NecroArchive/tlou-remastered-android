#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
test_dir=$(mktemp -d)
trap 'rm -rf "$test_dir"' EXIT
for contract in tsc_clock fex_clock services; do
  cc -std=c11 -O2 -Wall -Wextra -Werror -pthread -I gow3_pc -I gow3_pc/src \
    "gow3_pc/tests/test_${contract}.c" -lm -o "$test_dir/$contract"
  "$test_dir/$contract"
done
g++ -std=c++17 -O2 -Wall -Wextra -Werror -pthread -I gow3_pc -I gow3_pc/src \
  gow3_pc/tests/test_fiber_bionic.cpp -o "$test_dir/fibers"
"$test_dir/fibers"

g++ -std=c++20 -O2 -Wall -Wextra -Werror gow3_pc/tests/test_image_copy_plan.cpp -o "$test_dir/image-copies"
"$test_dir/image-copies"

# Movie source is 1920x1088 padded NV12; 720p must use the existing 4 MiB decoder buffer.
g++ -std=c++20 -O2 -Wall -Wextra -Werror -pthread -I gow3_pc/gpu/shim \
  gow3_pc/gpu/shim/gow3_movie.cpp gow3_pc/tests/test_movie_inplace.cpp -o "$test_dir/movie-inplace"
"$test_dir/movie-inplace"

g++ -std=c++20 -O2 -Wall -Wextra -Werror -pthread gow3_pc/tests/test_cache_io_queue.cpp -o "$test_dir/cache-io"
"$test_dir/cache-io"

cc -std=c11 -O2 -Wall -Wextra -Werror gow3_pc/tests/test_crash_guest_dump.c -o "$test_dir/crash-dump"
"$test_dir/crash-dump"

cc -std=c11 -O2 -Wall -Wextra -Werror -pthread gow3_pc/tests/test_android_cpu_policy.c -o "$test_dir/cpu-policy"
"$test_dir/cpu-policy"

g++ -std=c++20 -O2 -Wall -Wextra -Werror gow3_pc/tests/test_mapped_readback.cpp -o "$test_dir/mapped-readback"
"$test_dir/mapped-readback"

g++ -std=c++20 -O2 -Wall -Wextra -Werror gow3_pc/tests/test_tlou_compute_split.cpp -o "$test_dir/tlou-compute-split"

g++ -std=c++20 -O2 -Wall -Wextra -Werror gow3_pc/tests/test_tlou_inline_copy_record.cpp -o "$test_dir/tlou-inline-copy"

"$test_dir/tlou-inline-copy"
"$test_dir/tlou-compute-split"

# Producer-tick and late CPU-write safety gates for temporal SRT downloads.
g++ -std=c++20 -O2 -Wall -Wextra -Werror gow3_pc/tests/test_tlou_hot_srt_readback.cpp -o "$test_dir/tlou-hot-srt"
"$test_dir/tlou-hot-srt"

# Distinguish fewer wait API calls from actual modeled producer stalls.
# Includes adverse locality, GPU rewrites, unused fields and late CPU writes.
# Timing is informational; deterministic counters gate claims, not noisy speed.
g++ -std=c++20 -O2 -Wall -Wextra -Werror gow3_pc/tests/test_tlou_srt_cost.cpp -o "$test_dir/tlou-srt-cost"
"$test_dir/tlou-srt-cost"

g++ -std=c++20 -O2 -Wall -Wextra -Werror -pthread gow3_pc/tests/test_cpu_phase_sample.cpp -o "$test_dir/cpu-phase-sample"
"$test_dir/cpu-phase-sample" --correctness-only

g++ -std=c++20 -O2 -Wall -Wextra -Werror gow3_pc/tests/test_tlou_srt_batch_plan.cpp -o "$test_dir/tlou-srt-batch"
"$test_dir/tlou-srt-batch"

# TLOU must never restore graphics-breaking Readback 2 from a legacy preference.
grep -Fq 'env.put("GOW3_READBACKS","1");' android-launcher/app/src/main/java/ro/cobrabm/fexdroid/PortSettings.java
if grep -Fq '"2 · Preciso' android-launcher/app/src/main/java/ro/cobrabm/fexdroid/PortMenu.java; then
    echo 'ERROR: TLOU Precise readback was reintroduced' >&2
    exit 1
fi

# v0.31 opt-in paths: exact memory bytes, packet-local bindings and copy completion.
cc -std=c11 -O2 -Wall -Wextra -Werror -pthread \
  gow3_pc/tests/test_memory_walk_cost.c -o "$test_dir/memory-walk"
"$test_dir/memory-walk" --correctness-only
for test in ring_binding_cursor tlou_host_copy_batch; do
  g++ -std=c++20 -O2 -Wall -Wextra -Werror -pthread -I gow3_pc/gpu/shim -I gow3_pc/gpu/shadps4 \
    "gow3_pc/tests/test_${test}.cpp" -o "$test_dir/$test"
  "$test_dir/$test" --correctness-only
done
python3 gow3_pc/tests/extract_host_copy_chunks.py "$test_dir"
g++ -std=c++20 -O2 -Wall -Wextra -Werror -pthread \
  -I "$test_dir" -I gow3_pc/gpu/shim -I gow3_pc/gpu/shadps4 \
  gow3_pc/tests/test_tlou_threaded_record_chunk.cpp -o "$test_dir/threaded-copy"
"$test_dir/threaded-copy" --correctness-only
