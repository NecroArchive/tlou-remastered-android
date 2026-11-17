package ro.cobrabm.fexdroid;

import android.app.Application;
import android.content.SharedPreferences;
import java.io.File;
import java.nio.file.Files;
import java.nio.charset.StandardCharsets;
import org.json.JSONObject;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.robolectric.RobolectricTestRunner;
import org.robolectric.RuntimeEnvironment;
import org.robolectric.annotation.Config;
import static org.junit.Assert.*;

@RunWith(RobolectricTestRunner.class)
@Config(sdk=30,application=Application.class)
public class GameInstallStateTest {
    private File game(File root,String name) throws Exception {
        File base=new File(root,name);
        File sfo=new File(base,"sce_sys/param.sfo");
        sfo.getParentFile().mkdirs();
        Files.write(sfo.toPath(),new byte[]{1});
        Files.write(new File(base,"eboot.bin").toPath(),new byte[]{2});
        return base.getCanonicalFile();
    }
    private File update(File root) throws Exception {
        File dir=new File(root,"TLOU-Update-1.11");
        File sfo=new File(dir,"sce_sys/param.sfo");
        sfo.getParentFile().mkdirs();
        Files.write(sfo.toPath(),new byte[]{1});
        return dir.getCanonicalFile();
    }
    private void marker(File prepared,File game,File update) throws Exception {
        prepared.mkdirs();
        for(String name:new String[]{"boot-linked.bin","content.bin"})
            Files.write(new File(prepared,name).toPath(),new byte[]{1});
        new File(prepared,"tlou-view").mkdirs();
        JSONObject object=new JSONObject();
        object.put("game",game.getCanonicalPath());
        object.put("update",update.getCanonicalPath());
        object.put("update_version","01.11");
        Files.write(new File(prepared,"device-ready.json").toPath(),
                    object.toString().getBytes(StandardCharsets.UTF_8));
    }
    @Test public void previousPreparedFolderRecoversUpdateAfterApkUpgrade() throws Exception {
        File root=Files.createTempDirectory("tlou-update-retain").toFile();
        File base=game(root,"CUSA00556"), patch=update(root), prepared=new File(root,"private/tlou-prepared");
        marker(prepared,base,patch);
        SharedPreferences prefs=RuntimeEnvironment.getApplication().getSharedPreferences("tlou-update-test",0);
        prefs.edit().clear().commit();
        assertEquals(base,GameInstallState.base(prepared));
        assertEquals(patch,GameInstallState.update(base,prepared,prepared,prefs));
        assertEquals(base,GameInstallState.base(base));
        assertEquals(patch,GameInstallState.update(base,base,prepared,prefs));
        // APP upgrade MUST NOT mutate original game or update content.
        assertEquals(2,Files.readAllBytes(new File(base,"eboot.bin").toPath())[0]);
    }
    @Test public void missingUpdateMustNotSilentlyBootBaseVersion() throws Exception {
        File root=Files.createTempDirectory("tlou-update-missing").toFile();
        File base=game(root,"CUSA00556"), patch=update(root), prepared=new File(root,"tlou-prepared");
        marker(prepared,base,patch);
        Files.delete(new File(patch,"sce_sys/param.sfo").toPath());
        SharedPreferences prefs=RuntimeEnvironment.getApplication().getSharedPreferences("tlou-update-missing",0);
        prefs.edit().clear().commit();
        try {
            GameInstallState.update(base,base,prepared,prefs);
            fail("Missing 1.11 update silently reverted to 1.00");
        } catch(java.io.IOException expected) {
            assertTrue(expected.getMessage().contains("atualização"));
        }
    }
    @Test public void unrelatedGameNeverInheritsCachedUpdate() throws Exception {
        File root=Files.createTempDirectory("tlou-update-isolation").toFile();
        File base=game(root,"CUSA00556"), second=game(root,"CUSA00552");
        File patch=update(root), prepared=new File(root,"tlou-prepared");
        marker(prepared,base,patch);
        SharedPreferences prefs=RuntimeEnvironment.getApplication().getSharedPreferences("tlou-update-isolation",0);
        prefs.edit().clear().commit();
        assertNull(GameInstallState.update(second,second,prepared,prefs));
    }
    @Test public void physicallyInstalledUpdateRequiresNoSeparateOverlay() throws Exception {
        File root=Files.createTempDirectory("tlou-physical-update").toFile();
        File base=game(root,"CUSA00556");
        File prepared=new File(root,"tlou-prepared");
        prepared.mkdirs();
        JSONObject marker=new JSONObject();
        marker.put("game",base.getCanonicalPath());
        marker.put("version","01.11");
        marker.put("update",JSONObject.NULL);
        marker.put("update_version",JSONObject.NULL);
        Files.write(new File(prepared,"device-ready.json").toPath(),
            marker.toString().getBytes(StandardCharsets.UTF_8));
        SharedPreferences prefs=RuntimeEnvironment.getApplication()
            .getSharedPreferences("tlou-physical-update",0);
        prefs.edit().clear().commit();
        assertNull(GameInstallState.update(base,base,prepared,prefs));
    }

    @Test public void explicitUpdateTakesPriorityOverOldPreparedCache() throws Exception {
        File root=Files.createTempDirectory("tlou-update-choice").toFile();
        File base=game(root,"CUSA00556"), old=update(root);
        File newer=new File(root,"update-new");
        File sfo=new File(newer,"sce_sys/param.sfo");sfo.getParentFile().mkdirs();
        Files.write(sfo.toPath(),new byte[]{1});
        File prepared=new File(root,"tlou-prepared");
        marker(prepared,base,old);
        SharedPreferences prefs=RuntimeEnvironment.getApplication().getSharedPreferences("tlou-update-choice",0);
        prefs.edit().putString("update_game",base.getPath()).putString("update_path",newer.getPath()).commit();
        assertEquals(newer.getCanonicalFile(),GameInstallState.update(base,base,prepared,prefs));
    }
}
