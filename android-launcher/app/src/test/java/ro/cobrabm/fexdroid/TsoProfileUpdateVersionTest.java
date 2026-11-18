package ro.cobrabm.fexdroid;

import org.json.JSONObject;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.robolectric.RobolectricTestRunner;
import org.robolectric.annotation.Config;

import static org.junit.Assert.*;

/** Avoid silently selecting the 1.00 AOT when 1.11 was applied as an overlay. */
@RunWith(RobolectricTestRunner.class)
@Config(sdk=30,application=android.app.Application.class)
public class TsoProfileUpdateVersionTest {
    @Test public void separate111UpdateOverrides100Base() throws Exception {
        JSONObject marker=new JSONObject()
            .put("version","01.00")
            .put("update_version","01.11");
        assertEquals("111",TsoProfile.gameCode(marker));
    }

    @Test public void physical111Remains111() throws Exception {
        JSONObject marker=new JSONObject()
            .put("version","01.11").put("update_version",JSONObject.NULL);
        assertEquals("111",TsoProfile.gameCode(marker));
    }

    @Test public void base100Remains100() throws Exception {
        JSONObject marker=new JSONObject().put("version","01.00");
        assertEquals("100",TsoProfile.gameCode(marker));
    }

    @Test public void unknownDoesNotSelectLegacyCompilerDirectory() throws Exception {
        JSONObject marker=new JSONObject().put("version","01.02");
        try {
            TsoProfile.gameCode(marker);
            fail("Unrecognized PS4 version accepted as an AOT build");
        } catch(java.io.IOException expected) {
            assertTrue(expected.getMessage().contains("não suportada"));
        }
    }
}
