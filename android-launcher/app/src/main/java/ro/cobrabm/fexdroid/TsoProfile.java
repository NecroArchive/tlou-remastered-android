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
        String valid=normalize(profile);
        File destination=new File(context.getFilesDir(),"tlou-aot-work-"+valid);
        // Previous experimental APKs stored one large AOT image at this
        // legacy path. Rename the WHOLE cache once, only if the compiler
        // metadata matches the selected profile. Never copy GB of shards.
        File legacy=new File(context.getFilesDir(),"tlou-aot-work");
        if(!destination.exists() && legacy.isDirectory()) {
            File metadata=new File(legacy,"aot/libtlouaot.json");
            if(metadata.isFile() && new File(legacy,"aot-report.json").isFile()) {
                try {
                    org.json.JSONObject info=new org.json.JSONObject(
                        new String(java.nio.file.Files.readAllBytes(metadata.toPath()),
                            java.nio.charset.StandardCharsets.UTF_8));
                    if(version(valid).equals(info.optString("compiler_version")))
                        legacy.renameTo(destination);
                } catch(Exception ignored) {
                    // Keep the legacy library untouched if it is incomplete.
                    // Explicit recompilation creates the new profile directory.
                }
            }
        }
        return destination;
    }
    static String compilerRelative(String profile) {
        return "aot/compilers/"+normalize(profile)+"/libbbrecomp.so";
    }
    static String bundleRelative(String profile) {
        return "aot/compilers/"+normalize(profile)+"/bundle";
    }
}
