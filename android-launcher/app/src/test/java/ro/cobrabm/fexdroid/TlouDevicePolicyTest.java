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
        e=env();
        ok("normal".equals(TlouDevicePolicy.configureMemory(e,0)),
            "unavailable RAM information must fail open");
        // 8 Gen 3 logs: native 720p raster but Vulkan allocated 2480x1116
        // AHB output. Preserve the real Android window, only shrink Vulkan
        // color/present surfaces for A750 (unless explicitly opted out).
        e=env();
        e.put("GOW3_SCREEN_W","2480"); e.put("GOW3_SCREEN_H","1116");
        ok("compact-1280x720".equals(TlouDevicePolicy.configureCompactPresent(
            e,"auto","Adreno (TM) 750","720")), "A750 should use 720p output");
        ok("1280".equals(e.get("GOW3_SCREEN_W")) &&
           "720".equals(e.get("GOW3_SCREEN_H")), "real AHB resolution 720p");
        e=env();
        e.put("GOW3_SCREEN_W","2480"); e.put("GOW3_SCREEN_H","1116");
        ok("disabled".equals(TlouDevicePolicy.configureCompactPresent(
            e,"off","Adreno 750","720")), "A/B off must retain original surface");
        ok("2480".equals(e.get("GOW3_SCREEN_W")), "opt-out preserves output");
        for(String gpu:new String[]{"Adreno 740","Adreno 730","Adreno 830",
                                    "Adreno 840","Mali-G720"}) {
            e=env();
            e.put("GOW3_SCREEN_W","2480"); e.put("GOW3_SCREEN_H","1116");
            ok("auto-not-adreno750".equals(TlouDevicePolicy.configureCompactPresent(
                e,"auto",gpu,"720")), "other GPUs must remain unchanged: "+gpu);
            ok("2480".equals(e.get("GOW3_SCREEN_W")),gpu+" output preserved");
        }
        e=env(); e.put("GOW3_SCREEN_W","2480");e.put("GOW3_SCREEN_H","1116");
        ok("compact-960x540".equals(TlouDevicePolicy.configureCompactPresent(
            e,"auto","Adreno 750","540")), "540p follows compiled AOT patch");
        e=env(); e.put("GOW3_SCREEN_W","2480");e.put("GOW3_SCREEN_H","1116");
        ok("compact-640x360".equals(TlouDevicePolicy.configureCompactPresent(
            e,"auto","Adreno 750","360")), "360p follows compiled AOT patch");
        e=env(); e.put("GOW3_SCREEN_W","2480");e.put("GOW3_SCREEN_H","1116");
        ok("compact-1920x1080".equals(TlouDevicePolicy.configureCompactPresent(
            e,"auto","Adreno 750","original")), "native raster when unpatched");
        e=env(); e.put("GOW3_SCREEN_W","2480");e.put("GOW3_SCREEN_H","1116");
        ok("unknown-guest-raster".equals(TlouDevicePolicy.configureCompactPresent(
            e,"auto","Adreno 750","unknown")), "fail open to original surface");
        e=env(); e.put("GOW3_SCREEN_W","1280");e.put("GOW3_SCREEN_H","720");
        ok("not-smaller".equals(TlouDevicePolicy.configureCompactPresent(
            e,"auto","Adreno 750","720")), "do not create larger buffers");
        e=env(); e.put("GOW3_SCREEN_W","1116");e.put("GOW3_SCREEN_H","2480");
        ok("not-smaller".equals(TlouDevicePolicy.configureCompactPresent(
            e,"auto","Adreno 750","720")), "portrait surfaces must be untouched");
        System.out.println("TlouDevicePolicyTest: PASS (A740 UBWC, A750 AHB, A6xx/A8xx unchanged, 8 GB RAM)");
    }
}
