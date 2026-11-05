// Frozen original production GpuRangesAdd; comparison reference.
    void LegacyAdd(VAddr address, u64 size) {
        if (size) {
            const u64 first = address >> 16;
            const u64 last = std::min<u64>((address + size - 1) >> 16, GpuWindowBits - 1);
            for (u64 w = first; w <= last; ++w) {
                auto& word = gpu_window_bits[w >> 6];
                const u64 bit = 1ull << (w & 63);
                if (!(word.load(std::memory_order_relaxed) & bit)) {
                    word.fetch_or(bit, std::memory_order_relaxed);
                }
            }
        }
        std::unique_lock lock{gpu_ranges_mutex};
        // gow3 20 Oct 2026 (laptop CPUs): a game may dispatch ~1,300 compute jobs a frame,
        // most writing the ranges they wrote before; adding a range the set already holds cost
        // the GPU command thread ~2.3% on a Ryzen 7840HS (laptop, 47 FPS CPU-bound),
        // and the window ticks of a range already noted in this submission ~1.4%. A small cache
        // of recent adds skips both while nothing was subtracted from the range since
        // (GpuRangesSubtract clears overlapping entries). GOW3_GPU_RANGE_CACHE=0: off.
        static const bool range_cache = [] {
            const char* env = std::getenv("GOW3_GPU_RANGE_CACHE");
            return !(env && env[0] == '0');
        }();
        const u64 tick = size && range_cache ? RangeTick() : 0;
        if (range_cache && size) {
            auto& entry = gpu_add_cache[((address >> 6) ^ (address >> 18) ^ size) & (GpuAddCacheSize - 1)];
            if (entry.address == address && entry.size == size) {
                if (entry.tick != tick) {
                    entry.tick = tick;
                    NoteGpuWriteWindows(address, size);
                }
                return;
            }
            entry = {address, size, tick};
        }
        gpu_modified_ranges.Add(address, size);
        if (size) {
            NoteGpuWriteWindows(address, size);
        }
    }
