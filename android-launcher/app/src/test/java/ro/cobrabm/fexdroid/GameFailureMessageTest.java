package ro.cobrabm.fexdroid;
import org.junit.Test;
import static org.junit.Assert.*;
public final class GameFailureMessageTest {
    @Test public void gpuLossIsNotShownAsNativeLog() {
        GameFailureMessage m=GameFailureMessage.classify("Runtime exit 134",
            "GPU [Debug] <Critical> Device lost during submit");
        assertEquals("TLOU-GPU-01",m.code);
        assertFalse(m.description.contains("vk_scheduler"));
    }
    @Test public void jobDeadlockClassifiedBeforeNullGuestCrash() {
        GameFailureMessage m=GameFailureMessage.classify("Fault (signal 11) at RIP 0x0",
            "Job System has been inactive with a waiting job for more than 10 seconds.");
        assertEquals("TLOU-JOB-02",m.code);
    }
    @Test public void freedrenoFaultIsDriverSpecific() {
        GameFailureMessage m=GameFailureMessage.classify("Host fault (signal 11)",
            "libvulkan_freedreno.so+0x8e0304, address 0x1d0");
        assertEquals("TLOU-DRV-03",m.code);
    }
    @Test public void noStackTraceLeaksIntoFallback() {
        GameFailureMessage m=GameFailureMessage.classify("private path /data/user/0",
            "Fatal exception with memory dump");
        assertEquals("TLOU-RUN-00",m.code);
        assertFalse(m.description.contains("/data/user"));
    }
}
