#!/usr/bin/env python3
"""Compile the actual production range registry methods without a Vulkan device.

Only scheduler.CurrentTick is supplied by the test. RangeSet, cache, mutex,
sticky bits, window/page ticks and subtract invalidation remain production code.
Fail on missing declarations instead of silently benchmarking a stale copy.
"""
from pathlib import Path
import sys


def declaration(source, signature):
    start = source.index(signature)
    opening = source.index('{', start)
    depth = 1
    at = opening+1
    while depth:
        if source[at] == '{':
            depth += 1
        elif source[at] == '}':
            depth -= 1
        at += 1
    return source[start:at]


root = Path(__file__).resolve().parents[1]
out = Path(sys.argv[1])
out.mkdir(parents=True, exist_ok=True)
header = (root/'gpu/shadps4/video_core/buffer_cache/buffer_cache.h').read_text()
cpp = (root/'gpu/shadps4/video_core/buffer_cache/buffer_cache.cpp').read_text()
ranges = (root/'gpu/shadps4/video_core/buffer_cache/range_set.h').read_text()
(out/'production_range_set.inc').write_text(declaration(ranges, 'struct RangeSet {')+';\n')
parts = [declaration(header, '    void GpuRangesAdd('),
         declaration(header, '    void GpuRangesSubtract('),
         declaration(cpp, 'void BufferCache::NoteGpuWriteWindows(').replace('BufferCache::', '')]
(out/'production_gpu_ranges.inc').write_text('\n'.join(parts)+'\n')
rasterizer=(root/'gpu/shadps4/video_core/renderer_vulkan/vk_rasterizer.cpp').read_text()
parts=[declaration(rasterizer, signature).replace('Rasterizer::','') for signature in
       ('void Rasterizer::NotePendingGpuWrite(', 'void Rasterizer::FilterPendingRange(',
        'bool Rasterizer::PendingFilterHit(', 'bool Rasterizer::PendingWriteOverlaps(')]
(out/'production_pending_writes.inc').write_text('\n'.join(parts)+'\n')
