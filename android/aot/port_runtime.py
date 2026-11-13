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
        // FS/GS are TLS bases of the EXECUTING OS thread, not fiber-owned
        // state. A fiber is often prepared by the main thread and later runs
        // on NdJobWorkerThread N; copying its saved fs/gs would make the worker
        // read the creator's TLS and return worker-id -1 at guest 0xc89bd0.
        const uint64_t worker_fs = s->fs_cached;
        const uint64_t worker_gs = s->gs_cached;
        const uint64_t saved_fiber_fs = next->state.fs_cached;
        const uint64_t saved_fiber_gs = next->state.gs_cached;
        *s = next->state;
        s->fs_cached = worker_fs;
        s->gs_cached = worker_gs;
        // GuestState also contains pointers to its own embedded GDT.
        s->segment_arrays[0] = s->private_gdt;
        s->segment_arrays[1] = s->private_gdt;
        // Sample only initial switches per thread. This proves the fix
        // without flooding the runtime with hundreds of lines per boot.
        static thread_local unsigned tlou_tls_switch_samples = 0;
        if (tlou_tls_switch_samples++ < 3) {
            std::fprintf(stderr,
                "TLOU AOT fiber TLS preserved: tid=%ld fiber_fs=%#llx fiber_gs=%#llx thread_fs=%#llx thread_gs=%#llx final_fs=%#llx final_gs=%#llx worker_id=%#llx\\n",
                (long)syscall(SYS_gettid),
                (unsigned long long)saved_fiber_fs,
                (unsigned long long)saved_fiber_gs,
                (unsigned long long)worker_fs,
                (unsigned long long)worker_gs,
                (unsigned long long)s->fs_cached,
                (unsigned long long)s->gs_cached,
                (unsigned long long)((worker_gs >= 0x88) ? ReadGuest(worker_gs - 0x88) : 0));
            std::fflush(stderr);
        }
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


AOT_HOST_STACK_POLICY = r"""
// Strict ARM64 units execute x86 calls on the host C++ call stack. The pinned
// executable allocates only 64 KiB guest + 256 KiB margin = 320 KiB pthread
// stacks for NdJobWorkerThread/DCLoader. A native unit at +0xca4d40 faulted
// at a *host-high* page (0x70231e0ff0) with SEGV_ACCERR while working on
// core. Grow only this known short stack when BB_AOT is enabled; keep FEX,
// GPU and other Android pthread stack configurations untouched.
//
// The pinned probe calls pthread_attr_setstacksize@LIBC through a dynamic
// relocation. Preloading this AOT engine installs this wrapper without
// rebuilding or modifying the signed stable runtime executable.
BBR_EXPORT int pthread_attr_setstacksize(pthread_attr_t* attr, size_t requested) {
    using Real = int (*)(pthread_attr_t*, size_t);
    static Real real = reinterpret_cast<Real>(dlsym(RTLD_NEXT, "pthread_attr_setstacksize"));
    if (!real) return EINVAL;
    const char* aot = std::getenv("BB_AOT");
    const bool enabled = aot && *aot && std::strcmp(aot, "0") != 0;
    constexpr size_t TinyGuestHostStack = (64u + 256u) * 1024u;
    // Two 2026-11-13 DCLoader SIGSEGVs landed 16/32 bytes inside the
    // 16 KiB guard after exhausting the previous 2 MiB pthread stack.
    // 4 MiB is applied ONLY to the exact 320 KiB PS4 guest-thread request
    // when BB_AOT is active; all FEX, GPU and explicitly sized threads stay unchanged.
    constexpr size_t SafeAotHostStack = 4u * 1024u * 1024u;
    const bool adjusted = enabled && requested == TinyGuestHostStack;
    const size_t actual = adjusted ? SafeAotHostStack : requested;
    const int result = real(attr, actual);
    if (adjusted) {
        static std::atomic<unsigned> reported{0};
        const unsigned n = reported.fetch_add(1, std::memory_order_relaxed);
        if (n < 12) {
            std::fprintf(stderr,
                "TLOU AOT host pthread stack: requested=%zu applied=%zu error=%d tid=%ld\n",
                requested, actual, result, (long)syscall(SYS_gettid));
            std::fflush(stderr);
        }
    }
    return result;
}
"""

