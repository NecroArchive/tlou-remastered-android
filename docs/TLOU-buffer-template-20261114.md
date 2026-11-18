# Log 13: descriptor payload pointer correction

## Reproduced source defect

The original call in `30febfc` used:

```cpp
cmdbuf.pushDescriptorSetWithTemplateKHR(update, layout, 0, buffers.data());
```

Vulkan-Hpp has both a raw `const void*` overload and an enhanced
`const DataType&` overload. A typed `const DescriptorBufferInfo*` selects the
latter. Its implementation forwards `&data`: the address of the pointer argument,
not the array of descriptor infos. The driver therefore interprets unrelated
stack bytes as buffer handles/offsets/ranges. This is a concrete source defect,
not an established Turnip driver bug.

A test using the real Vulkan-Hpp header and a capture dispatch function reproduces
the incorrect forwarded address. The shared production helper forces
`static_cast<const void*>(buffers.data())` and verifies exact pointer and byte
identity for 100000 changing payloads. The real software-Vulkan execution test
now calls this same C++ helper instead of bypassing it through the C API. That
was the missing coverage in the original tests.

`477b765` temporarily withdrew the feature while the cause was investigated.
The follow-up reinstates it with the corrected C++ call and tests on both native
ARM64 and x86-64 before GPU compilation/APK packaging. No rendering recovery on
the user's physical device has yet been confirmed.

## User evidence

`TLOU-Android-Logs (13).zip` contains two runs on Adreno 840 with Turnip Gen8 V39
Patched, continued flips near 100-120/s and no fatal Vulkan device-loss message.
The user reports a missing image. These flip counts are not evidence of correct
rendering or improved FPS. The handled AOT/readback fault traces are not by
themselves a fatal process crash; the latest launcher reports termination 143.

## Previous candidate evidence (not a device validation)


Base: `cb8cb0c338a20b5b03fd562a6a464a69e6cbd5fb`
(`fix/tlou-aot-log12-pid-20261113`).

The candidate removes per-dispatch `VkWriteDescriptorSet` snapshots and pointer
relocations for compute layouts with at least four storage-buffer bindings and
no images or samplers. An immutable Vulkan descriptor-update template describes
the layout. Every dispatch still copies its current buffer handles, offsets and
ranges into its recording chunk. No guest data, resource lookup, readback,
barrier, dispatch, submission or `SyncRecording()` wait is removed or reused.

The old bulk-descriptor experiment is not enabled. This candidate also replaces
the driver's repeated decoding of `VkWriteDescriptorSet` with a prebuilt update
template. Two-buffer layouts stay on the original path because the local driver
benchmark showed a regression there. Unsupported template entry points, failed
template creation, extension chains, descriptor arrays, mismatched source spans
and noncanonical bindings retain the original path.

## Local checks

Actual Vulkan recording on Mesa llvmpipe 25.0.7 / LLVM 19.1.7, Vulkan 1.4.305,
x86-64 host. Alternating old/new trials, two warmups and forty measured samples;
2048 bindings per sample. Numbers are medians in ns per binding operation.

| Buffers | Old snapshot | New snapshot including guard | Old driver | New driver | Total reduction |
|---:|---:|---:|---:|---:|---:|
| 2 | 8.1 | 6.3 | 148.8 | 165.6 | regression; excluded |
| 4 | 11.8 | 10.2 | 241.3 | 160.0 | 32.8% |
| 8 | 18.5 | 12.2 | 362.6 | 224.3 | 37.9% |
| 16 | 36.0 | 21.3 | 788.5 | 335.8 | 56.7% |

The new path also avoids copying 256, 512 or 1024 bytes of write structures per
eligible 4-, 8- or 16-buffer dispatch. Vulkan driver command allocation is
included; guest work, queue handoff and the game's full rendering workload are
not. This is not a measured TLOU FPS improvement, nor an Adreno/Turnip result.
The actual distribution of eligible binding counts has not been measured.

GPU execution checks: four compiled compute programs, 128 changing-offset/range
dispatches through each descriptor API per program, exact output comparison.
All passed. A separate validation-layer run produced no VUID/validation errors.
Production fallback checks and 100000 changing payloads passed ASan/UBSan
(LeakSanitizer disabled locally because the runtime uses ptrace).

## Historical CI release gate (removed after regression)

The candidate experimental AOT workflow ran guard tests, exact GPU checks with validation,
and the real-driver cost benchmark on x86-64 and native ARM64. The 16-buffer case
had to save at least 5% total measured time before the GPU build started. The
ordinary Bionic renderer cross-compilation then validates the production C++.
APK packaging retains the existing AOT package and pinned signing certificate.
The FEX stable branch is not changed.

## Log continuity

The available file named `TLOU-Android-Logs (12)(2).zip` is an older AOT startup
failure, not the gameplay ZIP 12 cited in the previous chat. The latest accessible
logs 9 and 10 independently show recording waits in the same broad range
(roughly 566-770 ms per five-second gameplay interval in log 10). The quoted
765 ms number has not been reverified from the exact cited ZIP in this session.
