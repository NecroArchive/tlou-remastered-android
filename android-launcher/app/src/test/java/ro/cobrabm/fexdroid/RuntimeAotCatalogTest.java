package ro.cobrabm.fexdroid;

import org.json.JSONArray;
import org.json.JSONObject;
import org.junit.Test;
import static org.junit.Assert.*;

/** No incomplete or legacy 01.00 AOT library may boot with BBPATCH2 code writes. */
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

    @Test public void keepsAll111VariantContract() throws Exception {
        assertTrue(RuntimeAot.hasCompleteVariantCatalog(catalog("1.11")));
        JSONObject wrong=catalog("1.11");
        wrong.put("native_patch_profile","tlou-100-variants-v1");
        assertFalse(RuntimeAot.hasCompleteVariantCatalog(wrong));
    }
}
