package ro.cobrabm.fexdroid;

import java.util.HashMap;
import java.util.Map;

public final class AotInstrumentationTest {
    private static void check(boolean yes,String label) {
        if(!yes)throw new AssertionError(label);
    }
    public static void main(String[] args) {
        Map<String,String> env=new HashMap<>();
        env.put("BB_AOT_STATS","0");   // even "0" would enable instrumenting
        env.put("BB_AOT_TRACE","false"); // getenv != nullptr
        env.put("BB_AOT_MISS_LOG","/cache/real-aot-misses.log");
        env.put("BB_AOT","/cache/libtlouaot.so");
        env.put("TLOU_CPU_PHASES","1");
        AotInstrumentation.configure(env,false);
        check(!env.containsKey("BB_AOT_STATS"),"stats still on in default gameplay");
        check(!env.containsKey("BB_AOT_TRACE"),"trace still on in default gameplay");
        check("/cache/real-aot-misses.log".equals(env.get("BB_AOT_MISS_LOG")),
              "lost AOT missing entry recorder");
        check(env.containsKey("BB_AOT"),"lost CPU backend choice");
        check(env.containsKey("TLOU_CPU_PHASES"),"lost generic performance telemetry");
        AotInstrumentation.configure(env,true);
        check("1".equals(env.get("BB_AOT_STATS")),"opt-in stats not enabled");
        check("1".equals(env.get("BB_AOT_TRACE")),"opt-in trace not enabled");
        AotInstrumentation.configure(env,false);
        check(!env.containsKey("BB_AOT_STATS")&&!env.containsKey("BB_AOT_TRACE"),
              "trace not disabled after toggling off");
        System.out.println("TLOU AOT opt-in instrumentation tests passed");
    }
}
