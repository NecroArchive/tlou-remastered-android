package ro.cobrabm.fexdroid;

import java.util.Locale;
import java.util.Map;

/** Launch-time device compatibility only; no guest patches or per-frame work. */
final class TlouDevicePolicy {
    private static final long GIB = 1024L * 1024L * 1024L;

    private TlouDevicePolicy() {}

    /** Oryon cores: Snapdragon 8 Elite / Elite Gen 5; exclude 8 Gen 3 and older. */
    static boolean isOryonSoc(String soc) {
        if(soc==null)return false;
        String s=soc.toLowerCase(Locale.ROOT);
        return s.contains("oryon") || s.contains("sm8750") ||
               s.contains("sm8850") || s.contains("8 elite");
    }

    /** Return a status for logging; never set Freedreno-only flags on the system driver. */
    static String configureUbwcHint(Map<String,String> env, String mode,
                                    String renderer, String driverPath, String driverName) {
        String name = driverName == null ? "" : driverName.toLowerCase(Locale.ROOT);
        String path = driverPath == null ? "" : driverPath.toLowerCase(Locale.ROOT);
        String gpu = renderer == null ? "" : renderer.toLowerCase(Locale.ROOT);
        boolean turnip = !path.isEmpty() &&
            (path.endsWith("libvulkan_freedreno.so") || name.contains("turnip"));
        if (!turnip) return "not-turnip-driver";
        // A6xx/Turnip: forcing this compositor UBWC hint produced visible
        // scanlines through gameplay AND the Android HUD on Adreno 640.
        // Do not honor a persisted "on" selection on this GPU generation.
        if (gpu.matches(".*adreno[^0-9]*6[0-9]{2}([^0-9]|$).*")) {
            final String feature="enable_tp_ubwc_flag_hint=";
            String existing=env.get("FD_DEV_FEATURES");
            if(existing==null || existing.isEmpty()) {
                env.put("FD_DEV_FEATURES", feature+"0");
            } else if(existing.contains(feature)) {
                env.put("FD_DEV_FEATURES", existing.replaceAll(
                    "enable_tp_ubwc_flag_hint=[^,:;\\s]+", feature+"0"));
            } else {
                // Preserve unrelated Mesa overrides.
                env.put("FD_DEV_FEATURES", existing+",enable_tp_ubwc_flag_hint=0");
            }
            return "a6xx-ubwc-off";
        }
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

    /**
     * Release 0.2.0 fixed 720p Android presentation across all SoCs.
     * Native Android SurfaceControl scales this output to the physical screen,
     * while guest raster/patched resolution remain independent.
     */
    static String configureCompactPresent(Map<String,String> env) {
        final int targetW=1280,targetH=720;
        try {
            int currentW=Integer.parseInt(env.get("GOW3_SCREEN_W"));
            int currentH=Integer.parseInt(env.get("GOW3_SCREEN_H"));
            if(currentW<currentH || currentW<targetW || currentH<targetH)
                return "display-smaller-than-720p";
        } catch(RuntimeException ex) {return "display-size-unavailable";}
        env.put("GOW3_SCREEN_W",Integer.toString(targetW));
        env.put("GOW3_SCREEN_H",Integer.toString(targetH));
        return "fixed-1280x720";
    }

    static boolean isEightGiBClass(long totalMemBytes) {
        // 8 GB Android devices typically report 7.2-8.0 GiB physical RAM.
        // Fail open on missing information; never alter 12/16 GiB devices.
        return totalMemBytes > 4L * GIB && totalMemBytes <= 9L * GIB;
    }

    /** Reserve less *actual* GPU memory while leaving the virtual PS4 pool unchanged. */
    static String configureMemory(Map<String,String> env, long totalMemBytes) {
        return configureMemory(env,totalMemBytes,"");
    }
    static String configureMemory(Map<String,String> env, long totalMemBytes,
                                  String eglRenderer) {
        if (!isEightGiBClass(totalMemBytes)) return "normal";
        // Standard 8 GB profile: no guest memory contract changes.
        env.put("GOW3_STREAM_MB", "384");
        env.put("GOW3_STAGING_KEEP_MB", "128");
        env.put("GOW3_STAGING_KEEP_VRAM_MB", "16");
        env.put("GOW3_STAGING_LARGE_MB", "64");
        env.put("GOW3_RESIDENCY_CHUNKS", "0");
        // K20 Pro/Adreno 640: log 2026-11-18 03:16 reports
        // VK_ERROR_OUT_OF_DEVICE_MEMORY during world-core bootstrap.
        // The Turnip heap is shared with system RAM (5625 MiB reported).
        // Shrink speculative/pinned GPU allocations, NOT guest PS4 memory,
        // readbacks, Vulkan synchronization or patch compilation.
        final String gpu=eglRenderer==null?"":eglRenderer.toLowerCase(Locale.ROOT);
        final boolean a6xx=gpu.matches(".*adreno[^0-9]*6[0-9]{2}([^0-9]|$).*");
        if(a6xx) {
            env.put("GOW3_STREAM_MB","128");
            env.put("GOW3_STAGING_KEEP_MB","32");
            env.put("GOW3_STAGING_KEEP_VRAM_MB","0");
            env.put("GOW3_STAGING_LARGE_MB","0");
            env.put("GOW3_STAGING_PREWARM_BLOCKS","1");
            // Leave real room for buffers when image GC sees a shrinking
            // shared Turnip heap, rather than collecting only at OOM.
            env.put("GOW3_GC_HEADROOM_MB","384");
            // On A640 the critical LRU age of 160 frames blocked texture
            // eviction while GPU usage exceeded the live heap budget.
            env.put("GOW3_GC_A6XX_EARLY","1");
            env.put("GOW3_ARENA_RECLAIM_AGE_S","12");
            // Drain idle Vulkan staging objects after a few seconds instead
            // of holding them for up to 3000 frames on an 8 GiB Adreno.
            env.put("GOW3_STAGING_IDLE_FRAMES","90");
            env.put("GOW3_STAGING_IDLE_LIMIT_MB","32");
            // Turnip/KGSL reserves virtual BOs using GPUOBJ_ALLOC. The 1 GiB page
            // setting still merged into 2 GiB BOs and failed during world-core startup
            // (runtime 2026-11-19 02:28, vkCreateBuffer ErrorOutOfDeviceMemory).
            // 512 MiB pages bound the largest adjacent-page merge to 1 GiB.
            // This changes GPU virtual buffers, NEVER the PS4 direct-memory pool.
            env.put("GOW3_ARENA_PAGE_MB","512");
            // Reclaim inactive physical arena memory on the existing safe GPU-fence path.
            env.put("GOW3_ARENA_LIMIT_MB","768");
            return "8gb-a6xx-low-memory";
        }
        return "8gb-balanced";
    }
}
