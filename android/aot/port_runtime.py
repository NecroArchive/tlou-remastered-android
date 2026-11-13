#!/usr/bin/env python3
"""Port Bloodborne's strict AOT CPU ABI to the TLOU runtime.

Upstream: ducvd89/Bloodborne-Android, pinned commit
b6d4cfb6972c84d4029f54765406ec143df3c30e (GPL-2.0-or-later).
This derivative adds the PS4 fiber continuation operations required by TLOU.
It never embeds game code or modifies the player's ELF/SELF.
"""
from __future__ import annotations
import argparse
from pathlib import Path
import shutil

UPSTREAM = "b6d4cfb6972c84d4029f54765406ec143df3c30e"

FIBER_API = """
/* TLOU FEX-compatible guest fiber continuation ABI. */
#ifndef GUEST_CPU_NATIVE
typedef struct GuestFiberCpuState GuestFiberCpuState;
GuestFiberCpuState* guest_cpu_fiber_create(void);
void guest_cpu_fiber_save(GuestFiberCpuState* state);
void guest_cpu_fiber_entry(GuestFiberCpuState* state, uintptr_t entry, uintptr_t stack_top,
                          uint64_t arg0, uint64_t arg1, void* returned, int reset_fpu);
void guest_cpu_fiber_switch(GuestFiberCpuState* state, void (*release)(void*), void* previous);
void guest_cpu_fiber_dispose(GuestFiberCpuState* state);
uint64_t guest_cpu_fiber_frame_pointer(const GuestFiberCpuState* state);
#endif

"""

GLOBAL_STATE = """
// TLOU's guest-fiber ABI is an owned snapshot, not a host stack switch.
// The AOT engine may call C HLE functions which switch PS4 fibers; no AOT
// unit may continue using the old guest state when the callback returns.
struct GuestFiberCpuState { GuestState state; };

"""
PENDING = """
// Host callbacks can request a fiber change. Apply ONLY after the ARM64
// callback has returned and the saved x86-64 state is no longer in use.
thread_local GuestFiberCpuState* tlou_pending_fiber = nullptr;
thread_local void (*tlou_pending_release)(void*) = nullptr;
thread_local void* tlou_pending_previous = nullptr;

"""
HOST_RETURN = """
    if (tlou_pending_fiber) {
        GuestFiberCpuState* next = tlou_pending_fiber;
        auto release = tlou_pending_release;
        void* previous = tlou_pending_previous;
        tlou_pending_fiber = nullptr;
        tlou_pending_release = nullptr;
        tlou_pending_previous = nullptr;
        *s = next->state;
        // GuestState contains pointers to its own embedded GDT. Rebase after copy.
        s->segment_arrays[0] = s->private_gdt;
        s->segment_arrays[1] = s->private_gdt;
        if (release) release(previous);  // previous fiber can now be reclaimed
    } else {
        s->gregs[RSP] = sp + 8;
        s->rip = ret;
    }
"""
FIBER_IMPL = """
GuestFiberCpuState* guest_cpu_fiber_create(void) {
    return new GuestFiberCpuState {};
}
void guest_cpu_fiber_save(GuestFiberCpuState* target) {
    if (!target || !self || !self->depth || !self->host_depth)
        Fatal("TLOU: fiber save outside a guest->host callback");
    GuestState& state = *self->s;
    // AOT HostCall has not yet performed the guest stub's return. Save the
    // continuation *after* RET, as FEX's guest_cpu_fiber_save does.
    const uint64_t sp = state.gregs[RSP];
    target->state = state;
    target->state.gregs[RSP] = sp + 8;
    target->state.rip = ReadGuest(sp);
    target->state.gregs[RAX] = 0;
    target->state.segment_arrays[0] = target->state.private_gdt;
    target->state.segment_arrays[1] = target->state.private_gdt;
}
void guest_cpu_fiber_entry(GuestFiberCpuState* target, uintptr_t entry,
                           uintptr_t stack_top, uint64_t arg0, uint64_t arg1,
                           void* returned, int reset_fpu) {
    if (!target || !entry || !stack_top || !returned)
        Fatal("TLOU: invalid guest fiber entry");
    guest_cpu_fiber_save(target);
    GuestState& s = target->state;
    const uintptr_t sp = (stack_top & ~uintptr_t(15)) - 8;
    *reinterpret_cast<uint64_t*>(sp) = guest_cpu_host_function(returned);
    s.gregs[RSP] = sp;
    s.gregs[RBP] = 0;
    s.gregs[RDI] = arg0;
    s.gregs[RSI] = arg1;
    s.gregs[RAX] = 0;
    s.rip = entry;
    if (reset_fpu) {
        s.FCW = 0x037f;
        s.mxcsr = 0x9fc0;
    }
}
void guest_cpu_fiber_switch(GuestFiberCpuState* target, void (*release)(void*),
                            void* previous) {
    if (!target || !self || !self->depth || !self->host_depth)
        Fatal("TLOU: fiber switch outside guest callback");
    if (tlou_pending_fiber) Fatal("TLOU: nested fiber switch");
    tlou_pending_release = release;
    tlou_pending_previous = previous;
    tlou_pending_fiber = target;
}
void guest_cpu_fiber_dispose(GuestFiberCpuState* target) { delete target; }
uint64_t guest_cpu_fiber_frame_pointer(const GuestFiberCpuState* target) {
    return target ? target->state.gregs[RBP] : 0;
}

"""

