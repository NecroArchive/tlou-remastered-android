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
        System.out.println("TlouDevicePolicyTest: PASS (A740 UBWC, A6xx untouched, 8 GB RAM)");
    }
}