AOT_SIGNAL_STACK = r"""
// Native DCLoader workers can use 320 KiB host pthread stacks. A stack
// overflow may prevent Linux from delivering SIGSEGV to the existing handler,
// explaining exit 139 without a "TLOU AOT fault entry" line. Register an
// alternate stack on EACH thread before its first guest execution.
static thread_local void* tlou_signal_stack = nullptr;
static constexpr size_t TlouSignalStackSize = 256u * 1024u;
static thread_local bool tlou_host_stack_sampled = false;
static thread_local uintptr_t tlou_host_stack_low = 0;
static thread_local uintptr_t tlou_host_stack_high = 0;
static thread_local size_t tlou_host_stack_guard = 0;

static void TlouEnsureSignalStack() {
    if (!tlou_host_stack_sampled) {
        tlou_host_stack_sampled = true;
        pthread_attr_t attributes{};
        if (pthread_getattr_np(pthread_self(), &attributes) == 0) {
            void* address = nullptr;
            size_t length = 0;
            if (pthread_attr_getstack(&attributes, &address, &length) == 0) {
                tlou_host_stack_low = reinterpret_cast<uintptr_t>(address);
                tlou_host_stack_high = tlou_host_stack_low + length;
            }
            (void)pthread_attr_getguardsize(&attributes, &tlou_host_stack_guard);
            pthread_attr_destroy(&attributes);
        }
    }
    stack_t previous{};
    const int query = sigaltstack(nullptr, &previous);
    if (query == 0 && (previous.ss_flags & SS_DISABLE)) {
        void* memory = mmap(nullptr, TlouSignalStackSize,
                            PROT_READ | PROT_WRITE,
                            MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (memory != MAP_FAILED) {
            stack_t alternate{};
            alternate.ss_sp = memory;
            alternate.ss_size = TlouSignalStackSize;
            if (sigaltstack(&alternate, nullptr) == 0)
                tlou_signal_stack = memory;
            else
                munmap(memory, TlouSignalStackSize);
        }
    }
    // src/probe.c registers fault() after guest_cpu_init and before the
    // first guest_cpu_call. Change its SA_ONSTACK flag, NOT the function.
    // GPU write-watch faults still flow through the original recovery logic.
    static std::once_flag once;
    std::call_once(once, [] {
        unsigned active = 0;
        for (int signal : {SIGSEGV, SIGBUS, SIGILL}) {
            struct sigaction action{};
            if (sigaction(signal, nullptr, &action) != 0) continue;
            if (!(action.sa_flags & SA_SIGINFO) || !action.sa_sigaction) continue;
            if (action.sa_flags & SA_ONSTACK) { ++active; continue; }
            action.sa_flags |= SA_ONSTACK;
            if (sigaction(signal, &action, nullptr) == 0) ++active;
        }
        std::fprintf(stderr,
            "TLOU AOT signal handling: SA_ONSTACK=%u/3, existing handler preserved\n",
            active);
        std::fflush(stderr);
    });
    static std::atomic<unsigned> samples{0};
    if (samples.fetch_add(1, std::memory_order_relaxed) < 8) {
        stack_t current{};
        const int status = sigaltstack(nullptr, &current);
        std::fprintf(stderr,
            "TLOU AOT signal altstack: tid=%ld status=%d enabled=%d size=%zu owned=%d\n",
            (long)syscall(SYS_gettid), status,
            status == 0 && !(current.ss_flags & SS_DISABLE),
            status == 0 ? current.ss_size : size_t(0),
            tlou_signal_stack != nullptr);
        std::fflush(stderr);
    }
}

static void TlouReleaseSignalStack() {
    if (!tlou_signal_stack) return;
    stack_t disabled{};
    disabled.ss_flags = SS_DISABLE;
    if (sigaltstack(&disabled, nullptr) == 0)
        munmap(tlou_signal_stack, TlouSignalStackSize);
    tlou_signal_stack = nullptr;
}
"""