TARGET_PROVENANCE = r"""
// An all-ones indirect target is NOT the AOT guest return sentinel.
// Keep the strict failure but report its ARM64 caller and guest registers.
static void TlouTraceInvalidDispatch(GuestState* s, uint64_t target,
                                     uintptr_t return_pc, const char* dispatch_kind) {
    if (target != ~uint64_t{0}) return;
    uint64_t from_unit = ~uint64_t{0};
    uintptr_t from_host = 0;
    auto at = std::upper_bound(E.by_address.begin(), E.by_address.end(), return_pc,
        [](uintptr_t address, const Engine::Where& w) { return address < w.fn; });
    if (at != E.by_address.begin()) {
        --at;
        from_unit = at->vaddr;
        from_host = at->fn;
    }
    std::fprintf(stderr,
        "TLOU AOT invalid target: via=%s target=%#llx rip=%#llx arm64_return=%#llx arm64_unit=%#llx guest_unit=%#llx rsp=%#llx rbp=%#llx\n",
        dispatch_kind, (unsigned long long)target, (unsigned long long)s->rip,
        (unsigned long long)return_pc, (unsigned long long)from_host,
        (unsigned long long)from_unit, (unsigned long long)s->gregs[RSP],
        (unsigned long long)s->gregs[RBP]);
    for (unsigned i = 0; i < 16; ++i)
        std::fprintf(stderr, "TLOU AOT gpr[%02u]=%#llx%s",
                     i, (unsigned long long)s->gregs[i], i % 4 == 3 ? "\n" : " ");
    for (unsigned i = 0; i < 8; ++i)
        std::fprintf(stderr, "TLOU AOT stack[%u] @%#llx = %#llx\n",
                     i, (unsigned long long)(s->gregs[RSP] + i * 8),
                     (unsigned long long)ReadGuest(s->gregs[RSP] + i * 8));
    if (Thread* t = self) {
        std::fprintf(stderr, "TLOU AOT depth=%u host_depth=%u trace_n=%u\n",
                     t->depth, t->host_depth, t->trace_n);
        const unsigned first = t->trace_n > Thread::TraceSize
            ? t->trace_n - Thread::TraceSize : 0;
        for (unsigned i = first; i < t->trace_n; ++i) {
            const auto& e = t->trace[i % Thread::TraceSize];
            std::fprintf(stderr,
                "TLOU AOT trace[%u] kind=%u a=%#llx b=%#llx c=%#llx\n",
                i, e.kind, (unsigned long long)e.a,
                (unsigned long long)e.b, (unsigned long long)e.c);
        }
    }
    std::fflush(stderr);
}

"""

def once(source: str, before: str, after: str, name: str) -> str:
    if source.count(before) != 1:
        raise ValueError(f"{name}: expected exactly one upstream ABI patch site; found {source.count(before)}")
    return source.replace(before, after, 1)


