package ro.cobrabm.fexdroid;

import android.content.Context;
import java.io.File;

/**
 * Internal AOT codegen identity. The shipping Android app has exactly one
 * ARM64 compilation policy: native relaxed guest memory, no --tso.
 * Keep a distinct cache path from older AOT profile libraries so an APK
 * upgrade cannot mistakenly launch code compiled with the old model.
 */
final class TsoProfile {
    static final String RELAXED="off";
    private static final String COMPILER_VERSION="3-tlou-string";

    private TsoProfile() {}

    static String selected(Context context) {
        // Intentionally ignore legacy aot_tso_profile preferences.
        return RELAXED;
    }

    static String normalize(String ignored) { return RELAXED; }

    static String version(String ignored) { return COMPILER_VERSION; }

    static File work(Context context) {
        return new File(context.getFilesDir(),"tlou-aot-work-off");
    }

    static File work(Context context,String ignored) { return work(context); }

    static String compilerRelative(String ignored) {
        // One signed, pinned v3 backend. AOT frontend explicitly omits --tso.
        return "bin/libbbrecomp.so";
    }

    static String bundleRelative(String ignored) {
        return "aot/bundle";
    }
}
