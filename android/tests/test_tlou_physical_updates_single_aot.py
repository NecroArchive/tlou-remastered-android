#!/usr/bin/env python3
"""Static CI guard for single native AOT backend and physical update buttons."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]
JAVA = ROOT / "android-launcher/app/src/main/java/ro/cobrabm/fexdroid"

def method(code: str, name: str, next_name: str) -> str:
    first = code.index("    private ", code.index(name) - 28) if False else code.index(name)
    end = code.index(next_name, first) if next_name else len(code)
    return code[first:end]

class PhysicalUpdatesSingleAot(unittest.TestCase):
    def test_both_update_buttons_write_to_same_game(self):
        code=(JAVA/"MainActivity.java").read_text()
        self.assertIn('add(updates,"Atualizar por PKG"',code)
        self.assertIn('add(updates,"Atualizar por pasta"',code)
        self.assertNotIn("confirmInPlaceUpdate",code)
        self.assertNotIn('putString("update_path"',code)
        self.assertIn("try(UpdateInPlace.Transaction transaction=UpdateInPlace.apply(game,patch",code)
        self.assertIn('tools.prepare(game,null,this::update)',code)
        self.assertIn('prefs.edit().remove("update_game").remove("update_path")',code)
        package=method(code,"private void installGameUpdatePkg(", "private File updateForGame(")
        self.assertIn("installPhysicalUpdate(game,patch)",package)
        self.assertIn("InstallTools.delete(container)",package)
        folder=method(code,"private void applyGameUpdate(", "private void installPhysicalUpdate(")
        self.assertIn("installPhysicalUpdate(game,patch)",folder)

    def test_no_profile_controls_or_warnings_in_launcher(self):
        ui=(JAVA/"MainActivity.java").read_text()
        runtime=(JAVA/"RuntimeAot.java").read_text()
        tools=(JAVA/"InstallTools.java").read_text()
        profile=(JAVA/"TsoProfile.java").read_text()
        frontend=(ROOT/"android/aot/aot_local.py").read_text()
        for phrase in ("Perfil TSO","TSO DESLIGADO","perfil TSO","TsoProfile.KEY",
                       "confirmInPlaceUpdate"):
            self.assertNotIn(phrase,ui)
        for phrase in ("TSO DESLIGADO","TSO AOT selecionado","perfil TSO"):
            self.assertNotIn(phrase,runtime)
        self.assertIn('return RELAXED;',profile)
        self.assertIn('return "bin/libbbrecomp.so";',profile)
        self.assertIn('"--tso-profile",profile',tools)
        self.assertIn('choices=("off",), default="off"',frontend)
        self.assertIn('if(!reloadAfterCall || nativeTso || !fiberResumes',runtime)

    def test_production_compiler_package_is_single(self):
        package=(ROOT/"android/bionic/repackage_local_aot.py").read_text()
        self.assertIn("if os.environ.get(\"TLOU_TSO_MULTI_PROFILE_DIR\"):",package)
        self.assertIn('info.filename.startswith("aot/compilers/")',package)
        self.assertIn('b"3-tlou-string" not in code',package)

if __name__=="__main__":
    unittest.main()
