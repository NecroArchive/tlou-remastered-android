#!/usr/bin/env python3
"""Keep the candidate and frozen v0.30 queue implementations independent."""
from pathlib import Path
import sys

root = Path(__file__).resolve().parents[1]
out = Path(sys.argv[1])
out.mkdir(parents=True, exist_ok=True)
source = (root/'gpu/shadps4/video_core/renderer_vulkan/vk_scheduler.h').read_text()
start = source.index('class RecordChunk {')
end = source.index('\nclass Scheduler {', start)
legacy = (root/'tests/fixtures/tlou_record_chunk_v030.inc').read_text()
prefix = '''#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>
#include <vector>
#include "video_core/renderer_vulkan/tlou_host_copy_batch.h"
using u64=std::uint64_t;
namespace Gow3Toggle {
enum Flag {RecordPrefetch};
inline bool prefetch_enabled=true;
inline bool Disabled(Flag){return !prefetch_enabled;}
}
#define ASSERT(x) assert(x)
namespace Vulkan {
'''
code = legacy+'\n'+source[start:end]
(out/'tlou_record_chunks_both.h').write_text(prefix+code.replace('vk::CommandBuffer','int')+'\n}\n')
