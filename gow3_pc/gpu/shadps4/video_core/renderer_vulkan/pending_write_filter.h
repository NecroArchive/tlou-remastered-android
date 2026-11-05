// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace Vulkan::Detail {
// Hashed pending-write filter. It remains conservative: collisions only cause
// an exact range lookup. Mark/query retain the original granule and wrap rules.
template<std::size_t Words>
void MarkPendingWords(std::array<std::uint64_t,Words>& words,
                      std::uint64_t first, std::uint64_t last) {
    const auto first_word=first>>6, last_word=last>>6;
    for(auto i=first_word;i<=last_word;++i) {
        auto mask=~std::uint64_t{0};
        if(i==first_word)mask&=~std::uint64_t{0}<<(first&63);
        if(i==last_word)mask&=~std::uint64_t{0}>>(63-(last&63));
        words[i]|=mask;
    }
}
template<unsigned GranuleBits,std::size_t Words>
void MarkPendingFilter(std::array<std::uint64_t,Words>& words,std::uint64_t begin,std::uint64_t end) {
    constexpr auto bits=Words*64;
    const auto first=begin>>GranuleBits,last=(end-1)>>GranuleBits;
    if(first==last){words[(first%bits)>>6]|=std::uint64_t{1}<<(first&63);return;}
    if(last-first>=bits){words.fill(~std::uint64_t{0});return;}
    const auto start=first%bits;
    const auto finish=start+(last-first);
    MarkPendingWords(words,start,finish<bits?finish:bits-1);
    if(finish>=bits)MarkPendingWords(words,0,finish-bits);
}
template<unsigned GranuleBits,std::size_t Words>
bool PendingFilterIntersects(const std::array<std::uint64_t,Words>& words,std::uint64_t begin,std::uint64_t end) {
    constexpr auto bits=Words*64;
    const auto first=begin>>GranuleBits,last=(end-1)>>GranuleBits;
    if(first==last)return (words[(first%bits)>>6]>>(first&63))&1;
    if(last-first>=bits)return true;
    // Query stays const and applies the same first/last word masks as Mark.
    const auto start=first%bits,finish=start+(last-first);
    const auto any=[&](std::uint64_t lo,std::uint64_t hi){
        for(auto i=lo>>6;i<=hi>>6;++i){
            auto mask=~std::uint64_t{0};
            if(i==(lo>>6))mask&=~std::uint64_t{0}<<(lo&63);
            if(i==(hi>>6))mask&=~std::uint64_t{0}>>(63-(hi&63));
            if(words[i]&mask)return true;
        }
        return false;
    };
    return any(start,finish<bits?finish:bits-1)||(finish>=bits&&any(0,finish-bits));
}
} // namespace Vulkan::Detail
