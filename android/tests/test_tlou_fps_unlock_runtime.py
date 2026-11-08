"""120 FPS option controls the actual guest vblank, not just HUD text."""
import unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
JAVA=ROOT/"android-launcher/app/src/main/java/ro/cobrabm/fexdroid"
SET=(JAVA/"PortSettings.java").read_text()
MENU=(JAVA/"PortMenu.java").read_text()
VIDEO=(ROOT/"gow3_pc/gpu/shadps4/core/libraries/videoout/driver.cpp").read_text()
EMU=(ROOT/"gow3_pc/gpu/shim/core/emulator_settings.h").read_text()

class TlouFpsUnlockRuntimeContracts(unittest.TestCase):
    def test_user_option_is_off_by_default_and_restorable(self):
        self.assertIn('toggle("fps_unlock","Desbloquear FPS (até 120, experimental)",false)',MENU)
        self.assertIn('e.putBoolean("fps_unlock",false);',MENU)
        self.assertIn('if(prefs(context).getBoolean("fps_unlock",false))return "120";',SET)

    def test_vblank_and_present_cap_change_together(self):
        self.assertIn('env.put("GOW3_VBLANK_HZ",unlocked?"0":"60");',SET)
        self.assertIn('env.put("GOW3_FPS_LIMIT",fpsLimit(context));',SET)
        self.assertIn('GetVblankFrequency()',EMU)
        self.assertIn('GOW3_VBLANK_HZ',EMU)
        self.assertIn('480',EMU)
        self.assertIn('GetFrameLimit()',EMU)
        self.assertIn('const bool fixed_limit = std::getenv("GOW3_FPS_LIMIT") != nullptr;',VIDEO)
        self.assertIn('EmulatorSettings.GetVblankFrequency() == 480',VIDEO)

    def test_60_hz_cutscene_path_unchanged(self):
        self.assertIn('constexpr std::chrono::nanoseconds movie_period{1000000000 / 60};',VIDEO)
        self.assertIn('const bool movie = fast_vblank && MovieActive(now);',VIDEO)
        self.assertIn('if (movie)',VIDEO)
        self.assertIn('VideoOut: %s',VIDEO)

if __name__=="__main__": unittest.main()
