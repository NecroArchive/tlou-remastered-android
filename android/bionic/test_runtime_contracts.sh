#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
test_dir=$(mktemp -d)
trap 'rm -rf "$test_dir"' EXIT
python3 gow3_pc/tests/test_draw_workers.py
python3 gow3_pc/tests/test_android_save_choice.py
cc -std=c11 -O2 -Wall -Wextra -Werror -pthread -DGOW3_ANDROID_CHOICES_TEST \
  -I gow3_pc -I gow3_pc/src gow3_pc/tests/test_services.c -lm -o "$test_dir/android-save-services"
"$test_dir/android-save-services"
# Exercise real AppContent dispatch, including regional nonzero service labels.
cc -std=c11 -D_GNU_SOURCE -O2 -Wall -Wextra -Werror -I gow3_pc -I gow3_pc/src \
  gow3_pc/tests/test_content.c gow3_pc/src/runtime_content.c -o "$test_dir/content"
"$test_dir/content"
"$test_dir/content" --unknown
if "$test_dir/content" --missing; then
  echo 'ERROR: missing AppContent profile was accepted' >&2; exit 1
else
  test "$?" -eq 21
fi
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

# Check Android's narrow fallback for TLOU 01.00 physics result layouts.
# The full APK build must fail if this changes or accidentally matches skinning.
g++ -std=c++20 -O2 -Wall -Wextra -Werror \
  gow3_pc/tests/test_tlou_physics_readback.cpp -o "$test_dir/tlou-physics-readback"

# Exercise all small GPU-writable compute shapes, without shader-specific
# identity and without falling back to global precise readbacks.
g++ -std=c++20 -O2 -Wall -Wextra -Werror \
  gow3_pc/tests/test_tlou_auto_readback.cpp -o "$test_dir/tlou-auto-readback"
"$test_dir/tlou-auto-readback"

# The *default* game mode uses a single scene/HUD resolution including aim
# normalization; native movie/video allocation sites remain untouched.
g++ -std=c++20 -O2 -Wall -Wextra -Werror -I gow3_pc \
  gow3_pc/tests/test_ui_composition.cpp -o "$test_dir/tlou-ui-composition"
"$test_dir/tlou-ui-composition"
grep -Fq 'options.put("resolution",resolution);' android-launcher/app/src/main/java/ro/cobrabm/fexdroid/PortSettings.java
grep -Fq 'edit.remove("render_path");' android-launcher/app/src/main/java/ro/cobrabm/fexdroid/PortSettings.java
if grep -Fq 'choice(video,"render_path"' android-launcher/app/src/main/java/ro/cobrabm/fexdroid/MainActivity.java; then
    echo 'ERROR: discarded resolution method selector is visible' >&2
    exit 1
fi
if grep -Fq 'final boolean proxy=' android-launcher/app/src/main/java/ro/cobrabm/fexdroid/PortSettings.java; then
    echo 'ERROR: legacy proxy renderer unexpectedly reenabled' >&2
    exit 1
fi
grep -Fq 'env.put("GOW3_DMEM_REPORT_MB","4608");' android-launcher/app/src/main/java/ro/cobrabm/fexdroid/GameActivity.java
grep -Fq 'env.put("TLOU_UNIFIED_GUEST_UI","1");' android-launcher/app/src/main/java/ro/cobrabm/fexdroid/PortSettings.java
grep -Fq 'env.remove("GOW3_RENDER_RES");' android-launcher/app/src/main/java/ro/cobrabm/fexdroid/PortSettings.java
grep -Fq '"unified-direct-no-ui-blit"' android-launcher/app/src/main/java/ro/cobrabm/fexdroid/PortSettings.java
grep -Fq 'env.put("GOW3_UPSCALER","off");' android-launcher/app/src/main/java/ro/cobrabm/fexdroid/PortSettings.java
grep -Fq 'GAME_UI_NATIVE_OVERRIDES' android/tlou_patches.py
grep -Fq 'POSTPROCESS_FULL_SITES' android/tlou_patches.py
grep -Fq 'POSTPROCESS_HALF_SITES' android/tlou_patches.py
grep -Fq 'half_width = struct.pack("<I", width // 2).hex()' android/tlou_patches.py
grep -Fq 'half_height = struct.pack("<I", height // 2).hex()' android/tlou_patches.py
grep -Fq 'Perfil de resolução unificada incompleto' android/tlou_patches.py
grep -Fq '0x92EF1A, 0x92EFC0' android/tlou_patches.py
grep -Fq '0x92EF7D, 0x92F00D' android/tlou_patches.py
grep -Fq 'env.put("TLOU_EXPECT_SCENE_SIZE",actualSize);' android-launcher/app/src/main/java/ro/cobrabm/fexdroid/PortSettings.java
grep -Fq 'env.remove("TLOU_SPATIAL_SCENE");' android-launcher/app/src/main/java/ro/cobrabm/fexdroid/PortSettings.java
grep -Fq 'struct.pack_into("<f", insn, 1, 1.0 / height)' android/tlou_patches.py
grep -Fq 'struct.pack_into("<f", insn, 10, 1.0 / width)' android/tlou_patches.py

