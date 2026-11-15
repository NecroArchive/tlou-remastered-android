package ro.cobrabm.fexdroid;

import android.content.Context;
import android.content.SharedPreferences;

import org.json.JSONObject;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.robolectric.RobolectricTestRunner;
import org.robolectric.RuntimeEnvironment;

import java.io.File;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;

import static org.junit.Assert.*;

@RunWith(RobolectricTestRunner.class)
public final class TsoProfileTest {
    @Test public void versionAndNamesAreStrictlyWhitelisted() {
        assertEquals("v3",TsoProfile.normalize("unrecognized"));
        assertEquals("v3",TsoProfile.normalize(null));
        assertEquals("v3",TsoProfile.normalize("v3"));
        assertEquals("v4",TsoProfile.normalize("v4"));
        assertEquals("v5",TsoProfile.normalize("v5"));
        assertEquals("3-tlou-string",TsoProfile.version("v3"));
        assertEquals("4-tlou-tso-aligned-ar-v1",TsoProfile.version("v4"));
        assertEquals("5-tlou-tso-outlined-unaligned-v1",TsoProfile.version("v5"));
        for(String p:TsoProfile.VALUES) {
            assertEquals("aot/compilers/"+p+"/libbbrecomp.so",
                         TsoProfile.compilerRelative(p));
            assertEquals("aot/compilers/"+p+"/bundle",
                         TsoProfile.bundleRelative(p));
        }
    }

    @Test public void profilesHaveIndependentWritableWorkDirs() {
        Context context=RuntimeEnvironment.getApplication();
        SharedPreferences preferences=PortSettings.prefs(context);
        preferences.edit().putString(TsoProfile.KEY,"v4").commit();
        assertEquals("v4",TsoProfile.selected(context));
        assertNotEquals(TsoProfile.work(context,"v3"),
                        TsoProfile.work(context,"v4"));
        assertNotEquals(TsoProfile.work(context,"v5"),
                        TsoProfile.work(context,"v4"));
    }

    @Test public void matchingLegacyLibraryMigratesWithoutCopy() throws Exception {
        Context context=RuntimeEnvironment.getApplication();
        File base=context.getFilesDir();
        for(String p:TsoProfile.VALUES) {
            File old=new File(base,"tlou-aot-work");
            File dest=new File(base,"tlou-aot-work-"+p);
            // Test root is isolated per run; never remove an actual user folder.
            if(old.exists())throw new AssertionError("Unexpected legacy directory");
            if(dest.exists())throw new AssertionError("Unexpected profile directory");
            File aot=new File(old,"aot");
            assertTrue(aot.mkdirs());
            Files.write(new File(aot,"libtlouaot.json").toPath(),
                new JSONObject().put("compiler_version",TsoProfile.version(p))
                    .toString().getBytes(StandardCharsets.UTF_8));
            Files.write(new File(old,"aot-report.json").toPath(),
                        "{}".getBytes(StandardCharsets.UTF_8));
            assertEquals(dest.getCanonicalFile(),
                         TsoProfile.work(context,p).getCanonicalFile());
            assertTrue(new File(dest,"aot/libtlouaot.json").exists());
            assertFalse(old.exists());
            // Remove fixtures only after validating the resulting profile.
            assertTrue(new File(dest,"aot/libtlouaot.json").delete());
            assertTrue(new File(dest,"aot-report.json").delete());
            assertTrue(new File(dest,"aot").delete());
            assertTrue(dest.delete());
        }
    }

    @Test public void doesNotMigrateLegacyLibraryFromWrongCompiler() throws Exception {
        Context context=RuntimeEnvironment.getApplication();
        File old=new File(context.getFilesDir(),"tlou-aot-work");
        File aot=new File(old,"aot");
        if(!aot.mkdirs())throw new IllegalStateException("Old fixture exists");
        Files.write(new File(aot,"libtlouaot.json").toPath(),
            new JSONObject().put("compiler_version",TsoProfile.version("v4"))
                .toString().getBytes(StandardCharsets.UTF_8));
        Files.write(new File(old,"aot-report.json").toPath(),new byte[]{'{' ,'}'});
        assertNotEquals(old,TsoProfile.work(context,"v3"));
        assertTrue(old.exists());
        assertTrue(new File(aot,"libtlouaot.json").delete());
        assertTrue(new File(old,"aot-report.json").delete());
        assertTrue(aot.delete());
        assertTrue(old.delete());
    }
}
