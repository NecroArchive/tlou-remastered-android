package ro.cobrabm.fexdroid;

import android.content.Context;
import android.content.SharedPreferences;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.robolectric.RobolectricTestRunner;
import org.robolectric.RuntimeEnvironment;
import org.robolectric.annotation.Config;
import java.util.HashMap;
import java.util.Map;

import static org.junit.Assert.*;

@RunWith(RobolectricTestRunner.class)
@Config(sdk=33)
public final class HostCopyBatchLaunchTest {
    private static String launch(Context context, String backend, String tso) {
        SharedPreferences p=PortSettings.prefs(context);
        p.edit().putString("cpu_backend",backend)
                .putString(TsoProfile.KEY,tso).commit();
        Map<String,String> env=new HashMap<>();
        // An external default must not silently override the launcher policy.
        env.put("GOW3_TLOU_HOST_COPY_BATCH","0");
        PortSettings.environment(context,env);
        return env.get("GOW3_TLOU_HOST_COPY_BATCH");
    }

    @Test public void doNotForceUnproductiveBatchingInGameplay() {
        Context c=RuntimeEnvironment.getApplication();
        assertEquals("0",launch(c,"aot","off"));
        assertEquals("0",launch(c,"aot","v3"));
        assertEquals("0",launch(c,"aot","v4"));
        assertEquals("0",launch(c,"aot","v5"));
        assertEquals("0",launch(c,"fex","off"));
        assertEquals("0",launch(c,"fex","v3"));
    }

    @Test public void reportContainsEffectiveLaunchFlag() {
        Context c=RuntimeEnvironment.getApplication();
        Map<String,String> env=new HashMap<>();
        PortSettings.prefs(c).edit().putString("cpu_backend","aot")
                .putString(TsoProfile.KEY,"off").commit();
        PortSettings.environment(c,env);
        assertTrue(PortSettings.experimentReport(c,env).contains(
                "host-copy-batching=0"));
    }
}