# TLOU's Android executable calls the *C* Videodec2 runtime. Ensure it
# reaches the isolated Vulkan movie scaler/remapper; the C++ Videodec2 shim
# alone does not receive the real game's decoded frames.
g++ -std=c++20 -O2 -Wall -Wextra -Werror \
    gow3_pc/tests/test_tlou_movie_plane_layout.cpp -o "$test_dir/tlou-movie-layout"
"$test_dir/tlou-movie-layout"
g++ -std=c++20 -O2 -Wall -Wextra -Werror \
    gow3_pc/tests/test_tlou_movie_111_planes.cpp -o "$test_dir/tlou-movie-111-planes"
"$test_dir/tlou-movie-111-planes"
# Both 01.00 and 01.11 now use the same guarded movie-only 64-row
# NV12 plane remapper. Fail with an actionable diagnostic if either end is
# missing instead of silently rejecting the correctly generalized fix.
grep -Fq 'env.put("TLOU_MOVIE_NV12_LAYOUT","1");' android-launcher/app/src/main/java/ro/cobrabm/fexdroid/GameActivity.java || {
    echo 'ERROR: 01.00/01.11 movie plane alignment option missing from launcher' >&2
    exit 1
}
grep -Fq 'if(RemapAligned(address,width,height,pitch,bits))return true;' gow3_pc/gpu/shim/gow3_movie.cpp || {
    echo 'ERROR: shared recorded-frame NV12 remap missing from video bridge' >&2
    exit 1
}
grep -Fq 'TlouMovie111::ChromaAddress(address,base,w,h)' gow3_pc/gpu/shim/gow3_movie.cpp || {
    echo 'ERROR: guarded chroma-plane address matching was lost' >&2
    exit 1
}
cc -std=c11 -Wall -Wextra -Werror -x c -fsyntax-only \
    -include gow3_pc/gpu/shim/gow3_movie_bridge.h /dev/null
grep -Fq 'env.put("GOW3_MOVIE_SIZE",actualSize);' android-launcher/app/src/main/java/ro/cobrabm/fexdroid/PortSettings.java
grep -Fq 'env.remove("GOW3_MOVIE_SIZE");' android-launcher/app/src/main/java/ro/cobrabm/fexdroid/PortSettings.java
grep -Fq 'gow3_movie_scale_nv12(' gow3_pc/src/runtime_videodec.c
grep -Fq 'buffer->frame_buffer_size, f->data[0], f->data[1]' gow3_pc/src/runtime_videodec.c
grep -Fq 'gow3_movie_can_scale_in_place(' gow3_pc/gpu/shim/gow3_movie.cpp
grep -Fq 'in_place ? width : src_width' gow3_pc/gpu/shim/gow3_movie.cpp
grep -Fq 'extern "C" int gow3_movie_scale_nv12(' gow3_pc/gpu/shim/gow3_movie.cpp
grep -Fq 'Gow3Movie::RecordFrame(' gow3_pc/gpu/shim/gow3_movie.cpp
# In-place destination must update actual luma/chroma dimensions rather than stale source layout.
grep -Fq 'guest_base, in_place ? width : src_width' gow3_pc/gpu/shim/gow3_movie.cpp
grep -Fq 'Gow3Movie::Remap(' gow3_pc/gpu/shadps4/video_core/texture_cache/image_info.cpp
grep -Fq 'Gow3Movie::MovieShaderConstant(' gow3_pc/gpu/shadps4/shader_recompiler/recompiler.cpp
grep -Fq 'TLOU movie NV12:' gow3_pc/gpu/shim/gow3_movie.cpp
grep -Fq 'TLOU movie NV12 shader:' gow3_pc/gpu/shim/gow3_movie.cpp
grep -Fq 'TLOU movie NV12 texture:' gow3_pc/gpu/shim/gow3_movie.cpp


