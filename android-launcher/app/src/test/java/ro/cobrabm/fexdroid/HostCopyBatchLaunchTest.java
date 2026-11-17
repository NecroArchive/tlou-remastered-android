package ro.cobrabm.fexdroid;

import android.content.Context;
import org.json.JSONObject;
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
                .putString("aot_tso_profile",tso).commit();
        Map<String,String> env=new HashMap<>();
        // An external default must not silently override the launcher policy.
        env.put("GOW3_TLOU_HOST_COPY_BATCH","0");
        PortSettings.environment(context,env);
        assertEquals("off",TsoProfile.selected(context));
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
                .putString("aot_tso_profile","off").commit();
        PortSettings.environment(c,env);
        assertTrue(PortSettings.experimentReport(c,env).contains(
                "host-copy-batching=0"));
    }
    @Test public void changingStaticVisualPatchesDoesNotInvalidateExistingAot() throws Exception {
        JSONObject compiled=new JSONObject()
                .put("resolution","720").put("ssao",true)
                .put("motion_blur",true).put("omni_light_shadows",true);
        JSONObject requested=new JSONObject()
                .put("resolution","540").put("ssao",true)
                .put("motion_blur",false).put("omni_light_shadows",false);
        assertEquals("resolution, motion_blur, omni_light_shadows",
                RuntimeAot.pendingCodePatches(compiled,requested));
        assertEquals("",RuntimeAot.pendingCodePatches(compiled,compiled));
    }

}
