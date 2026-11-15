package ro.cobrabm.fexdroid;

import android.content.Context;
import android.content.SharedPreferences;
import java.io.File;

/**
 * The PS4 guest TSO memory model is always enabled.
 * Only the safe AOT lowering implementation is selectable.
 *
 * Codegen happens at compilation time, not at gameplay runtime. Native units
 * and resume shards must never be shared across compiler versions.
 */
final class TsoProfile {
    static final String KEY="aot_tso_profile";
    static final String BASELINE="v3", ALIGNED="v4", OUTLINED="v5";
    static final String[] VALUES={BASELINE,ALIGNED,OUTLINED};
    static final String[] LABELS={
        "TSO v3 — conservador (DMB)",
        "TSO v4 — LDAR/STLR + fallback completo",
        "TSO v5 — LDAR/STLR + fallback compartilhado"
    };
    private TsoProfile() {}

    static String normalize(String value) {
        return ALIGNED.equals(value)?ALIGNED:OUTLINED.equals(value)?OUTLINED:BASELINE;
    }
    static String selected(Context context) {
        SharedPreferences prefs=PortSettings.prefs(context);
        return normalize(prefs.getString(KEY,BASELINE));
    }
    static String version(String profile) {
        switch(normalize(profile)) {
            case ALIGNED: return "4-tlou-tso-aligned-ar-v1";
            case OUTLINED: return "5-tlou-tso-outlined-unaligned-v1";
            default: return "3-tlou-string";
        }
    }
    static File work(Context context) {
        return work(context,selected(context));
    }
    static File work(Context context,String profile) {
        return new File(context.getFilesDir(),"tlou-aot-work-"+normalize(profile));
    }
    static String compilerRelative(String profile) {
        return "aot/compilers/"+normalize(profile)+"/libbbrecomp.so";
    }
    static String bundleRelative(String profile) {
        return "aot/compilers/"+normalize(profile)+"/bundle";
    }
}