# Retain native spatial code tests for Vulkan robustness, but the launcher
# must not enable that retired path (unified mode only).
g++ -std=c++20 -O2 -Wall -Wextra -Werror -I gow3_pc \
  gow3_pc/tests/test_tlou_spatial_scene.cpp -o "$test_dir/tlou-spatial-scene"
"$test_dir/tlou-spatial-scene"
grep -Fq 'env.put("GOW3_UPSCALER","off");' android-launcher/app/src/main/java/ro/cobrabm/fexdroid/PortSettings.java
if grep -Fq 'env.put("TLOU_SPATIAL_SCENE","1");' android-launcher/app/src/main/java/ro/cobrabm/fexdroid/PortSettings.java; then
    echo 'ERROR: removed spatial proxy was reactivated' >&2
    exit 1
fi
grep -Fq '(active || spatial_session) ? preset : 0' gow3_pc/gpu/shadps4/video_core/renderer_vulkan/vk_temporal_upscaler.cpp
grep -Fq '(spatial_session && scene_targets.Reduced())' gow3_pc/gpu/shadps4/video_core/renderer_vulkan/vk_temporal_upscaler.cpp
grep -Fq 'RunUiOnly(color, depth);' gow3_pc/gpu/shadps4/video_core/renderer_vulkan/vk_temporal_upscaler.cpp
grep -Fq 'vk::Filter::eLinear' gow3_pc/gpu/shadps4/video_core/renderer_vulkan/vk_temporal_upscaler.cpp
# The user tested Full geometry sync in both direct 720p and native 1080p:
# retain it alone. Guard against silently restoring Fast/Balanced fallbacks.
grep -Fq 'env.put("TLOU_AUTO_SMALL_CS_READBACK","2");' android-launcher/app/src/main/java/ro/cobrabm/fexdroid/PortSettings.java
grep -Fq 'const bool automatic_result =' gow3_pc/gpu/shadps4/video_core/renderer_vulkan/vk_rasterizer.cpp
if grep -Fq 'EligibleBalanced(' gow3_pc/gpu/shadps4/video_core/renderer_vulkan/vk_rasterizer.cpp; then
    echo 'ERROR: discarded Balanced geometry synchronization was reintroduced' >&2
    exit 1
fi
# Global readback page protection was deliberately removed in the Android
# profile: each physics/EOP result still has its own mandatory protection,
# with Full geometry synchronization and Readback 1 unchanged.
grep -Fq 'env.remove("GOW3_ASYNC_READBACK");' android-launcher/app/src/main/java/ro/cobrabm/fexdroid/PortSettings.java
if grep -Fq 'env.put("GOW3_ASYNC_READBACK","1");' android-launcher/app/src/main/java/ro/cobrabm/fexdroid/PortSettings.java; then
    echo 'ERROR: global protection unexpectedly reenabled in Android profile' >&2
    exit 1
fi
grep -Fq 'buffer_cache.DownloadRangesAsync(eop_readbacks, true, true);' gow3_pc/gpu/shadps4/video_core/renderer_vulkan/vk_rasterizer.cpp
grep -Fq 'if (!never_protect && (SafeReadbacks() || protect))' gow3_pc/gpu/shadps4/video_core/buffer_cache/buffer_cache.cpp

# CUSA00552 01.00: do not regress to the PC-only hash/stride fallback or
# accidentally enable Readback 2 to compensate for stale physics results.
grep -Fq '0x697cfcc4ull' gow3_pc/gpu/shadps4/video_core/renderer_vulkan/tlou_physics_readback.h
grep -Fq 'TlouAndroidPhysicsShape(stage.pgm_hash' gow3_pc/gpu/shadps4/video_core/renderer_vulkan/vk_rasterizer.cpp
grep -Fq 'env.put("GOW3_READBACKS","1");' android-launcher/app/src/main/java/ro/cobrabm/fexdroid/PortSettings.java
"$test_dir/tlou-physics-readback"
"$test_dir/tlou-hot-srt"

