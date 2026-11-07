"""Release guards for TLOU 1.11 launch, unified mode and update UI."""
from pathlib import Path
import unittest

ROOT=Path(__file__).resolve().parents[2]
JAVA=ROOT/"android-launcher/app/src/main/java/ro/cobrabm/fexdroid"

class TlouUpdate111Contract(unittest.TestCase):
    def test_devkit_memory_report_disabled_for_update_111_only(self):
        game=(JAVA/"GameActivity.java").read_text()
        assert 'env.put("GOW3_DMEM_REPORT_MB","4608")' in game
        assert 'if("01.11".equals(gameVersion))' in game
        assert "device-ready.json" in game
        assert 'env.put("GOW3_DMEM_MB"' not in game

    def test_unified_mode_has_no_proxy_or_hidden_old_preference(self):
        settings=(JAVA/"PortSettings.java").read_text()
        main=(JAVA/"MainActivity.java").read_text()
        assert 'edit.remove("render_path")' in settings
        assert 'TLOU_UNIFIED_GUEST_UI' in settings
        assert 'final boolean proxy=' not in settings
        assert 'choice(video,"render_path"' not in main

    def test_update_pkg_extraction_needs_no_additional_folder_selection(self):
        main=(JAVA/"MainActivity.java").read_text()
        assert 'installGameUpdatePkg(local(uri,false));' in main
        assert "PICK_UPDATE_DEST" not in main
        assert "pendingUpdatePkg" not in main
        assert "automaticUpdateDestination" in main

if __name__=="__main__":
    unittest.main()
