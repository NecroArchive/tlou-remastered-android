package ro.cobrabm.fexdroid;

import android.content.Context;
import android.content.SharedPreferences;
import android.os.Build;
import android.opengl.EGL14;
import android.opengl.EGLConfig;
import android.opengl.EGLContext;
import android.opengl.EGLDisplay;
import android.opengl.EGLSurface;
import android.opengl.GLES20;
import org.json.JSONObject;
import java.io.File;
import java.io.InputStream;
import java.nio.file.Files;
import java.util.Locale;

/** Install pinned, SHA-256-verified Turnip drivers for the detected Adreno generation. */
final class BuiltinDrivers {
    private static final String ASSET="drivers/turnip-v27-r2-a6xx-a7xx.zip";
    private static final String SHA="8d30be022582d0c03b1918c019540491d00439e14e9e5e1359fe0bb19004f781";
    private static final String ASSET_8XX="drivers/turnip-gen8-v39-patched-a8xx.zip";
    private static final String SHA_8XX="bd457b30f195cf93a334b5302c221b1b5cb59c79c485935decf95d560a38c8a2";

    private BuiltinDrivers() {}

    // Query an actual EGL renderer. Build.MODEL and Snapdragon model strings
    // cannot reliably distinguish Adreno 7xx from 8xx; do not guess.
    static String renderer() {
        EGLDisplay display=EGL14.eglGetDisplay(EGL14.EGL_DEFAULT_DISPLAY);
        if(display==EGL14.EGL_NO_DISPLAY)return "";
        int[] version=new int[2];
        if(!EGL14.eglInitialize(display,version,0,version,1))return "";
        EGLContext context=EGL14.EGL_NO_CONTEXT;
        EGLSurface surface=EGL14.EGL_NO_SURFACE;
        try {
            int[] attributes={
                EGL14.EGL_RENDERABLE_TYPE,EGL14.EGL_OPENGL_ES2_BIT,
                EGL14.EGL_SURFACE_TYPE,EGL14.EGL_PBUFFER_BIT,
                EGL14.EGL_RED_SIZE,8,EGL14.EGL_GREEN_SIZE,8,EGL14.EGL_BLUE_SIZE,8,
                EGL14.EGL_NONE
            };
            EGLConfig[] config=new EGLConfig[1];
            int[] count={0};
            if(!EGL14.eglChooseConfig(display,attributes,0,config,0,1,count,0) ||
               count[0]<1)return "";
            context=EGL14.eglCreateContext(display,config[0],EGL14.EGL_NO_CONTEXT,
                new int[]{EGL14.EGL_CONTEXT_CLIENT_VERSION,2,EGL14.EGL_NONE},0);
            if(context==EGL14.EGL_NO_CONTEXT)return "";
            surface=EGL14.eglCreatePbufferSurface(display,config[0],
                new int[]{EGL14.EGL_WIDTH,1,EGL14.EGL_HEIGHT,1,EGL14.EGL_NONE},0);
            if(surface==EGL14.EGL_NO_SURFACE)return "";
            if(!EGL14.eglMakeCurrent(display,surface,surface,context))return "";
            String gpu=GLES20.glGetString(GLES20.GL_RENDERER);
            return gpu==null?"":gpu;
        } finally {
            EGL14.eglMakeCurrent(display,EGL14.EGL_NO_SURFACE,
                EGL14.EGL_NO_SURFACE,EGL14.EGL_NO_CONTEXT);
            if(surface!=EGL14.EGL_NO_SURFACE)EGL14.eglDestroySurface(display,surface);
            if(context!=EGL14.EGL_NO_CONTEXT)EGL14.eglDestroyContext(display,context);
            EGL14.eglTerminate(display);
        }
    }

    static boolean isPreviousBundledDefault(Context context, SharedPreferences p) {
        final String display=p.getString("driver_name","");
        final String oldId;
        if("Turnip Gen8 V38 Patched · Adreno 8xx".equals(display))
            oldId="2bbb279a3da7680503a54a51a3051c7a4302c8fb2a089f8b183b4b5d6ba9ac17";
        else if("Turnip v27.0.0-R1 · Adreno 6xx/7xx".equals(display))
            oldId="07938ef843a73dd47ebb88ba9c2d3bca011667bd2b657e3b4a75d9ba8ed591a0";
        else return false;
        File expected=new File(new File(new File(context.getFilesDir(),"drivers"),oldId),
                               "libvulkan_freedreno.so");
        return expected.getAbsolutePath().equals(p.getString("driver_path",""));
    }

    static synchronized void ensureDefault(Context context) throws Exception {
        SharedPreferences p=PortSettings.prefs(context);
        // Always stage the packaged driver so it appears in the selector,
        // including when a user explicitly chose the system driver or imported
        // an older OneUI driver. NEVER overwrite those explicit choices.
        // Only migrate the previous version that we selected automatically.
        final boolean selectDefault=!p.contains("driver_path") ||
                                    isPreviousBundledDefault(context,p);
        final String gpu;
        try { gpu=renderer(); }
        catch(Throwable e) { return; } // unknown GPU: keep system default
        String name=gpu.toLowerCase(Locale.ROOT);
        if(!name.contains("adreno"))return;
        boolean a8=name.matches(".*\\b8[0-9]{2}\\b.*");
        boolean a67=name.matches(".*\\b[67][0-9]{2}\\b.*");
        if(!a8 && !a67)return;
        String asset=a8?ASSET_8XX:ASSET;
        String digest=a8?SHA_8XX:SHA;
        String display=a8?"Turnip Gen8 V39 Patched · Adreno 8xx":"Turnip v27.0.0-R2 · Adreno 6xx/7xx";
        File drivers=new File(context.getFilesDir(),"drivers");
        try {
            DriverArchive.Installed installed;
            File destination=new File(drivers,digest);
            if(destination.isDirectory()) {
                installed=DriverArchive.read(drivers,digest,Build.VERSION.SDK_INT);
                DriverArchive.validateLibrary(installed.library);
            } else {
                try(InputStream archive=context.getAssets().open(asset)){
                    installed=DriverArchive.install(drivers,archive,Build.VERSION.SDK_INT,file->{
                        JSONObject meta=new JSONObject(new String(Files.readAllBytes(file.toPath()),
                            java.nio.charset.StandardCharsets.UTF_8));
                        return new DriverArchive.Metadata(
                            meta.optString("name",display),
                            meta.optString("libraryName","libvulkan_freedreno.so"),
                            meta.optInt("minApi",26));
                    });
                }
            }
            if(!digest.equals(installed.id))
                throw new IllegalStateException("Bundled Turnip archive digest mismatch");
            if(selectDefault) {
                p.edit().putString("driver_path",installed.library.getAbsolutePath())
                    .putString("driver_name",display).apply();
            }
        } catch(java.io.FileNotFoundException missingAsset) {
            // Developer checkout without staged APK assets: system driver, never
            // secretly pick an incompatible one.
        }
    }
}