# Distinguish fewer wait API calls from actual modeled producer stalls.
# Includes adverse locality, GPU rewrites, unused fields and late CPU writes.
# Timing is informational; deterministic counters gate claims, not noisy speed.
g++ -std=c++20 -O2 -Wall -Wextra -Werror gow3_pc/tests/test_tlou_srt_cost.cpp -o "$test_dir/tlou-srt-cost"
"$test_dir/tlou-srt-cost"

# GPU-range tracking: the new indexed subtraction must invalidate exactly the
# same cached entries as a full 512-slot scan under interleaved add/subtract.
# Run both equivalence and host-cost comparison; do not infer FPS from cost.
g++ -std=c++20 -O2 -Wall -Wextra -Werror \
  -I gow3_pc/gpu/shadps4 \
  gow3_pc/tests/test_tlou_gpu_range_cache_index.cpp -o "$test_dir/gpu-range-index"
"$test_dir/gpu-range-index"
g++ -std=c++20 -O2 -Wall -Wextra -Werror \
  -I gow3_pc/gpu/shadps4 \
  gow3_pc/tests/test_tlou_gpu_write_window_cost.cpp -o "$test_dir/gpu-window-cost"
"$test_dir/gpu-window-cost"

# Stage-A packet encoding must preserve EXACT register IDs and 32-word values
# even across sparse/multiple 64-block dirty masks. ARM64 host microbenchmarks
# gate a measurable 2- and 4-block common-path win before constructing the APK.
# Never equate this encoder-local speedup with a measured Android FPS gain.
g++ -std=c++20 -O2 -Wall -Wextra -Werror \
  -I gow3_pc/gpu/shadps4 \
  gow3_pc/tests/test_tlou_dirty_packet_write.cpp -o "$test_dir/tlou-packet-direct"
"$test_dir/tlou-packet-direct"

# TLOU heavy gameplay calls GetComputePipeline 400k+ times per five seconds.
# Keep shader variant/guest snapshot selection per dispatch; service pending
# builds immediately and cached-hit maintenance at least every 64 lookups.
# One million randomized lookup/worker-completion events must preserve keys.
g++ -std=c++20 -O2 -Wall -Wextra -Werror \
  -I gow3_pc/gpu/shadps4 \
  gow3_pc/tests/test_tlou_compute_cached_hit_gate.cpp -o "$test_dir/tlou-compute-hit"
"$test_dir/tlou-compute-hit"

# The busiest GPU command-thread path: index pending write ranges while
# preserving the original cyclic-first candidate and fence ordering.
g++ -std=c++20 -O2 -Wall -Wextra -Werror \
  -I gow3_pc/gpu/shadps4 \
  gow3_pc/tests/test_tlou_pending_write_index.cpp -o "$test_dir/pending-write-index"
"$test_dir/pending-write-index"
# Most TLOU Stage-A retire passes leave fewer than 32 pending writes.
# Require the real index to skip its large membership-table rebuild until
# the first 32nd write and preserve exact overlap/cyclic-first behavior.
"$test_dir/pending-write-index" --maintenance-gate

# Measure the actual pending-write filter marking path selected for Stage A:
# 250k randomized exact bitmask comparisons and host CPU A/B speedup on the
# 2-MiB and 16-MiB cases. 1-KiB writes intentionally retain the old loop.
# No Android-FPS claims are inferred from these local timings.
g++ -std=c++20 -O2 -Wall -Wextra -Werror \
  -I gow3_pc/gpu/shadps4 \
  gow3_pc/tests/test_tlou_pending_granule_filter.cpp -o "$test_dir/tlou-pending-granules"
"$test_dir/tlou-pending-granules" --cost-gate

# Stage-A pending write pruning: compare every resulting range under
# 180k interleaved merge/commit/retirement events, then require measured
# reduction of needless vector walks on x86/ARM host CPU. Never defer a
# per-draw constant snapshot or an uncommitted packet.
g++ -std=c++20 -O2 -Wall -Wextra -Werror \
  -I gow3_pc/gpu/shadps4 \
  gow3_pc/tests/test_tlou_pending_prune_gate.cpp -o "$test_dir/tlou-pending-prune"
"$test_dir/tlou-pending-prune" --cost-gate

# Device-RAM-tiered constant-ring capacity (32/64/128 MiB). Prove
# that increasing space never overwrites an in-flight GPU snapshot
# and preserves exactly the previous wrap and alignment semantics.
g++ -std=c++20 -O2 -Wall -Wextra -Werror \
  -I gow3_pc/gpu/shadps4 \
  gow3_pc/tests/test_tlou_constant_ring_capacity.cpp \
  -o "$test_dir/tlou-ring-capacity"
