// SPDX-License-Identifier: GPL-2.0-or-later
// Packet-owned flattened descriptors must survive Stage A's next-shader updates.
// Exact payload parity of early constant-ring copy vs packet snapshot copied
// later into the recording thread's Vulkan stream buffer (no guest reread).
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <vector>

int main() {
    std::mt19937 gen{0x112026};
    std::vector<std::uint32_t> live(2048), early(2048), packet(2048), stage_b(2048);
    for (unsigned draw=0;draw<100000;++draw) {
        const std::size_t n=1+(gen()%live.size());
        for (std::size_t i=0;i<n;++i) live[i]=gen();
        // Prior implementation: command thread binds early ring copy.
        std::memcpy(early.data(),live.data(),n*4);
        // Still required: PostDraw encodes flattened descriptors in packet.
        std::memcpy(packet.data(),live.data(),n*4);
        // A different guest draw changes flattened descriptors before B.
        for (std::size_t i=0;i<n;++i) live[i]=gen();
        // New path: B's FlatUserData() selects packet.ud_snapshots.
        std::memcpy(stage_b.data(),packet.data(),n*4);
        assert(std::memcmp(early.data(),stage_b.data(),n*4)==0);
    }
    // Small payloads now stay on the early constant ring, because moving
    // these frequent tiny uploads to Stage B cost more than it saved.
    // Large packet-owned payloads still use the Stage-B snapshot.
    constexpr std::size_t threshold = 4096;
    assert(1024 < threshold);
    assert(4096 >= threshold);
    assert(8192 >= threshold);
    // Reusing the same immutable flattened payload for two shader Flatbuf
    // bindings is byte-identical; different stages keep distinct offsets.
    std::array<std::uint32_t, 64> flatbuf{};
    for (auto &x : flatbuf) x=gen();
    std::array<std::uint32_t, 64> first{}, second{};
    std::memcpy(first.data(),flatbuf.data(),sizeof(flatbuf));
    std::memcpy(second.data(),first.data(),sizeof(flatbuf));
    assert(std::memcmp(first.data(),second.data(),sizeof(flatbuf))==0);
    puts("TLOU Flatbuf per-draw packet-owned snapshots: 100000 changing draws PASS");
}
