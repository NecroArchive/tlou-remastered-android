package ro.cobrabm.fexdroid;

import android.app.Application;
import android.content.Context;
import android.content.SharedPreferences;
import java.lang.reflect.Method;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.robolectric.RobolectricTestRunner;
import org.robolectric.RuntimeEnvironment;
import org.robolectric.annotation.Config;
import static org.junit.Assert.*;

/** Scale drawing and touch targets together without changing saved button locations. */
@RunWith(RobolectricTestRunner.class)
@Config(sdk=30, application=Application.class)
public class TouchControllerSizeTest {
    private Application context;
    private SharedPreferences settings;
    private TouchControllerOverlay overlay;
    private Method hit;

    @Before public void setUp() throws Exception {
        context=RuntimeEnvironment.getApplication();
        settings=PortSettings.prefs(context);
        settings.edit().clear().putString("control_style","classic").commit();
        context.getSharedPreferences("gow3",Context.MODE_PRIVATE).edit().clear().commit();
        overlay=new TouchControllerOverlay(context,null,false);
        hit=TouchControllerOverlay.class.getDeclaredMethod("hit",float.class,float.class);
        hit.setAccessible(true);
    }

    private String hitAt(float x,float y) throws Exception {
        return (String)hit.invoke(overlay,x,y);
    }

    @Test public void defaultControlsAreLargerAndHitboxesFollowLiveGlobalSetting() throws Exception {
        // CROSS is centered at (1120,545); this tap is 54 layout pixels away.
        assertEquals("cross",hitAt(1120f,599f)); // 120% default
        settings.edit().putInt("control_size",100).commit();
        assertNull(hitAt(1120f,599f)); // original 48-pixel hit radius
        settings.edit().putInt("control_size",160).commit();
        assertEquals("cross",hitAt(1120f,615f)); // 70-pixel gap is now in bounds
        settings.edit().putInt("control_size",75).commit();
        assertNull(hitAt(1120f,599f)); // smaller hitbox too
    }

    @Test public void globalSizeDoesNotMoveAnExistingCustomButton() throws Exception {
        SharedPreferences positions=context.getSharedPreferences("gow3",Context.MODE_PRIVATE);
        positions.edit().putFloat("overlay_x_cross",800f)
                        .putFloat("overlay_y_cross",300f).commit();
        overlay=new TouchControllerOverlay(context,null,false);
        assertEquals("cross",hitAt(800f,355f)); // 120%, persisted position
        assertNull(hitAt(1120f,545f)); // no longer at its default location
        settings.edit().putInt("control_size",100).commit();
        assertNull(hitAt(800f,355f)); // same center, smaller radius
        assertEquals("cross",hitAt(800f,300f));
    }

    @Test public void actionLayoutUsesSeparatePositionsAndWidescreenHitTargets() throws Exception {
        settings.edit().putString("control_style","action").putInt("control_size",100).commit();
        overlay=new TouchControllerOverlay(context,null,false);
        overlay.layout(0,0,1600,720);
        assertEquals("triangle",hitAt(1166f,273f));
        assertEquals("leftstick",hitAt(146f,532f));
        assertEquals("rightstick",hitAt(830f,532f));
        assertEquals("l3",hitAt(284f,551f));
        assertEquals("r3",hitAt(960f,551f));
        assertEquals("shake",hitAt(640f,628f));
        assertEquals("selector_2",hitAt(200f,414f));
        assertEquals("left",TouchControllerOverlay.selectorDirection("selector_left"));
        assertEquals("right",TouchControllerOverlay.selectorDirection("selector_0"));
        assertEquals("down",TouchControllerOverlay.selectorDirection("selector_1"));
        assertEquals("down",TouchControllerOverlay.selectorDirection("selector_2"));
        assertEquals("up",TouchControllerOverlay.selectorDirection("selector_3"));
        assertEquals("right",TouchControllerOverlay.selectorDirection("selector_right"));
        settings.edit().putString("control_style","classic").commit();
        overlay=new TouchControllerOverlay(context,null,false);
        assertEquals("cross",hitAt(1120f,545f));
    }

    @Test public void scalePreferenceIsClampedWithoutErasingIt() throws Exception {
        settings.edit().putInt("control_size",1000).commit();
        assertEquals("cross",hitAt(1120f,615f)); // capped at 160%
        settings.edit().putInt("control_size",-10).commit();
        assertNull(hitAt(1120f,599f)); // floored at 75%
        assertEquals(-10,settings.getInt("control_size",0)); // keep preference intact
    }
}
