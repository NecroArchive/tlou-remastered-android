#!/usr/bin/env python3
"""0.2.0: DualShock flashlight shake from physical L3+R3 or touch AGITAR."""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]
JAVA = ROOT / "android-launcher/app/src/main/java/ro/cobrabm/fexdroid"
NATIVE = ROOT / "gow3_pc/src/runtime_pad_bionic.c"


class FlashlightShakeIntegration(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.bridge = (JAVA / "PadBridge.java").read_text(encoding="utf-8")
        cls.touch = (JAVA / "TouchControllerOverlay.java").read_text(encoding="utf-8")
        cls.native = NATIVE.read_text(encoding="utf-8")

    def test_shortcut_only_when_both_physical_sticks_are_pressed(self):
        self.assertIn('keys.contains("l3") && keys.contains("r3")', self.bridge)
        self.assertIn('all.remove("l3");', self.bridge)
        self.assertIn('all.remove("r3");', self.bridge)
        self.assertIn('all.add("shake");', self.bridge)
        self.assertIn('if(shakePhysical) physicalShakeLatch=true;', self.bridge)
        self.assertIn('if(physicalShakeLatch) {', self.bridge)
        self.assertIn('if(!keys.contains("l3") && !keys.contains("r3"))', self.bridge)
        self.assertIn('physicalShakeLatch=false;', self.bridge)
        # On-screen L3 and R3 remain normal independent buttons.
        self.assertIn('all.addAll(virtualKeys);', self.bridge)
        self.assertLess(self.bridge.index('all.add("shake");'),
                        self.bridge.index('all.addAll(virtualKeys);'))
        self.assertIn('case KeyEvent.KEYCODE_BUTTON_THUMBL: return "l3";', self.bridge)
        self.assertIn('case KeyEvent.KEYCODE_BUTTON_THUMBR: return "r3";', self.bridge)

    def test_touch_has_independent_editable_shake_button(self):
        self.assertRegex(self.touch, r'"leftstick","l3","r3","rightstick","shake"')
        self.assertIn('rectangle(c,"shake",118,"AGITAR");', self.touch)
        self.assertIn('preferences.getFloat("overlay_x_"+name', self.touch)
        self.assertIn('preferences.getFloat("overlay_y_"+name', self.touch)
        self.assertIn('pad.virtual(pressed,lx,ly,rx,ry,left,right,l2,r2)', self.touch)
        self.assertIn('else pressed.add(control);', self.touch)

    def test_native_shake_is_motion_sensor_data_not_gamepad_button(self):
        self.assertIn('if (!strcmp(token,"shake")) { injected.shake=1; continue; }',
                      self.native)
        self.assertIn('static void sample_virtual_shake(PadData *d)', self.native)
        self.assertIn('if (down && !was_down) active_until=now+650000;', self.native)
        self.assertIn('if (!down && now>=active_until) return;', self.native)
        self.assertIn('d->acceleration[0]=2.5f*wave;', self.native)
        self.assertIn('d->acceleration[2]=1.5f*cosf(phase);', self.native)
        self.assertIn('d->angular_velocity[1]=6.0f*wave;', self.native)
        self.assertIn('d->angular_velocity[2]=4.0f*wave;', self.native)
        self.assertNotIn('{"shake",', self.native)
        # A host menu/dialog must never receive fake motion.
        self.assertIn('if (capture) { hold_after_capture=1; return; }', self.native)
        self.assertIn('if (!hold_after_capture) sample_virtual_shake(d);', self.native)

if __name__ == "__main__":
    unittest.main()
