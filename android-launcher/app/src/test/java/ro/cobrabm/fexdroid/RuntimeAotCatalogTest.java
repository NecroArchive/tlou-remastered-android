package ro.cobrabm.fexdroid;

import org.json.JSONArray;
import org.json.JSONObject;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.robolectric.RobolectricTestRunner;
import org.robolectric.annotation.Config;
import static org.junit.Assert.*;

/** No incomplete or legacy 01.00 AOT library may boot with BBPATCH2 code writes. */
@RunWith(RobolectricTestRunner.class)
@Config(sdk=30,application=android.app.Application.class)
public class RuntimeAotCatalogTest {
    private JSONObject catalog(String version) throws Exception {
        final boolean v100="1.00".equals(version);
        final String profile=v100?"tlou-100-variants-v1":"tlou-111-variants-v3";
        JSONArray groups=new JSONArray();
        if(v100)for(String base:new String[]{
            "base_null_component_transforms","base_static_ray_hit_submesh_bounds",
            "base_frame_timeout","base_job_sleep"})groups.put(base);
        for(String resolution:new String[]{"720","540","360"})
            groups.put("resolution_"+resolution);
        for(String effect:new String[]{"ssao","motion_blur","depth_of_field",
                "bloom","sunlight_shadows","subsurface"})
            groups.put("effect_"+effect);
        if(!v100)for(String extra:new String[]{
            "omni_light_shadows","spot_light_shadows"})
            groups.put("effect_"+extra);
        JSONObject variant=new JSONObject().put("profile",profile)
            .put("sha256","a".repeat(64)).put("groups",groups);
        return new JSONObject().put("version",version)
            .put("native_patch_profile",profile).put("variant_catalog",variant);
    }

    @Test public void acceptsAll100NativeVariantGroups() throws Exception {
        JSONObject valid=catalog("1.00");
        assertEquals(13,valid.getJSONObject("variant_catalog").getJSONArray("groups").length());
        assertTrue(RuntimeAot.hasCompleteVariantCatalog(valid));
    }

    @Test public void rejectsMissingMandatory100BaseVariants() throws Exception {
        JSONObject incomplete=catalog("1.00");
        JSONArray groups=incomplete.getJSONObject("variant_catalog").getJSONArray("groups");
        JSONArray removed=new JSONArray();
        for(int i=0;i<groups.length();i++)
            if(!"base_job_sleep".equals(groups.getString(i)))
                removed.put(groups.getString(i));
        incomplete.getJSONObject("variant_catalog").put("groups",removed);
        assertFalse(RuntimeAot.hasCompleteVariantCatalog(incomplete));
    }

    @Test public void refusesOld100LibrariesWithoutCatalog() throws Exception {
        JSONObject old=catalog("1.00");
        old.put("variant_catalog",JSONObject.NULL);
        assertFalse(RuntimeAot.hasCompleteVariantCatalog(old));
    }

    @Test public void acceptsVerified100MidfunctionCallEntry() throws Exception {
        JSONObject report=catalog("1.00");
        report.put("verified_100_entry_policy","tlou-100-batched-call-hotspots-v2");
        report.put("granular_policy","tlou-100-batched-call-hotspots-v1");
        report.put("verified_100_midcall_entries",new JSONArray()
            .put("+0x910580"));
        report.put("extra_entry_count",1);
        report.put("granular_risk_hint_count",151560);
        report.put("entry_check_completed",true);
        report.put("resume_enabled",true);
        report.put("translation_command",new JSONArray()
            .put("--variants").put("all.txt")
            .put("--resume")
            .put("--extra-entries").put("call-targets.txt")
            .put("--granular-funcs").put("granular.txt")
            .put("--check-entries").put("910580"));
        assertTrue(RuntimeAot.hasVerified100MidCall(report));

        report.put("resume_enabled",false);
        assertFalse(RuntimeAot.hasVerified100MidCall(report));
        report.put("resume_enabled",true);
        report.put("granular_risk_hint_count",5);
        assertFalse(RuntimeAot.hasVerified100MidCall(report));
        report.put("granular_risk_hint_count",151560);
        report.put("resume_enabled",false);
        report.put("full_rebuild_100",true);
        assertTrue(RuntimeAot.hasVerified100MidCall(report));
        report.put("full_rebuild_100",false);
        assertFalse(RuntimeAot.hasVerified100MidCall(report));
        report.put("resume_enabled",true);
        report.put("verified_100_entry_policy","tlou-100-midcall-910580-v1");
        assertFalse(RuntimeAot.hasVerified100MidCall(report));
    }

    @Test public void rejectsUnverified100MidcallEvenWithAllPatchVariants() throws Exception {
        JSONObject report=catalog("1.00");
        assertTrue(RuntimeAot.hasCompleteVariantCatalog(report));
        assertFalse(RuntimeAot.hasVerified100MidCall(report));
        report.put("verified_100_entry_policy","tlou-100-batched-call-hotspots-v2");
        report.put("granular_policy","tlou-100-batched-call-hotspots-v1");
        report.put("resume_enabled",true);
        report.put("entry_check_completed",true);
        report.put("verified_100_midcall_entries",new JSONArray()
            .put("+0x910580"));
        report.put("extra_entry_count",1);
        report.put("granular_risk_hint_count",151560);
        report.put("translation_command",new JSONArray()
            .put("--extra-entries").put("target-list.txt")
            .put("--granular-funcs").put("risk-list.txt")
            .put("--check-entries").put("946180"));
        assertFalse(RuntimeAot.hasVerified100MidCall(report));
    }

    @Test public void keepsAll111VariantContract() throws Exception {
        assertTrue(RuntimeAot.hasCompleteVariantCatalog(catalog("1.11")));
        JSONObject wrong=catalog("1.11");
        wrong.put("native_patch_profile","tlou-100-variants-v1");
        assertFalse(RuntimeAot.hasCompleteVariantCatalog(wrong));
    }
}