FATAL_SIGNAL_PROBE = r"""
// Runs first in src/probe.c's existing SIGSEGV/SIGBUS handler, before
// GPU write-watch recovery. Never replace the renderer's signal handler.
// Fixed-size stack buffer and write(2), not printf/allocator on a signal.
static void TlouAotProbeFault(int sig, const siginfo_t* info, const void* uc) {
    static std::atomic<uint32_t> count{0};
    const uint32_t n = count.fetch_add(1, std::memory_order_relaxed) + 1;
    if (n > 48 && n % 128 != 0) return;
    const uintptr_t pc = bbnative::HostPc(uc);
    const uint64_t addr = uint64_t(uintptr_t(info ? info->si_addr : nullptr));
    const Thread* t = self;
    const uint64_t rip = t && t->s ? t->s->rip : 0;
    const uint64_t rsp = t && t->s ? t->s->gregs[RSP] : 0;
    const uintptr_t host_sp = bbnative::HostSp(uc);
    // si_code=SEGV_ACCERR at the bottom of a small host pthread stack
    // is distinguishable from an invalid low guest address or GPU page.
    const bool near_host_stack_guard = tlou_host_stack_low &&
        addr <= tlou_host_stack_low &&
        tlou_host_stack_low - addr <= 65536;
    char line[450];
    char* out = line;
    const char* const end = line + sizeof(line) - 1;
    auto add = [&](const char* word) {
        for (; *word && out < end; ++word) *out++ = *word;
    };
    auto hex = [&](uint64_t value) {
        static const char digits[] = "0123456789abcdef";
        if (out + 18 >= end) return;
        *out++ = '0'; *out++ = 'x';
        for (unsigned i = 0; i < 16; ++i)
            *out++ = digits[(value >> ((15-i)*4)) & 15];
    };
    add("TLOU AOT fault entry: n="); hex(n);
    add(" sig="); hex(uint64_t(sig));
    add(" tid="); hex(uint64_t(syscall(SYS_gettid)));
    add(" si_code="); hex(uint64_t(info ? info->si_code : 0));
    add(" fault_addr="); hex(addr);
    add(" host_pc="); hex(pc);
    add(" guest_rip="); hex(rip);
    add(" rsp="); hex(rsp);
    add(" in_aot="); hex(pc >= E.so_lo && pc < E.so_hi ? 1 : 0);
    add(" host_sp="); hex(host_sp);
    add(" host_stack_lo="); hex(tlou_host_stack_low);
    add(" host_stack_hi="); hex(tlou_host_stack_high);
    add(" guard_bytes="); hex(tlou_host_stack_guard);
    add(" near_guard="); hex(near_host_stack_guard ? 1 : 0);
    *out++ = '\n';
    (void)write(STDERR_FILENO, line, size_t(out - line));
}
"""

WORKER_ENTRY_TRACE = r"""
    // The 1.11 binary has two distinct job-loop entrypoints: 0xc6a0a0
    // explicitly registers a numbered worker in TLS, while 0xc6a050 does not.
    // Do NOT assign an invented ID: record which one runs and its real TLS.
    if (count == 1 && args &&
        (fn == E.base + 0xc6a050 || fn == E.base + 0xc6a0a0)) {
        const uint64_t raw_id = s.gs_cached >= 0x88
            ? ReadGuest(s.gs_cached - 0x88) : 0;
        std::fprintf(stderr,
            "TLOU AOT job thread entry: tid=%ld entry=%#llx arg=%#llx gs=%#llx tls_raw=%#llx\n",
            (long)syscall(SYS_gettid), (unsigned long long)(fn - E.base),
            (unsigned long long)args[0], (unsigned long long)s.gs_cached,
            (unsigned long long)raw_id);
        std::fflush(stderr);
    }
"""

