package ro.cobrabm.fexdroid;

import java.util.Map;

/** In the strict ARM64 runtime, every guest->native callback is expensive
 * to instrument. Detailed counters and a trace ring are opt-in only. The
 * always-on fatal fault recorder, CPU thread sampler and AOT miss logger
 * have independent controls and are intentionally preserved.
 */
final class AotInstrumentation {
    private AotInstrumentation() {}
    static void configure(Map<String,String> env,boolean detailed) {
        // getenv() != nullptr enables AOT's counters, even when value="0".
        // Erase *inherited* keys, never assign a false-looking string.
        env.remove("BB_AOT_STATS");
        env.remove("BB_AOT_TRACE");
        if(detailed) {
            env.put("BB_AOT_STATS","1");
            env.put("BB_AOT_TRACE","1");
        }
    }
}
