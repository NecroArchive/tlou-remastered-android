package ro.cobrabm.fexdroid;

import java.util.Locale;
import java.util.Map;

/** Launch-time device compatibility only; no guest patches or per-frame work. */
final class TlouDevicePolicy {
    private static final long GIB = 1024L * 1024L * 1024L;

    private TlouDevicePolicy() {}

    /** Return a status for logging; never set Freedreno-only flags on the system driver. */
    static String configureUbwcHint(Map<String,String> env, String mode,
                                    String renderer, String driverPath, String driverName) {
        String name = driverName == null ? "" : driverName.toLowerCase(Locale.ROOT);
        String path = driverPath == null ? "" : driverPath.toLowerCase(Locale.ROOT);
        String gpu = renderer == null ? "" : renderer.toLowerCase(Locale.ROOT);
        boolean turnip = !path.isEmpty() &&
            (path.endsWith("libvulkan_freedreno.so") || name.contains("turnip"));
        if (!turnip) return "not-turnip-driver";
        if ("off".equals(mode)) return "disabled";
        boolean adreno740 = gpu.contains("adreno") && gpu.matches(".*740([^0-9]|$).*");
        if (!"on".equals(mode) && !adreno740) return "auto-not-adreno740";
        String previous = env.get("FD_DEV_FEATURES");
        if (previous != null && !previous.isEmpty()) {
            // Do not overwrite caller-supplied Freedreno overrides.
            return previous.contains("enable_tp_ubwc_flag_hint=") ?
                "already-defined" : "existing-features-preserved";
        }
        env.put("FD_DEV_FEATURES", "enable_tp_ubwc_flag_hint=1");
        return "enabled";
    }

    static boolean isEightGiBClass(long totalMemBytes) {
        // 8 GB Android devices typically report 7.2-8.0 GiB physical RAM.
        // Fail open on missing information; never alter 12/16 GiB devices.
        return totalMemBytes > 4L * GIB && totalMemBytes <= 9L * GIB;
    }

    /** Reserve less *actual* GPU memory while leaving the virtual PS4 pool unchanged. */
    static String configureMemory(Map<String,String> env, long totalMemBytes) {
        if (!isEightGiBClass(totalMemBytes)) return "normal";
        // Streaming 512 -> 384 MiB: fewer buffers but still 3x the 128 MiB
        // ring proven to stall under 2-frame load. Render correctness unchanged.
        env.put("GOW3_STREAM_MB", "384");
        // These are cache retention limits, not guest allocation limits.
        env.put("GOW3_STAGING_KEEP_MB", "128");
        env.put("GOW3_STAGING_KEEP_VRAM_MB", "16");
        // Retain one preallocated large staging copy to limit GPU stalls.
        env.put("GOW3_STAGING_LARGE_MB", "64");
        // Avoid speculative 64 MiB x 2 Vulkan arena chunks.
        env.put("GOW3_RESIDENCY_CHUNKS", "0");
        return "8gb-balanced";
    }
}
