package ro.cobrabm.fexdroid;

import android.content.Context;
import android.content.SharedPreferences;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.robolectric.RobolectricTestRunner;
import org.robolectric.RuntimeEnvironment;
import org.robolectric.annotation.Config;
import java.io.File;
import static org.junit.Assert.*;

@RunWith(RobolectricTestRunner.class)
@Config(sdk=33)
public final class TsoProfileTest {
    @Test public void legacyPrefsCannotReEnableOldCodegenProfiles() {
        Context context=RuntimeEnvironment.getApplication();
        SharedPreferences prefs=PortSettings.prefs(context);
        for(String old:new String[]{"v3","v4","v5","off","invalid"}) {
            prefs.edit().putString("aot_tso_profile",old).commit();
            assertEquals("off",TsoProfile.selected(context));
            assertEquals("off",TsoProfile.normalize(old));
            assertEquals("3-tlou-string",TsoProfile.version(old));
            assertEquals("bin/libbbrecomp.so",TsoProfile.compilerRelative(old));
            assertEquals("aot/bundle",TsoProfile.bundleRelative(old));
        }
    }
    @Test public void onlySeparateNonTsoCacheIsUsed() {
        Context context=RuntimeEnvironment.getApplication();
        File expected=new File(context.getFilesDir(),"tlou-aot-work-off");
        assertEquals(expected,TsoProfile.work(context));
        assertEquals(expected,TsoProfile.work(context,"v3"));
        assertEquals(expected,TsoProfile.work(context,"v4"));
        assertEquals(expected,TsoProfile.work(context,"v5"));
        assertNotEquals(new File(context.getFilesDir(),"tlou-aot-work"),
                        TsoProfile.work(context));
        assertNotEquals(new File(context.getFilesDir(),"tlou-aot-work-v3"),
                        TsoProfile.work(context));
        assertEquals("off",TsoProfile.RELAXED);
    }
}
