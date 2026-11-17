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

    /**
     * Adreno 750 A/B: the logs render at 1280x720 but allocate AHB frames at
     * 2480x1116. Native SurfaceControl already scales the AHB source rect
     * into the REAL Android window, so this only reduces the Vulkan output
     * surface (and blit/present traffic), never the guest raster or AOT patches.
     *
     * No driver-selection changes. Other GPUs retain original dimensions.
     */
    static String configureCompactPresent(Map<String,String> env,
            String mode, String eglRenderer, String effectiveGameResolution) {
        if ("off".equals(mode)) return "disabled";
        String gpu = eglRenderer == null ? "" : eglRenderer.toLowerCase(Locale.ROOT);
        boolean a750 = gpu.contains("adreno") && gpu.matches(".*750([^0-9]|$).*");
        if (!a750 && !"on".equals(mode)) return "auto-not-adreno750";
        final String resolution = effectiveGameResolution == null ? "" :
            effectiveGameResolution.toLowerCase(Locale.ROOT);
        final String size;
        switch(resolution) {
            case "720": size = "1280x720"; break;
            case "540": size = "960x540"; break;
            case "360": size = "640x360"; break;
            case "original": size = "1920x1080"; break;
            default: return "unknown-guest-raster";
        }
        String[] wh=size.split("x");
        int wantW=Integer.parseInt(wh[0]), wantH=Integer.parseInt(wh[1]);
        try {
            int currentW=Integer.parseInt(env.get("GOW3_SCREEN_W"));
            int currentH=Integer.parseInt(env.get("GOW3_SCREEN_H"));
            // Never grow the presentation buffer or change orientation.
            if(currentW<currentH || currentW<=wantW || currentH<=wantH)
                return "not-smaller";
        } catch(RuntimeException ex) {
            return "display-size-unavailable";
        }
        env.put("GOW3_SCREEN_W",Integer.toString(wantW));
        env.put("GOW3_SCREEN_H",Integer.toString(wantH));
        return "compact-"+size;
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
            env.put("GOW3_STREAM_MB","256");
            env.put("GOW3_STAGING_KEEP_MB","64");
            env.put("GOW3_STAGING_KEEP_VRAM_MB","0");
            env.put("GOW3_STAGING_LARGE_MB","0");
            env.put("GOW3_STAGING_PREWARM_BLOCKS","2");
            return "8gb-a6xx-low-memory";
        }
        return "8gb-balanced";
    }
}
