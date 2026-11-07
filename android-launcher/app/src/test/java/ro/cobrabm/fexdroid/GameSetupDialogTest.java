package ro.cobrabm.fexdroid;

import android.app.Activity;
import android.app.AlertDialog;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import java.util.ArrayList;
import java.util.List;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.robolectric.Robolectric;
import org.robolectric.RobolectricTestRunner;
import org.robolectric.annotation.Config;
import static org.junit.Assert.*;

@RunWith(RobolectricTestRunner.class)
@Config(sdk=30, application=android.app.Application.class)
public class GameSetupDialogTest {
    private void buttons(View view,List<Button> out) {
        if(view instanceof Button)out.add((Button)view);
        if(view instanceof ViewGroup)for(int i=0;i<((ViewGroup)view).getChildCount();i++)
            buttons(((ViewGroup)view).getChildAt(i),out);
    }
    @Test public void allSetupActionsRemainVisibleAndRouteCorrectly() {
        try(var controller=Robolectric.buildActivity(Activity.class).setup()) {
            Activity activity=controller.get();
            for(int expected=0;expected<3;expected++) {
                int[] actual={-1};
                AlertDialog dialog=GameSetupDialog.show(activity,i->actual[0]=i);
                List<Button> actions=new ArrayList<>();
                buttons(dialog.getWindow().getDecorView(),actions);
                actions.removeIf(v->v.getVisibility()!=View.VISIBLE);
                assertEquals(3,actions.size());
                for(Button action:actions)assertTrue(action.isShown());
                actions.get(expected).performClick();
                assertEquals(expected,actual[0]);assertFalse(dialog.isShowing());
            }
        }
    }
    @Test public void setupActionsAreTranslatedInEverySupportedLanguage() {
        try(var controller=Robolectric.buildActivity(Activity.class).setup()) {
            Activity activity=controller.get();
            for(String lang:TlouLanguage.CODES) {
                PortSettings.prefs(activity).edit().putString("ui_language",lang).commit();
                AlertDialog dialog=GameSetupDialog.show(activity,i->{});
                List<Button> actions=new ArrayList<>();buttons(dialog.getWindow().getDecorView(),actions);
                actions.removeIf(v->v.getVisibility()!=View.VISIBLE);
                assertEquals(3,actions.size());
                assertFalse(actions.get(0).getText().toString().isEmpty());
                if(lang.equals("en"))assertEquals("Install PKG",actions.get(0).getText().toString());
                dialog.dismiss();
            }
        }
    }
    @Test public void releaseProfileDisablesHeavyDiagnosticsAndPreservesBoundedCpuSamples() {
        try(var controller=Robolectric.buildActivity(Activity.class).setup()) {
            Activity activity=controller.get();
            PortSettings.prefs(activity).edit().putBoolean("gpu_profile",true)
                .putBoolean("memory_walk_once",false).putBoolean("host_copy_batch",true)
                .putString("prep_workers","auto").putString("game_path","/storage/emulated/0/TLOU").commit();
            PortSettings.retireTestPreferences(activity);
            java.util.Map<String,String> env=new java.util.HashMap<>();
            env.put("GOW3_PREP_WORKERS","8");
            env.put("TLOU_CPU_PHASE_SAMPLES","1");env.put("GOW3_GPU_PROFILE","1");
            env.put("GOW3_FRAME_STATS","1");env.put("GOW3_PERF_DIAG","1");
            PortSettings.environment(activity,env);
            // Eight bounded samples per second expose draw/dispatch CPU phase
            // costs without enabling verbose frame/GPU diagnostics.
            assertEquals("1",env.get("TLOU_CPU_PHASE_SAMPLES"));
            // Draw workers were previously measured as a regression in TLOU.
            // Restore zero-worker default; never use a hidden stale preference
            // or enable the five-second preparation stats in gameplay.
            assertEquals("0",env.get("GOW3_PREP_WORKERS"));
            assertFalse(env.containsKey("TLOU_PREP_NORMAL_PRIORITY"));
            assertFalse(env.containsKey("TLOU_PREP_HIT_STATS"));
            for(String name:new String[]{"GOW3_GPU_PROFILE","GOW3_FRAME_STATS","GOW3_PERF_DIAG"})
                assertFalse(env.containsKey(name));
            assertEquals("1",env.get("TLOU_MEMORY_WALK_ONCE"));
            assertEquals("1",env.get("TLOU_RING_BINDING_CURSOR"));
            assertEquals("0",env.get("GOW3_TLOU_HOST_COPY_BATCH"));
            assertEquals("/storage/emulated/0/TLOU",PortSettings.prefs(activity).getString("game_path",""));
        }
    }

}
