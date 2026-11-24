package ro.cobrabm.fexdroid;

import java.util.HashMap;
import java.util.Map;

/** Run with javac/java independently of Android; catches device-policy regressions. */
public final class TlouDevicePolicyTest {
    private static void ok(boolean condition, String reason) {
        if(!condition) throw new AssertionError(reason);
    }
    private static Map<String,String> env() { return new HashMap<>(); }
    public static void main(String[] args) {
        Map<String,String> e=env();
        ok("enabled".equals(TlouDevicePolicy.configureUbwcHint(e,"auto",
            "Adreno (TM) 740","/data/drivers/libvulkan_freedreno.so",
            "Turnip v27 R2")), "740 Turnip must enable UBWC");
        ok("enable_tp_ubwc_flag_hint=1".equals(e.get("FD_DEV_FEATURES")),
            "exact Mesa feature syntax");
        for(String gpu:new String[]{"Adreno (TM) 730", "Adreno (TM) 830"}) {
            e=env();
            ok("auto-not-adreno740".equals(TlouDevicePolicy.configureUbwcHint(
                e,"auto",gpu,"/data/libvulkan_freedreno.so","Turnip")),
                gpu+" must not get the A740-only workaround");
            ok(!e.containsKey("FD_DEV_FEATURES"),"unexpected override on "+gpu);
        }
        e=env();
        ok("enabled".equals(TlouDevicePolicy.configureUbwcHint(e,"auto",
            "Adreno740v2","/data/libvulkan_freedreno.so","Turnip")),
            "alternative Adreno740v2 spelling");
        e=env();
        ok("not-turnip-driver".equals(TlouDevicePolicy.configureUbwcHint(e,"on",
            "Adreno (TM) 740","","Driver do sistema")),
            "never change proprietary Vulkan driver");
        e=env();
        ok("disabled".equals(TlouDevicePolicy.configureUbwcHint(e,"off",
            "Adreno (TM) 740","/data/libvulkan_freedreno.so","Turnip")),
            "explicit A/B opt-out");
        e=env();
        ok("a6xx-ubwc-off".equals(TlouDevicePolicy.configureUbwcHint(e,"on",
            "Adreno (TM) 660","/data/libvulkan_freedreno.so","Turnip")),
            "A6xx must ignore a persisted UBWC-on override");
        ok("enable_tp_ubwc_flag_hint=0".equals(e.get("FD_DEV_FEATURES")),
            "Turnip A6xx UBWC disabled at the driver");
        e=env();
        e.put("FD_DEV_FEATURES","another_feature=1,enable_tp_ubwc_flag_hint=1");
        ok("a6xx-ubwc-off".equals(TlouDevicePolicy.configureUbwcHint(e,"auto",
            "Adreno (TM) 640","/data/libvulkan_freedreno.so","Turnip")),
            "A640 replaces preexisting conflicting UBWC feature");
        ok("another_feature=1,enable_tp_ubwc_flag_hint=0".equals(e.get("FD_DEV_FEATURES")),
            "other Turnip feature overrides must remain");
        for(String gpu:new String[]{"Adreno (TM) 630","Adreno (TM) 640","Adreno (TM) 650"}) {
            e=env();
            ok("a6xx-ubwc-off".equals(TlouDevicePolicy.configureUbwcHint(e,"auto",
                gpu,"/data/libvulkan_freedreno.so","Turnip")), "A6xx auto must disable UBWC");
        }
        e=env();
        e.put("FD_DEV_FEATURES","another_feature=1");
        ok("existing-features-preserved".equals(TlouDevicePolicy.configureUbwcHint(
            e,"auto","Adreno 740","/data/libvulkan_freedreno.so","Turnip")),
            "never stomp existing Freedreno flags");
        ok("another_feature=1".equals(e.get("FD_DEV_FEATURES")),"existing env retained");

        // Every 8 GiB class gets exactly the same RAM-saving allocation
        // policy, regardless of Qualcomm/Mali generation.
        for(String gpu:new String[]{"Adreno (TM) 630","Adreno (TM) 640",
                "Adreno (TM) 650","Adreno (TM) 750","Adreno (TM) 830","Mali-G720",""}) {
            e=env();
            ok("8gb-low-memory".equals(TlouDevicePolicy.configureMemory(e,8L<<30,gpu)),
                "8 GiB memory profile missing on "+gpu);
            ok("128".equals(e.get("GOW3_STREAM_MB")), "stream budget for "+gpu);
            ok("32".equals(e.get("GOW3_STAGING_KEEP_MB")), "host staging for "+gpu);
            ok("0".equals(e.get("GOW3_STAGING_KEEP_VRAM_MB")), "GPU staging for "+gpu);
            ok("0".equals(e.get("GOW3_STAGING_LARGE_MB")), "large staging for "+gpu);
            ok("1".equals(e.get("GOW3_STAGING_PREWARM_BLOCKS")), "prewarm for "+gpu);
            ok("0".equals(e.get("GOW3_RESIDENCY_CHUNKS")), "speculative pools for "+gpu);
            ok("384".equals(e.get("GOW3_GC_HEADROOM_MB")), "image GC for "+gpu);
            ok("1".equals(e.get("GOW3_GC_LOW_RAM_EARLY")), "early GC for "+gpu);
            ok("12".equals(e.get("GOW3_ARENA_RECLAIM_AGE_S")), "arena idle for "+gpu);
            ok("90".equals(e.get("GOW3_STAGING_IDLE_FRAMES")), "staging idle for "+gpu);
            ok("32".equals(e.get("GOW3_STAGING_IDLE_LIMIT_MB")), "staging cap for "+gpu);
            ok("16".equals(e.get("GOW3_DENSE_ARENA_PAGE_MB")), "dense pages for "+gpu);
            ok("768".equals(e.get("GOW3_ARENA_LIMIT_MB")), "resident arena cap for "+gpu);
            ok(!e.containsKey("GOW3_ARENA_PAGE_MB"), "A6xx-only sparse cap in RAM profile "+gpu);
            ok(!e.containsKey("GOW3_DMEM_MB") && !e.containsKey("GOW3_DMEM_REPORT_MB"),
                "never modify PS4 guest memory for "+gpu);
        }
        // The 7xx/8xx must keep every RAM/GPU override absent at 12+ GiB.
        for(long ram:new long[]{12L<<30,16L<<30}) {
            for(String gpu:new String[]{"Adreno (TM) 730","Adreno (TM) 750",
                    "Adreno (TM) 830","Adreno (TM) 840","Mali-G720"}) {
                e=env();
                ok("normal".equals(TlouDevicePolicy.configureMemory(e,ram,gpu)),
                    "12/16 GiB memory policy must be off: "+gpu);
                ok("unchanged".equals(TlouDevicePolicy.configureA6xxCompatibility(e,gpu)),
                    "A6xx crash flags must not leak to "+gpu);
                ok(e.isEmpty(), "12/16 GiB A7xx/A8xx must have NO RAM/crash overrides: "+gpu);
            }
        }
        // A6xx crash/compatibility workarounds are independent of RAM size.
        for(long ram:new long[]{8L<<30,12L<<30,16L<<30}) {
            for(String gpu:new String[]{"Adreno (TM) 630","Adreno (TM) 640",
                    "Adreno (TM) 650","Adreno (TM) 660"}) {
                e=env();
                TlouDevicePolicy.configureMemory(e,ram,gpu);
                ok("a6xx-crash-compat".equals(
                    TlouDevicePolicy.configureA6xxCompatibility(e,gpu)), "all A6xx: "+gpu);
                ok("512".equals(e.get("GOW3_ARENA_PAGE_MB")), "A6xx sparse limit "+gpu);
                ok("0".equals(e.get("GOW3_RESIDENCY_CHUNKS")), "A6xx pre-allocation off "+gpu);
                if(ram>=12L<<30) {
                    ok(!e.containsKey("GOW3_DENSE_ARENA_PAGE_MB"), "A6xx 12+ RAM limit leaked");
                    ok(!e.containsKey("GOW3_GC_LOW_RAM_EARLY"), "A6xx 12+ GC limit leaked");
                    ok(!e.containsKey("GOW3_STAGING_IDLE_FRAMES"), "A6xx 12+ staging limit leaked");
                }
            }
        }
        e=env();
        ok("normal".equals(TlouDevicePolicy.configureMemory(e,0)),
            "unavailable RAM information must fail open");
        ok(TlouDevicePolicy.isOryonSoc("SM8750"),"8 Elite Oryon");
        ok(TlouDevicePolicy.isOryonSoc("SM8850"),"Elite Gen 5 Oryon");
        ok(TlouDevicePolicy.isOryonSoc("Snapdragon Oryon"),"Oryon name");
        ok(!TlouDevicePolicy.isOryonSoc("SM8650"),"8 Gen 3 is not Oryon");
        ok(!TlouDevicePolicy.isOryonSoc("SM8475"),"older Snapdragon not Oryon");
        for(String gpu:new String[]{"Adreno 750","Adreno 830","Adreno 840","Mali-G720"}) {
            e=env();e.put("GOW3_SCREEN_W","2480");e.put("GOW3_SCREEN_H","1116");
            ok("fixed-1280x720".equals(TlouDevicePolicy.configureCompactPresent(e)),
                "720p output independent of GPU: "+gpu);
            ok("1280".equals(e.get("GOW3_SCREEN_W")) &&
               "720".equals(e.get("GOW3_SCREEN_H")), "exact output size");
        }
        e=env();e.put("GOW3_SCREEN_W","1280");e.put("GOW3_SCREEN_H","720");
        ok("fixed-1280x720".equals(TlouDevicePolicy.configureCompactPresent(e)),
            "already compact");
        e=env();e.put("GOW3_SCREEN_W","960");e.put("GOW3_SCREEN_H","540");
        ok("display-smaller-than-720p".equals(TlouDevicePolicy.configureCompactPresent(e)),
            "do not upscale smaller physical screen");
        e=env();e.put("GOW3_SCREEN_W","1116");e.put("GOW3_SCREEN_H","2480");
        ok("display-smaller-than-720p".equals(TlouDevicePolicy.configureCompactPresent(e)),
            "portrait display unchanged");
        System.out.println("TlouDevicePolicyTest: PASS (A740 UBWC, 720p all SoCs, Oryon automatic, A6xx/A8xx memory, 8 GB RAM)");
    }
}
