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
        for(String gpu:new String[]{"Adreno (TM) 630","Adreno (TM) 650",
                                    "Adreno (TM) 660","Adreno (TM) 730",
                                    "Adreno (TM) 830"}) {
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
        ok("enabled".equals(TlouDevicePolicy.configureUbwcHint(e,"on",
            "Adreno (TM) 660","/data/libvulkan_freedreno.so","Turnip")),
            "manual A/B override remains possible");
        e=env();
        e.put("FD_DEV_FEATURES","another_feature=1");
        ok("existing-features-preserved".equals(TlouDevicePolicy.configureUbwcHint(
            e,"auto","Adreno 740","/data/libvulkan_freedreno.so","Turnip")),
            "never stomp existing Freedreno flags");
        ok("another_feature=1".equals(e.get("FD_DEV_FEATURES")),"existing env retained");

        e=env();
        ok("normal".equals(TlouDevicePolicy.configureMemory(e,12L<<30)),
            "12 GB devices must not be altered");
        ok(e.isEmpty(),"no changes on 12 GB");
        e=env();
        ok("8gb-balanced".equals(TlouDevicePolicy.configureMemory(e,8L<<30)),
            "8 GB memory preset must activate");
        ok("384".equals(e.get("GOW3_STREAM_MB")), "stream ring");
        ok("128".equals(e.get("GOW3_STAGING_KEEP_MB")), "host staging cache");
        ok("64".equals(e.get("GOW3_STAGING_LARGE_MB")), "large scratch buffer");
        ok("0".equals(e.get("GOW3_RESIDENCY_CHUNKS")), "speculative arena allocations");
        ok(!e.containsKey("GOW3_DMEM_MB") && !e.containsKey("GOW3_DMEM_REPORT_MB"),
            "must preserve PS4 virtual memory contract");
        ok(!e.containsKey("GOW3_STAGING_PREWARM_BLOCKS"),
            "normal 8 GB profile keeps its original staging prewarm");
        e=env();
        ok("8gb-a6xx-low-memory".equals(TlouDevicePolicy.configureMemory(
            e,7500L<<20,"Adreno (TM) 640")), "K20 Pro profile must activate");
        ok("256".equals(e.get("GOW3_STREAM_MB")), "A640 stream memory cap");
        ok("64".equals(e.get("GOW3_STAGING_KEEP_MB")), "A640 staging retention");
        ok("0".equals(e.get("GOW3_STAGING_KEEP_VRAM_MB")), "A640 idle VRAM");
        ok("0".equals(e.get("GOW3_STAGING_LARGE_MB")), "no A640 pinned scratch");
        ok("2".equals(e.get("GOW3_STAGING_PREWARM_BLOCKS")), "two 16 MiB prewarm blocks");
        ok(!e.containsKey("GOW3_DMEM_MB") && !e.containsKey("GOW3_DMEM_REPORT_MB"),
            "A640 must keep guest PS4 virtual memory unchanged");
        for(String gpu:new String[]{"Adreno (TM) 750","Adreno (TM) 830","Mali-G720"}) {
            e=env();
            ok("8gb-balanced".equals(TlouDevicePolicy.configureMemory(
                e,8L<<30,gpu)), "non-A6xx must preserve its profile: "+gpu);
            ok("384".equals(e.get("GOW3_STREAM_MB")), gpu+" streaming unchanged");
        }
        e=env();
        ok("normal".equals(TlouDevicePolicy.configureMemory(
            e,12L<<30,"Adreno (TM) 640")), "12 GB A6xx keeps normal profile");
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
