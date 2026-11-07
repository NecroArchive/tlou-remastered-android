package ro.cobrabm.fexdroid;

import android.app.Activity;
import android.view.Gravity;
import android.view.MotionEvent;
import java.io.File;
import java.nio.file.Files;
import java.util.Arrays;
import java.util.HashMap;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.robolectric.Robolectric;
import org.robolectric.RobolectricTestRunner;
import org.robolectric.annotation.Config;
import static org.junit.Assert.*;

@RunWith(RobolectricTestRunner.class)
@Config(sdk=30,application=android.app.Application.class)
public class MenuHudBehaviorTest {
    private int event(EdgeMenuGesture g,int action,float x,float y){
        MotionEvent e=MotionEvent.obtain(0,100,action,x,y,0);
        try{return g.onEvent(e);}finally{e.recycle();}
    }
    @Test public void menuOpensOnlyFromLeftEdgeAndOnlyToTheRight(){
        EdgeMenuGesture g=new EdgeMenuGesture(1);
        assertEquals(0,event(g,0,5,100));assertEquals(0,event(g,2,20,100));
        assertEquals(1,event(g,2,60,105));assertEquals(2,event(g,2,80,105));assertEquals(2,event(g,1,80,105));
        event(g,0,100,100);assertEquals(0,event(g,2,170,100));
        event(g,0,20,100);assertEquals(0,event(g,2,-50,100));
        event(g,0,5,100);assertEquals(0,event(g,2,7,140));assertEquals(0,event(g,2,100,140));
        event(g,0,5,100);event(g,MotionEvent.ACTION_POINTER_DOWN,5,100);assertEquals(0,event(g,2,100,100));
        event(g,0,5,100);event(g,MotionEvent.ACTION_CANCEL,5,100);assertEquals(0,event(g,2,100,100));
    }
    @Test public void topRightHasNoOldMenuOffsetAndAllAnchorsStayInBounds(){
        assertArrayEquals(new int[]{792,8},HudPlacement.position(1000,500,200,30,8,"1",0,0));
        for(int i=0;i<9;i++){
            int[] xy=HudPlacement.position(1000,500,200,30,8,""+i,0,0);
            assertTrue(xy[0]>=0&&xy[0]+200<=1000);assertTrue(xy[1]>=0&&xy[1]+30<=500);
        }
        assertArrayEquals(new int[]{0,0},HudPlacement.position(10,10,20,20,8,"1",0,0));
    }
    @Test public void drawerReallyUsesLeftGravity(){
        try(var c=Robolectric.buildActivity(Activity.class).setup()){
            PortMenu menu=new PortMenu(c.get(),true,new PortMenu.Actions(){
                public void logs(){}public void exportLogs(){}public void chooseGame(){}public void installPkg(){}
                public void importDriver(){}public void selectDriver(){}public void editControls(){}public void resetControls(){}
                public void exitGame(){}public void changed(){}
            });
            menu.open("home");assertTrue(menu.isShowing());
            assertEquals(Gravity.LEFT,menu.drawerGravity()&Gravity.HORIZONTAL_GRAVITY_MASK);menu.dismiss();
        }
    }
    @Test public void removedUnlimitedOptionCannotSurviveUpgrade(){
        try(var c=Robolectric.buildActivity(Activity.class).setup()){
            Activity a=c.get();PortSettings.prefs(a).edit().putString("fps_limit","0").commit();
            PortSettings.retireTestPreferences(a);assertEquals("60",PortSettings.fpsLimit(a));
            HashMap<String,String> env=new HashMap<>();PortSettings.environment(a,env);assertEquals("60",env.get("GOW3_FPS_LIMIT"));
            PortSettings.prefs(a).edit().putString("fps_limit","30").commit();assertEquals("30",PortSettings.fpsLimit(a));
        }
    }
    @Test public void exactExecPidAndRealCpuUsageAreReadable() throws Exception {
        try(var c=Robolectric.buildActivity(Activity.class).setup()){
            File dir=Files.createTempDirectory("tlou runtime ").toFile(),pid=new File(dir,"runtime.pid");
            Process child=new ProcessBuilder(RuntimeProcess.command("/bin/sh",Arrays.asList("/bin/sh","-c","while :; do :; done"),pid)).start();
            HudTelemetry telemetry=new HudTelemetry(c.get(),pid);
            try{
                long deadline=System.nanoTime()+3_000_000_000L;
                while(RuntimeProcess.readPid(pid)<1 && System.nanoTime()<deadline)Thread.yield();
                int actual=RuntimeProcess.readPid(pid);assertTrue(actual>0);assertEquals(actual,telemetry.runtimePid(child));
                telemetry.sample(actual,System.nanoTime());
                long sampleDeadline=System.nanoTime()+2_000_000_000L;
                do{Thread.sleep(50);telemetry.sample(actual,System.nanoTime());}while(!(telemetry.cpuPercent()>0) && System.nanoTime()<sampleDeadline);
                assertTrue("CPU must use the live game's counters",telemetry.cpuPercent()>0);
                telemetry.reset();assertTrue(Double.isNaN(telemetry.cpuPercent()));
            }finally{telemetry.close();child.destroyForcibly();child.waitFor();pid.delete();dir.delete();}
        }
    }
}
