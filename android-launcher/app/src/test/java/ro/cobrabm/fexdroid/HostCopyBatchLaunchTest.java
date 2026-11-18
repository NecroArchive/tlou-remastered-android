package ro.cobrabm.fexdroid;

import android.content.Context;
import org.json.JSONObject;
import org.json.JSONArray;
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
    @Test public void fullVariantBankPermitsAllPatchSwitchesWithoutRecompilation()
            throws Exception {
        JSONArray groups=new JSONArray();
        for(String resolution:new String[]{"720","540","360"})
            groups.put("resolution_"+resolution);
        for(String effect:PortSettings.EFFECTS)
            groups.put("effect_"+effect);
        JSONObject bank=new JSONObject()
            .put("profile","tlou-111-variants-v3")
            .put("sha256","a".repeat(64))
            .put("groups",groups);
        JSONObject source=new JSONObject()
            .put("version","1.11")
            .put("native_patch_profile","tlou-111-variants-v3")
            .put("variant_catalog",bank);
        assertTrue(RuntimeAot.hasCompleteVariantCatalog(source));
        JSONObject prefs=new JSONObject()
            .put("resolution","original");
        for(String effect:PortSettings.EFFECTS)prefs.put(effect,false);
        for(String resolution:new String[]{"original","720","540","360"}) {
            prefs.put("resolution",resolution);
            for(String effect:PortSettings.EFFECTS) {
                prefs.put(effect,true);
                assertTrue("all 11 variants required for "+effect+" at "+resolution,
                    RuntimeAot.hasCompleteVariantCatalog(source));
                prefs.put(effect,false);
            }
        }
        // Reject a library which lacks ANY optional native effect; it would
        // silently ignore the corresponding setting in ZIP 28.
        groups.remove(groups.length()-1);
        assertFalse(RuntimeAot.hasCompleteVariantCatalog(source));
        source.remove("variant_catalog");
        assertFalse(RuntimeAot.hasCompleteVariantCatalog(source));
    }

    @Test public void zip28NewShadowsCannotSilentlyRemainUnapplied() throws Exception {
        // Real ZIP 28 01.11 case: only these two switches changed after
        // the user's AOT image was compiled. Previously JOGAR silently
        // launched a .so with both effects still enabled.
        JSONObject built=new JSONObject()
            .put("resolution","720")
            .put("ssao",true)
            .put("motion_blur",true)
            .put("depth_of_field",true)
            .put("bloom",true)
            .put("sunlight_shadows",true)
            .put("subsurface",true)
            .put("omni_light_shadows",false)
            .put("spot_light_shadows",false);
        JSONObject chosen=new JSONObject(built.toString())
            .put("omni_light_shadows",true)
            .put("spot_light_shadows",true);
        assertEquals("omni_light_shadows, spot_light_shadows",
            RuntimeAot.pendingCodePatches(built,chosen));
        assertEquals("",RuntimeAot.pendingCodePatches(chosen,chosen));
        assertEquals("",RuntimeAot.pendingCodePatches(built,built));
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
