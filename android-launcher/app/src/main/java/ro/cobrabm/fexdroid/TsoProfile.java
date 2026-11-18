package ro.cobrabm.fexdroid;

import android.content.Context;
import java.io.File;
import org.json.JSONObject;

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

    // The pre-v0.2.0.1 AOT directory belongs to 1.11. Never move, erase
    // or rewrite its existing compiled game while preparing 1.00.
    static File work(Context context) {
        return new File(context.getFilesDir(),"tlou-aot-work-off");
    }

    static String gameCode(JSONObject marker) throws java.io.IOException {
        if(marker==null)throw new java.io.IOException(
            "Versão do jogo não identificada. Prepare o jogo antes de compilar.");
        // A separately installed PS4 update has a base-game APP_VER of
        // 01.00 but an effective update_version of 01.11. Resolve the
        // *effective* version, not merely the base param.sfo version.
        String update=marker.optString("update_version","");
        String gameVersion=update.isEmpty() || "null".equalsIgnoreCase(update)
            ? marker.optString("version","") : update;
        if("01.00".equals(gameVersion)||"1.00".equals(gameVersion))
            return "100";
        if("01.11".equals(gameVersion)||"1.11".equals(gameVersion))
            return "111";
        throw new java.io.IOException("Versão do jogo não suportada pelo AOT: "+gameVersion);
    }

    /** Runtime and compiler policy: the prepared effective game must be 1.11.
     * A 1.00 base with a valid 1.11 update overlay resolves to "111".
     * Never delete existing 1.11 AOT data when refusing a 1.00 image.
     */
    static void requireSupported111(File prepared) throws Exception {
        if(!"111".equals(gameCode(GameInstallState.marker(prepared))))
            throw new java.io.IOException(
                "The Last of Us Remastered 1.00 não é suportado nesta versão. "+
                "Atualize o jogo para 1.11 para jogar.");
    }

    static File workForPrepared(Context context,File prepared) throws Exception {
        String game=gameCode(GameInstallState.marker(prepared));
        // Only 1.00 gets a new directory. 1.11 reuses its original
        // directory so upgrades do not force recompilation of a valid 1.11.
        return "100".equals(game) ?
            new File(context.getFilesDir(),"tlou-aot-work-off-100") :
            work(context);
    }

    static File missesForPrepared(Context context,File prepared) throws Exception {
        return new File(context.getFilesDir(),
            "tlou/aot-misses-"+gameCode(GameInstallState.marker(prepared))+".log");
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