FIBER_RUN_TRACE = r"""
// At 01.11 x86 offset c82869, StartFiberExecution calls sceFiberRun.
// c828a3 is a deliberate INT 41 after sceFiberRun returned nonzero.
// Diagnose the HLE result and fiber state without modifying the call.
static void TlouTraceFiberRun(const GuestState* s, void* fn, uint64_t ret, bool after) {
    if (ret != E.base + 0xc8286e) return;
    const uint64_t fiber = s->gregs[RDI];
    const uint64_t header = fiber ? ReadGuest(fiber) : 0;
    const uint64_t entry = fiber ? ReadGuest(fiber + 8) : 0;
    const uint64_t magic_end = fiber ? ReadGuest(fiber + 0x68) : 0;
    const uint32_t sig_start = uint32_t(header);
    const uint32_t state = uint32_t(header >> 32);
    const uint32_t sig_end = uint32_t(magic_end);
    std::fprintf(stderr,
        "TLOU AOT sceFiberRun %s: tid=%ld fn=%p fiber=%#llx magic=%#x/%#x state=%u "
        "entry=%#llx stack=%#llx size=%#llx incoming=%#llx outgoing=%#llx result=%#x\n",
        after ? "return" : "enter", (long)syscall(SYS_gettid), fn,
        (unsigned long long)fiber, sig_start, sig_end, state,
        (unsigned long long)entry,
        (unsigned long long)(fiber ? ReadGuest(fiber + 0x18) : 0),
        (unsigned long long)(fiber ? ReadGuest(fiber + 0x20) : 0),
        (unsigned long long)s->gregs[RSI],
        (unsigned long long)s->gregs[RDX],
        after ? uint32_t(s->gregs[RAX]) : 0u);
    std::fflush(stderr);
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
    // The crashing CALL reads its callback from the job queue. The original
    // x86-64 code at c88375 reads [scheduler + (worker_id<<7) + 0x780];
    // print the actual queued values, including the dedicated id=-1 lane.
    const uint64_t scheduler = ReadGuest(E.base + 0x1aa4780);
    if (scheduler > 0x10000 && scheduler < (uint64_t{1} << 44)) {
        std::fprintf(stderr, "TLOU AOT queue state: scheduler=%#llx flag58=%#llx active4f50=%#llx\n",
            (unsigned long long)scheduler,
            (unsigned long long)ReadGuest(scheduler + 0x58),
            (unsigned long long)ReadGuest(scheduler + 0x4f50));
        for (int idx = -1; idx <= 7; ++idx) {
            const uint64_t lane = scheduler + 0x780 + intptr_t(idx) * 0x80;
            std::fprintf(stderr, "TLOU AOT queue lane[%d] addr=%#llx callback=%#llx arg=%#llx\n",
                idx, (unsigned long long)lane, (unsigned long long)ReadGuest(lane),
                (unsigned long long)ReadGuest(lane + 8));
        }
    }
    for (unsigned i = 0; i < 16; ++i)
        std::fprintf(stderr, "TLOU AOT gpr[%02u]=%#llx%s",
                     i, (unsigned long long)s->gregs[i], i % 4 == 3 ? "\n" : " ");
    for (unsigned i = 0; i < 8; ++i)
        std::fprintf(stderr, "TLOU AOT stack[%u] @%#llx = %#llx\n",
                     i, (unsigned long long)(s->gregs[RSP] + i * 8),
                     (unsigned long long)ReadGuest(s->gregs[RSP] + i * 8));
    if (s->gs_cached >= 0x88 && s->gs_cached < (uint64_t{1} << 40)) {
        std::fprintf(stderr,
            "TLOU AOT TLS at crash: fs=%#llx gs=%#llx id_slot=%#llx id_value=%#llx\n",
            (unsigned long long)s->fs_cached, (unsigned long long)s->gs_cached,
            (unsigned long long)(s->gs_cached - 0x88),
            (unsigned long long)ReadGuest(s->gs_cached - 0x88));
    }
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
    main = once(main, 'namespace {\nusing namespace bbaot;', AOT_HOST_STACK_POLICY + GLOBAL_STATE + 'namespace {\nusing namespace bbaot;',
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
    main = once(main, "    s.rip = fn;\n    bbr_dispatch(&s, fn);",
                "    s.rip = fn;\n" + WORKER_ENTRY_TRACE + "    bbr_dispatch(&s, fn);",
                "TLOU 1.11 AOT job worker entry provenance")
    main = once(main,
                "int guest_cpu_handle_fault(int sig, siginfo_t* info, void* ucontext) {",
                "int guest_cpu_handle_fault(int sig, siginfo_t* info, void* ucontext) {\n"
                "    TlouAotProbeFault(sig, info, ucontext);\n",
                "AOT fault pretrace before GPU recovery")
    main = once(main,
                "int guest_cpu_handle_fault(int sig, siginfo_t* info, void* ucontext) {",
                FATAL_SIGNAL_PROBE + "int guest_cpu_handle_fault(int sig, siginfo_t* info, void* ucontext) {",
                "AOT fault recorder definition")
    main = once(main, "uintptr_t guest_cpu_host_function(void* fn) {",
                AOT_SIGNAL_STACK + "uintptr_t guest_cpu_host_function(void* fn) {",
                "per-thread alternate signal stack definition")
    main = once(main,
                "    Thread* t = Current();\n    GuestState& s = *t->s;",
                "    Thread* t = Current();\n    TlouEnsureSignalStack();\n    GuestState& s = *t->s;",
                "register altstack before AOT guest code executes")
    main = once(main,
                "void guest_cpu_thread_end(void) {\n    Thread* t = self;",
                "void guest_cpu_thread_end(void) {\n    TlouReleaseSignalStack();\n    Thread* t = self;",
                "restore signal stack at thread end")
    main = once(main,
                "inline void HostCall(GuestState* s, void* fn, Thread* t) {",
                FIBER_RUN_TRACE + "inline void HostCall(GuestState* s, void* fn, Thread* t) {",
                "TLOU sceFiberRun trace helper definition")
    main = once(main,
                "    Trace(t, EvHost, reinterpret_cast<uint64_t>(fn), sp, ret);\n",
                "    Trace(t, EvHost, reinterpret_cast<uint64_t>(fn), sp, ret);\n"
                "    TlouTraceFiberRun(s, fn, ret, false);\n",
                "TLOU fiber run HLE entry diagnostics")
    main = once(main,
                "    CallHostFn(s, fn);\n    --t->host_depth;\n",
                "    CallHostFn(s, fn);\n"
                "    TlouTraceFiberRun(s, fn, ret, true);\n"
                "    --t->host_depth;\n",
                "TLOU fiber run HLE return diagnostics")
    (out / "guest_cpu_aot.cpp").write_text(main)
    print(f"TLOU AOT backend: upstream {UPSTREAM}, PS4 fiber ABI ported; source generated at {out}")


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--upstream", type=Path, required=True)
    p.add_argument("--out", type=Path, required=True)
    a = p.parse_args()
    port(a.upstream, a.out)
