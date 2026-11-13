// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
#include <algorithm>
#include <cstddef>
#include <span>
#include <vector>

namespace Shader::Arm64Srt {
// Keep the portable cache format: ARM64 code is regenerated from these operations on load.
enum SrtOp : std::uint32_t {
    OpEnd, OpPushImm, OpPushDyn, OpPop, OpCopyImm, OpCopyDyn, OpUserData, OpFlat,
    OpImm, OpAdd, OpSub, OpMul, OpShl, OpShr, OpAnd, OpOr, OpXor, OpNot,
    OpUMin, OpUMax, OpBitFieldUExtract,
};
constexpr std::uint32_t OpZeroed = 0x80000000u, SrtMagic = 0x42545253u;
constexpr std::uint32_t MaxDepth = 64, MaxValues = 32;

// x0=user data, x1=flattened data, x2=current pointer.
// Keep the expression stack's top in w3, not in the spill array: SRT
// walkers run on every draw/dispatch; previously each binary op spilled
// its result, and every following op loaded it again. w4/w5 are arithmetic
// scratch and w6 is the temporary guest-copy value. All are caller-saved.
// Stack layout is unchanged for compatibility: 64 pointers, 32 values.
inline std::vector<std::uint32_t> Compile(std::span<const std::uint32_t> program) {
    using U32 = std::uint32_t;
    constexpr U32 Frame = MaxDepth * 8 + MaxValues * 4;
    if (program.size() < 2 || program[0] != SrtMagic || program.size() > 262144) return {};
    std::vector<U32> out{0xd10003ffu | (Frame << 10), 0xaa0003e2u};
    std::size_t pc = 1;
    U32 depth = 0, count = 0;
    bool valid = true;
    const auto emit = [&](U32 code) { out.push_back(code); };
    const auto next = [&]() -> U32 {
        if (pc >= program.size()) { valid = false; return 0; }
        return program[pc++];
    };
    const auto imm = [&](U32 reg, U32 value) {
        emit(0x52800000u | ((value & 65535) << 5) | reg);
        if (value >> 16) emit(0x72a00000u | ((value >> 16) << 5) | reg);
    };
    const auto load = [&](U32 reg, U32 slot) {
        emit(0xb94003e0u | (((MaxDepth * 8 + slot * 4) / 4) << 10) | reg);
    };
    const auto store = [&](U32 reg, U32 slot) {
        emit(0xb90003e0u | (((MaxDepth * 8 + slot * 4) / 4) << 10) | reg);
    };
    while (valid && pc < program.size()) {
        const U32 word = next(), op = word & ~OpZeroed;
        const bool zero = (word & OpZeroed) != 0;
        if (op == OpEnd) {
            if (depth || count || pc != program.size()) return {};
            emit(0x910003ffu | (Frame << 10)); emit(0xd65f03c0u);
            return out;
        }
        if (op == OpPushImm || op == OpPushDyn || op == OpCopyImm || op == OpCopyDyn) {
            const bool push = op == OpPushImm || op == OpPushDyn;
            const bool dynamic = op == OpPushDyn || op == OpCopyDyn;
            U32 static_offset = 0;
            if (dynamic) {
                if (!count) return {};
                emit(0x2a0303e4u); // mov w4, w3, before popping the expression top
                --count;
                if (count) load(3, count - 1);
                emit(0x0b040084u); emit(0x0b040084u); // offset << 2, 32-bit wrap
            } else {
                static_offset = next(); // persisted SRT offsets are in DWORDS
                // ARM64 unsigned immediate loads encode a scaled 12-bit
                // address directly. In the common SRT table-copy case this
                // eliminates BOTH immediate construction and register-offset
                // addressing, without changing the 32-bit wrap fallback.
                const bool fits_ptr = push && !(static_offset & 1u) &&
                                      static_offset / 2 < 4096; // 64-bit x2 load
                const bool fits_word = !push && static_offset < 4096; // 32-bit w6 load
                if (!zero && !(fits_ptr || fits_word)) imm(4, static_offset << 2);
            }
            if (push) {
                if (depth == MaxDepth) return {};
                emit(0xf90003e2u | (depth++ << 10));
                if (zero) {
                    emit(0xd2800002u); // mov x2,#0
                } else {
                    if (!dynamic && !(static_offset & 1u) && static_offset / 2 < 4096) {
                        emit(0xf9400042u | ((static_offset / 2) << 10)); // ldr x2,[x2,#imm]
                    } else {
                        emit(0xf8646842u); // ldr x2,[x2,w4,uxtw]
                    }
                    emit(0xd340bc42u); // mask guest pointer to 48 bits
                }
            } else {
                // w6 is caller-saved and never destroys w3's arithmetic top.
                if (zero) {
                    emit(0x52800006u); // mov w6,#0
                } else if (!dynamic && static_offset < 4096) {
                    emit(0xb9400046u | (static_offset << 10)); // ldr w6,[x2,#imm]
                } else {
                    emit(0xb8646846u); // ldr w6,[x2,w4,uxtw]
                }
                const U32 output = next();
                if (output > 262143) return {};
                if (output < 4096) {
                    emit(0xb9000026u | (output << 10)); // str w6,[x1,#imm]
                } else {
                    imm(5, output * 4); emit(0xb8256826u);
                }
            }
        } else if (op == OpPop) {
            if (!depth) return {};
            emit(0xf94003e2u | (--depth << 10));
        } else if (op == OpImm || op == OpFlat || op == OpUserData) {
            if (count == MaxValues) return {};
            const U32 value = next();
            // Only older values spill. The last result stays in w3 until
            // it is consumed, instead of a store+load on every operation.
            if (count) store(3, count - 1);
            if (op == OpImm) imm(3, value);
            else {
                if (value > 262143 || (op==OpUserData && value>=16)) return {};
                if (value < 4096) {
                    // SRT user-data and flat-data operands are static offsets
                    // in nearly every draw: emit one LDR instead of MOV+LDR.
                    emit((op == OpUserData ? 0xb9400003u : 0xb9400023u) |
                         (value << 10)); // ldr w3,[x0/x1,#4*value]
                } else {
                    imm(5, value * 4);
                    emit(op == OpUserData ? 0xb8656803u : 0xb8656823u);
                }
            }
            ++count;
        } else if (op == OpNot) {
            if (!count) return {};
            emit(0x2a2303e3u); // mvn w3,w3; no stack traffic
        } else if (op == OpBitFieldUExtract) {
            if (count < 3) return {};
            // w3=bits, two preceding operands remain in the stack array.
            emit(0x2a0303e5u); // mov w5,w3
            --count;
            load(4, --count); load(3, count - 1);
            emit(0x1ac42463u); imm(4, 1); emit(0x1ac52084u);
            emit(0x51000484u); emit(0x0a040063u);
        } else {
            if (count < 2) return {};
            // w3=right operand, w4 receives it; load only the left.
            emit(0x2a0303e4u); // mov w4,w3
            load(3, --count - 1);
            switch (op) {
            case OpAdd: emit(0x0b040063u); break;
            case OpSub: emit(0x4b040063u); break;
            case OpMul: emit(0x1b047c63u); break;
            case OpShl: emit(0x1ac42063u); break;
            case OpShr: emit(0x1ac42463u); break;
            case OpAnd: emit(0x0a040063u); break;
            case OpOr: emit(0x2a040063u); break;
            case OpXor: emit(0x4a040063u); break;
            case OpUMin: emit(0x6b04007fu); emit(0x1a843063u); break;
            case OpUMax: emit(0x6b04007fu); emit(0x1a848063u); break;
            default: return {};
            }
        }
    }
    return {};
}
// Input must first pass Compile() validation. Loads go through the caller's
// fault/recovery boundary, so a transient invalid pointer never mutates bytecode.
template<class Reader>
void Interpret(const std::uint32_t* program, const std::uint32_t* user_data,
               std::uint32_t* flat, Reader&& read) {
    using u32=std::uint32_t; using u64=std::uint64_t; using u8=std::uint8_t;
    const u32* code=program;
    const u8* stack[MaxDepth];
    u32 values[MaxValues];
    u32 depth = 0, count = 0;
    const u8* pointer = reinterpret_cast<const u8*>(user_data);

    for (u32 pc = 1;;) {
        const u32* const at = &code[pc++];
        const u32 word = *at;
        const u32 op = word & ~OpZeroed;
        const bool zeroed = (word & OpZeroed) != 0;
        switch (op) {
        case OpEnd:
            return;
        case OpPushImm:
        case OpPushDyn: {
            const u32 offset = (op == OpPushImm ? code[pc++] : values[--count]) << 2;
            u64 next = 0;
            if (!zeroed) {
                next=read(reinterpret_cast<const void*>(reinterpret_cast<std::uintptr_t>(pointer)+offset),sizeof(u64));
            }
            stack[depth++] = pointer;
            pointer = reinterpret_cast<const u8*>(next & 0xFFFFFFFFFFFFull);
            break;
        }
        case OpPop:
            pointer = stack[--depth];
            break;
        case OpCopyImm:
        case OpCopyDyn: {
            const u32 offset = (op == OpCopyImm ? code[pc++] : values[--count]) << 2;
            u32 value = 0;
            if (!zeroed) {
                value=read(reinterpret_cast<const void*>(reinterpret_cast<std::uintptr_t>(pointer)+offset),sizeof(u32));
            }
            flat[code[pc++]] = value;
            break;
        }
        case OpUserData:
            values[count++] = user_data[code[pc++]];
            break;
        case OpFlat:
            values[count++] = flat[code[pc++]];
            break;
        case OpImm:
            values[count++] = code[pc++];
            break;
        case OpNot:
            values[count - 1] = ~values[count - 1];
            break;
        case OpBitFieldUExtract: {
            const u32 bits = values[--count];
            const u32 offset = values[--count];
            u32& base = values[count - 1];
            base = (base >> (offset & 31)) & ((1u << (bits & 31)) - 1);
            break;
        }
        default: {
            const u32 b = values[--count];
            u32& a = values[count - 1];
            switch (op) {
            case OpAdd: a += b; break;
            case OpSub: a -= b; break;
            case OpMul: a *= b; break;
            case OpShl: a <<= b & 31; break;
            case OpShr: a >>= b & 31; break;
            case OpAnd: a &= b; break;
            case OpOr: a |= b; break;
            case OpXor: a ^= b; break;
            case OpUMin: a = std::min(a, b); break;
            case OpUMax: a = std::max(a, b); break;
            default: return;
            }
            break;
        }
        }
    }
}

} // namespace Shader::Arm64Srt
