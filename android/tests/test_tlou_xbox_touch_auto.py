"""Non-interactive regression guards for Xbox input and auto touch controls."""
import unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
PAD=(ROOT/"android-launcher/app/src/main/java/ro/cobrabm/fexdroid/PadBridge.java").read_text()
ACT=(ROOT/"android-launcher/app/src/main/java/ro/cobrabm/fexdroid/GameActivity.java").read_text()
MENU=(ROOT/"android-launcher/app/src/main/java/ro/cobrabm/fexdroid/PortMenu.java").read_text()
SET=(ROOT/"android-launcher/app/src/main/java/ro/cobrabm/fexdroid/PortSettings.java").read_text()

class XboxAndTouchAutoContracts(unittest.TestCase):
    def test_physical_buttons_use_android_keycodes_first(self):
        self.assertLess(PAD.index("switch (e.getKeyCode())"),PAD.index("switch (e.getScanCode())"))
        for button,ps in (("A","cross"),("B","circle")):
            self.assertIn('case KeyEvent.KEYCODE_BUTTON_'+button+': return "'+ps+'";',PAD)
        self.assertIn('KeyEvent.KEYCODE_BUTTON_START: return "options"',PAD)
        self.assertIn('KeyEvent.KEYCODE_BUTTON_SELECT: return "touchpad"',PAD)
        self.assertIn('KeyEvent.KEYCODE_BUTTON_THUMBL: return "l3"',PAD)
        self.assertIn('KeyEvent.KEYCODE_BUTTON_THUMBR: return "r3"',PAD)

    def test_xbox_right_stick_and_triggers_have_separate_axes(self):
        self.assertIn('hasRxRy?MotionEvent.AXIS_RX:MotionEvent.AXIS_Z',PAD)
        self.assertIn('hasRxRy?MotionEvent.AXIS_RY:MotionEvent.AXIS_RZ',PAD)
        self.assertIn('hasRxRy?MotionEvent.AXIS_Z:-1',PAD)
        self.assertIn('hasRxRy?MotionEvent.AXIS_RZ:-1',PAD)
        self.assertIn('r.getMin()<0 && r.getMax()>r.getMin()',PAD)

    def test_overlay_is_automatically_hidden_for_connected_controller(self):
        self.assertIn('registerInputDeviceListener(controllerListener,handler)',ACT)
        self.assertIn('unregisterInputDeviceListener(controllerListener)',ACT)
        self.assertIn('InputDevice.SOURCE_GAMEPAD',ACT)
        self.assertIn('InputDevice.SOURCE_JOYSTICK',ACT)
        self.assertIn('!hasHardwareController()',ACT)
        self.assertIn('if(!show)touch.release()',ACT)
        self.assertIn('editingControls || "on".equals(controlMode)',ACT)
        self.assertIn('new String[]{"auto","on","off"}',MENU)
        self.assertIn('edit.putString("touch_mode",',SET)

if __name__ == "__main__":
    unittest.main()
