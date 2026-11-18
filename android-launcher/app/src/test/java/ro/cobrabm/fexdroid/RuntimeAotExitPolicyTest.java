package ro.cobrabm.fexdroid;

import org.junit.Test;
import static org.junit.Assert.*;

public final class RuntimeAotExitPolicyTest {
    @Test public void intentionalAotSigtermIsNotCrash() {
        assertFalse(RuntimeAotExitPolicy.shouldDisableDirectLaunch(true,143,true));
        assertFalse(RuntimeAotExitPolicy.shouldDisableDirectLaunch(true,137,true));
    }
    @Test public void unexpectedAotCrashStillDisablesDirectLaunch() {
        assertTrue(RuntimeAotExitPolicy.shouldDisableDirectLaunch(true,143,false));
        assertTrue(RuntimeAotExitPolicy.shouldDisableDirectLaunch(true,139,false));
    }
    @Test public void successfulOrFexExitNeverResetsAotPreference() {
        assertFalse(RuntimeAotExitPolicy.shouldDisableDirectLaunch(true,0,false));
        assertFalse(RuntimeAotExitPolicy.shouldDisableDirectLaunch(false,143,false));
    }
}
