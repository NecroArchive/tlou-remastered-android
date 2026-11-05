// Frozen original production pending-write methods.
void OldNotePendingGpuWrite(VAddr address, u64 size) {
    if (!draw_pipe || !size) {
        return;
    }
    // Draws write the same buffers over and over: an entry for the range only moves on.
    const bool uncommitted = noting_packet && PipeWriteEnd();
    const u64 position = uncommitted ? PendingUncommitted : draw_pipe->Head();
    const VAddr end = address + size;
    if (noting_copy) {
        // A copy shader's destination, for the resource table walks (SyncSrtTables): pending
        // until stage B has run the copy's packet (its end, set at the commit).
        copy_uncommitted |= noting_packet;
        const u64 copy_position = noting_packet ? PendingUncommitted : draw_pipe->Head();
        copy_min = std::min(copy_min, address);
        copy_max = std::max(copy_max, end);
        bool merged = false;
        for (auto& write : pending_copy_writes) {
            if (address <= write.end && write.begin <= end) {
                write.begin = std::min(write.begin, address);
                write.end = std::max(write.end, end);
                write.position = copy_position;
                merged = true;
                break;
            }
        }
        if (!merged) {
            pending_copy_writes.push_back({address, end, copy_position});
        }
    }
    // gow3 31 Oct 2026: 9% of the GPU command thread in a heavy area was this walk. A range no
    // pending write can touch skips it (one that only touches an entry across a granule stays a
    // new entry, which is harmless), and the walk starts at the entry found last.
    const u32 count = OldPendingFilterHit(address, end)
                          ? static_cast<u32>(pending_writes.size())
                          : 0;
    const u32 first = pending_hint < count ? pending_hint : 0;
    for (u32 step = 0; step < count; ++step) {
        const u32 i = first + step < count ? first + step : first + step - count;
        auto& write = pending_writes[i];
        if (address <= write.end && write.begin <= end) {
            pending_hint = i;
            write.begin = std::min(write.begin, address);
            write.end = std::max(write.end, end);
            write.position = position;
            pending_min = std::min(pending_min, address);
            pending_max = std::max(pending_max, end);
            OldFilterPendingRange(address, end);
            if (uncommitted) {
                uncommitted_indices.push_back(i);
            }
            return;
        }
    }
    if (uncommitted) {
        uncommitted_indices.push_back(static_cast<u32>(pending_writes.size()));
    }
    pending_writes.push_back({address, end, position});
    pending_min = std::min(pending_min, address);
    pending_max = std::max(pending_max, end);
    OldFilterPendingRange(address, end);
}
void OldFilterPendingRange(VAddr begin, VAddr end) {
    const u64 first = begin >> PendingGranuleBits;
    const u64 last = (end - 1) >> PendingGranuleBits;
    if (last - first >= PendingFilterBits) {
        pending_filter.fill(~u64{0});
        return;
    }
    for (u64 granule = first; granule <= last; ++granule) {
        const u32 bit = u32(granule % PendingFilterBits);
        pending_filter[bit / 64] |= u64{1} << (bit % 64);
    }
}
bool OldPendingFilterHit(VAddr begin, VAddr end) const {
    const u64 first = begin >> PendingGranuleBits;
    const u64 last = (end - 1) >> PendingGranuleBits;
    if (last - first >= PendingFilterBits) {
        return true;
    }
    for (u64 granule = first; granule <= last; ++granule) {
        const u32 bit = u32(granule % PendingFilterBits);
        if ((pending_filter[bit / 64] >> (bit % 64)) & 1) {
            return true;
        }
    }
    return false;
}
bool OldPendingWriteOverlaps(VAddr address, u64 size) {
    if (pending_writes.empty() || address >= pending_max || address + size <= pending_min) {
        return false;
    }
    // Now and then drop entries whose writes the recording thread has done: the bounds shrink.
    // (Not while PostDraw notes a packet: uncommitted_indices point into the vector.)
    if ((++pending_checks & 63) == 0 && uncommitted_indices.empty()) {
        std::erase_if(pending_writes, [&](const PendingWrite& write) {
            return draw_pipe->Reached(write.position);
        });
        pending_min = ~VAddr{0};
        pending_max = 0;
        pending_filter.fill(0);
        for (const auto& write : pending_writes) {
            pending_min = std::min(pending_min, write.begin);
            pending_max = std::max(pending_max, write.end);
            OldFilterPendingRange(write.begin, write.end);
        }
    }
    const VAddr end = address + size;
    // gow3 26 Oct 2026: no pending write near the range, no walk (the walk was ~6% of the GPU
    // command thread in The Last of Us, a check per read-only constant buffer).
    if (!OldPendingFilterHit(address, end)) {
        return false;
    }
    for (const auto& write : pending_writes) {
        if (address < write.end && write.begin < end && !draw_pipe->Reached(write.position)) {
            return true;
        }
    }
    return false;
}