"$test_dir/tlou-ring-capacity"

# When ring allocations fail, querying Vulkan's timeline for EVERY attempt
# spends CPU without freeing anything unless the GPU advances. Preserve
# immediate probes on new published stamps / externally known GPU ticks and
# bound an otherwise invisible GPU completion to 32 missed allocations.
g++ -std=c++20 -O2 -Wall -Wextra -Werror -I gow3_pc/gpu/shadps4 \
  gow3_pc/tests/test_tlou_constant_ring_recycle_gate.cpp -o "$test_dir/constant-ring-recycle"
"$test_dir/constant-ring-recycle"

# A stalled 32-MiB constant ring must publish its final position after the
# GPU submission tick changes even if no Allocate() succeeds in that tick.
# Exercise the actual production stamp state machine, safe timeline retirement,
# complete-ring recovery and 100k randomized epoch transitions.
g++ -std=c++20 -O2 -Wall -Wextra -Werror -I gow3_pc/gpu/shadps4 \
  gow3_pc/tests/test_tlou_constant_ring_stamp.cpp -o "$test_dir/constant-ring-stamp"
"$test_dir/constant-ring-stamp"

# Flush constant-ring snapshots by packet, preserving exact byte coverage.
g++ -std=c++20 -O2 -Wall -Wextra -Werror \
  -I gow3_pc/gpu/shadps4 \
  gow3_pc/tests/test_tlou_ring_flush_batch.cpp -o "$test_dir/ring-flush-batch"
"$test_dir/ring-flush-batch"

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

# Fence-order regression: waiting for an earlier signal cannot drain newer copy
# epochs. The same helper must make progress with the copy worker pool disabled.
g++ -std=c++20 -O2 -Wall -Wextra -Werror -pthread \
  gow3_pc/tests/test_tlou_deferred_signal_wait.cpp -o "$test_dir/tlou-deferred-signal-wait"
"$test_dir/tlou-deferred-signal-wait"
grep -Fq 'Detail::WaitForDeferredSignal(*deferred_signals_done, issued,' \
  gow3_pc/gpu/shadps4/video_core/renderer_vulkan/vk_scheduler.cpp
grep -Fq 'done->notify_all();' \
  gow3_pc/gpu/shadps4/video_core/renderer_vulkan/vk_scheduler.cpp

# Verify zero draw-preparation workers in both game releases without changing
# the frame scheduler or correctness barriers.
python3 android/tests/test_tlou_111_preparation_regression.py

# A real 01.00 startup failure was the missing retail-memory report. Movie
# conversion now bypasses the redundant guest-wide 1080p write with fallback.
python3 android/tests/test_tlou_movie_memory_contract.py

# v0.31 opt-in paths: exact memory bytes, packet-local bindings and copy completion.
cc -std=c11 -O2 -Wall -Wextra -Werror -pthread \
  gow3_pc/tests/test_memory_walk_cost.c -o "$test_dir/memory-walk"
"$test_dir/memory-walk" --correctness-only
# Real runtime_memory.c + independently frozen locked reference on the same
# guest VMA fixture. Require byte parity and a host CPU reduction for small
# two-VMA backing uploads; routine-level speedup is not an FPS guarantee.
"$test_dir/memory-walk" --two-span-gate
for test in ring_binding_cursor tlou_host_copy_batch; do
  g++ -std=c++20 -O2 -Wall -Wextra -Werror -pthread -I gow3_pc/gpu/shim -I gow3_pc/gpu/shadps4 \
    "gow3_pc/tests/test_${test}.cpp" -o "$test_dir/$test"
  "$test_dir/$test" --correctness-only
done
# Stage B uses the cursor only for 2+ ring descriptors; require parity and
# a CPU cost reduction against the original per-buffer linear lookup.
"$test_dir/ring_binding_cursor" --cost-gate
python3 gow3_pc/tests/extract_host_copy_chunks.py "$test_dir"
g++ -std=c++20 -O2 -Wall -Wextra -Werror -pthread \
  -I "$test_dir" -I gow3_pc/gpu/shim -I gow3_pc/gpu/shadps4 \
  gow3_pc/tests/test_tlou_threaded_record_chunk.cpp -o "$test_dir/threaded-copy"
"$test_dir/threaded-copy" --correctness-only
