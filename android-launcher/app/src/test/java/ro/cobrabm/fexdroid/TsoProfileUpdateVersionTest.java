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
        TsoProfile.requireSupported111(marker);
    }

    @Test public void shaSelected111OverridesStaleSfo100() throws Exception {
        JSONObject marker=new JSONObject().put("version","01.00")
            .put("update_version",JSONObject.NULL).put("effective_version","01.11")
            .put("effective_self_sha256","31f3b0d120803878d10a7184d139c545de334394dd7dd6ca6058a7c995e484a7");
        assertEquals("111",TsoProfile.gameCode(marker));
        TsoProfile.requireSupported111(marker);
    }

    @Test public void known100CannotBePromotedByMetadata111() throws Exception {
        JSONObject marker=new JSONObject().put("version","01.11")
            .put("effective_version","01.00");
        assertEquals("100",TsoProfile.gameCode(marker));
        try {
            TsoProfile.requireSupported111(marker);
            fail("Known 1.00 SELF must remain blocked");
        } catch(java.io.IOException expected) {
            assertTrue(expected.getMessage().contains("Atualize o jogo para 1.11"));
        }
    }

    @Test public void physical111Remains111() throws Exception {
        JSONObject marker=new JSONObject()
            .put("version","01.11").put("update_version",JSONObject.NULL);
        assertEquals("111",TsoProfile.gameCode(marker));
        TsoProfile.requireSupported111(marker);
    }

    @Test public void base100Remains100() throws Exception {
        JSONObject marker=new JSONObject().put("version","01.00");
        assertEquals("100",TsoProfile.gameCode(marker));
        try {
            TsoProfile.requireSupported111(marker);
            fail("1.00 must be blocked before launching or compiling AOT");
        } catch(java.io.IOException expected) {
            assertTrue(expected.getMessage().contains("Atualize o jogo para 1.11"));
        }
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
