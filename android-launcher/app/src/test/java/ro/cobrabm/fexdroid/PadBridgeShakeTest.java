package ro.cobrabm.fexdroid;

import android.view.KeyEvent;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Set;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.robolectric.RobolectricTestRunner;
import org.robolectric.annotation.Config;
import static org.junit.Assert.*;

/** Actual Android gamepad-event and atomic-file tests for flashlight motion. */
@RunWith(RobolectricTestRunner.class)
@Config(sdk=30, application=android.app.Application.class)
public class PadBridgeShakeTest {
    private static void press(PadBridge bridge, int action, int key) {
        assertTrue(bridge.key(new KeyEvent(action,key)));
    }
    private static String state(Path p) throws Exception {
        return new String(Files.readAllBytes(p), java.nio.charset.StandardCharsets.UTF_8);
    }
    @Test public void physicalL3R3ShakesAndSuppressesClicksUntilBothReleased() throws Exception {
        Path dir=Files.createTempDirectory("tlou-shake-gamepad");
        Path file=dir.resolve("pad.txt");
        try {
            PadBridge bridge=new PadBridge(file.toFile());
            press(bridge,KeyEvent.ACTION_DOWN,KeyEvent.KEYCODE_BUTTON_THUMBL);
            assertTrue(state(file).startsWith("l3 "));
            assertFalse(state(file).contains("shake "));

            press(bridge,KeyEvent.ACTION_DOWN,KeyEvent.KEYCODE_BUTTON_THUMBR);
            assertTrue(state(file).startsWith("shake "));
            assertFalse(state(file).contains("l3 "));
            assertFalse(state(file).contains("r3 "));

            // Releasing one stick must not accidentally toggle the flashlight.
            press(bridge,KeyEvent.ACTION_UP,KeyEvent.KEYCODE_BUTTON_THUMBL);
            assertFalse(state(file).startsWith("r3 "));
            assertFalse(state(file).contains("shake "));

            press(bridge,KeyEvent.ACTION_UP,KeyEvent.KEYCODE_BUTTON_THUMBR);
            assertFalse(state(file).contains("shake "));
            press(bridge,KeyEvent.ACTION_DOWN,KeyEvent.KEYCODE_BUTTON_THUMBR);
            assertTrue(state(file).startsWith("r3 "));
        } finally {
            Files.deleteIfExists(file);
            Files.deleteIfExists(dir);
        }
    }
    @Test public void touchStickClicksDoNotBecomeShakeButDedicatedTouchButtonDoes() throws Exception {
        Path dir=Files.createTempDirectory("tlou-shake-touch");
        Path file=dir.resolve("pad.txt");
        try {
            PadBridge bridge=new PadBridge(file.toFile());
            bridge.virtual(new java.util.LinkedHashSet<>(java.util.Arrays.asList("l3","r3")),128,128,128,128,false,false,0,0);
            assertTrue(state(file).contains("l3 "));
            assertTrue(state(file).contains("r3 "));
            assertFalse(state(file).contains("shake "));
            bridge.virtual(new java.util.LinkedHashSet<>(java.util.Arrays.asList("shake")),128,128,128,128,false,false,0,0);
            assertTrue(state(file).startsWith("shake "));
            bridge.reset();
            assertFalse(state(file).contains("shake "));
        } finally {
            Files.deleteIfExists(file);
            Files.deleteIfExists(dir);
        }
    }
}