def port(upstream: Path, out: Path) -> None:
    cpu = upstream / "cpu"
    main_header = upstream / "src/guest_cpu.h"
    required = ("guest_cpu_aot.cpp", "guest_cpu_aot_hooks.inc", "guest_cpu_native.cpp",
                "guest_cpu_native.h", "guest_cpu_cpuid.h", "guest_cpu_atomics.h",
                "guest_state.h", "host_call_arm64.S")
    for name in required:
        if not (cpu / name).is_file():
            raise ValueError(f"upstream file missing: {name}")
    if not main_header.is_file():
        raise ValueError("Missing upstream guest_cpu.h ABI")
    out.mkdir(parents=True, exist_ok=True)
    for name in required:
        shutil.copy2(cpu / name, out / name)
    header = main_header.read_text()
    header = once(header, "#pragma GCC visibility pop", FIBER_API + "#pragma GCC visibility pop",
                  "fiber ABI declarations")
    (out / "guest_cpu.h").write_text(header)
    main = (out / "guest_cpu_aot.cpp").read_text()
    main = once(main, 'namespace {\nusing namespace bbaot;', GLOBAL_STATE + 'namespace {\nusing namespace bbaot;',
                "global guest fiber state")
    main = once(main, "thread_local Thread* self;\n", "thread_local Thread* self;\n" + PENDING,
                "per-thread fiber switch")
    host_return = "    s->gregs[RSP] = sp + 8;\n    s->rip = ret;\n"
    main = once(main, host_return, HOST_RETURN, "host callback continuation")
    main = once(main, 'void guest_cpu_set_panic_text(', FIBER_IMPL + 'void guest_cpu_set_panic_text(',
                "fiber implementation")
    # PS4 fibers can resume at the guest address following a CALL. The
    # --resume-entries units expose that address via GuestState::_pad1.
    # Bloodborne's strict CPU currently recognizes granular *jump* entries
    # only. Extend its dispatcher to recognize compiler-provided resume
    # entries as well, without interpreting arbitrary x86 instructions.
    main = once(main, "    const uint8_t* flags = nullptr;    // bbr_func_granular[]",
                "    const uint8_t* flags = nullptr;    // bbr_func_granular[]\n    bool resume = false;       // bbr_abi_flags bit 0",
                "resume capability")
    main = once(main, "    if (!G.flags) return false;",
                "    if (!G.flags && !G.resume) return false;",
                "resume dispatch prerequisite")
    main = once(main, "    if (offset >= f->end || !G.flags[f - G.funcs]) return false;",
                "    if (offset >= f->end || !(G.resume || (G.flags && G.flags[f - G.funcs]))) return false;",
                "resume range check")
    original_loader = '''    if (abi_flags && (*abi_flags & 1))
        std::fprintf(stderr, "AOT: %s has resume entries (bbr_abi_flags bit 0); this runtime does not use them\\n", path);
    if (abi_flags && (*abi_flags & 16)) {   // granular entries: bbr_func_granular[] says which functions of bbr_funcs[] have them
        G.funcs = static_cast<const GranRec*>(dlsym(so, "bbr_funcs"));
        const auto* nf = static_cast<const uint64_t*>(dlsym(so, "bbr_func_count"));
        G.flags = static_cast<const uint8_t*>(dlsym(so, "bbr_func_granular"));
        G.nfuncs = nf ? *nf : 0;
        if (!G.funcs || !G.flags || !G.nfuncs) { G.flags = nullptr; std::fprintf(stderr, "AOT: %s: granular entries declared but their tables are missing\\n", path); }
    }'''
    adapted_loader = '''    G.resume = abi_flags && (*abi_flags & 1);
    if (G.resume || (abi_flags && (*abi_flags & 16))) {
        G.funcs = static_cast<const GranRec*>(dlsym(so, "bbr_funcs"));
        const auto* nf = static_cast<const uint64_t*>(dlsym(so, "bbr_func_count"));
        G.flags = (abi_flags && (*abi_flags & 16))
            ? static_cast<const uint8_t*>(dlsym(so, "bbr_func_granular")) : nullptr;
        G.nfuncs = nf ? *nf : 0;
        if (!G.funcs || !G.nfuncs || ((abi_flags && (*abi_flags & 16)) && !G.flags)) {
            G.resume = false;
            G.flags = nullptr;
            std::fprintf(stderr, "AOT: %s: resume/granular function tables unavailable\\n", path);
        }
    }
    if (G.resume) std::printf("TLOU AOT: guest CALL return sites enabled for PS4 fiber resumes\\n");'''
    main = once(main, original_loader, adapted_loader, "PS4 fiber resume dispatch")
    main = once(main, "BBR_EXPORT void bbr_dispatch(GuestState* s, uint64_t target) {",
                TARGET_PROVENANCE + """BBR_EXPORT void bbr_dispatch(GuestState* s, uint64_t target) {
    if (target == ~uint64_t{0}) TlouTraceInvalidDispatch(s, target, (uintptr_t)__builtin_return_address(0), "call");""",
                "invalid indirect call provenance")
    main = once(main, "BBR_EXPORT void bbr_dispatch_tail(GuestState* s) {\n    const uint64_t target = s->rip;",
                """BBR_EXPORT void bbr_dispatch_tail(GuestState* s) {
    const uint64_t target = s->rip;
    if (target == ~uint64_t{0}) TlouTraceInvalidDispatch(s, target, (uintptr_t)__builtin_return_address(0), "tail");""",
                "invalid indirect tail provenance")
    (out / "guest_cpu_aot.cpp").write_text(main)
    print(f"TLOU AOT backend: upstream {UPSTREAM}, PS4 fiber ABI ported; source generated at {out}")


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--upstream", type=Path, required=True)
    p.add_argument("--out", type=Path, required=True)
    a = p.parse_args()
    port(a.upstream, a.out)
