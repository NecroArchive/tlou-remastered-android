import importlib.util
from pathlib import Path
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location("reuse_apk", Path(__file__).parents[1]/"bionic/reuse_apk.py")
reuse = importlib.util.module_from_spec(spec)
spec.loader.exec_module(reuse)

class LauncherReuseTests(unittest.TestCase):
    def test_v021_always_requires_full_native_release_even_for_launcher_only_changes(self):
        gradle=(reuse.ROOT/"android-launcher/app/build.gradle").read_text()
        self.assertIn("versionName '0.2.1'",gradle)
        for change in ("android/bionic/reuse_apk.py",
                       "android/tlou_patches.py",
                       "android-launcher/app/src/main/java/ro/cobrabm/fexdroid/PortMenu.java",
                       "gow3_pc/gpu/shim.cpp"):
            with patch.object(reuse.subprocess,"check_output",return_value=change+"\\n") as mocked:
                self.assertFalse(reuse.unchanged(),change)
                # Release 0.2.1 must reject the old fast path before git diff.
                mocked.assert_not_called()

    def test_current_ui_is_never_replaced_by_old_apk(self):
        self.assertIsNone(reuse.payload("assets/ui-translations.tsv"))
        self.assertIsNone(reuse.payload("classes.dex"))
        self.assertEqual(Path("apk-jni/arm64-v8a/libgow3gpu.so"),reuse.payload("lib/arm64-v8a/libgow3gpu.so"))
        self.assertEqual(Path("apk-assets/tools.zip"),reuse.payload("assets/tools.zip"))
    def test_patch_refresh_preserves_native_installer_and_other_sources(self):
        import zipfile,tempfile
        with tempfile.TemporaryDirectory() as tmp:
            archive=Path(tmp)/"tools.zip"
            with zipfile.ZipFile(archive,"w") as z:
                z.writestr("python/bin/python3.13",b"unchanged interpreter")
                z.writestr("source/android/tlou_patches.py",b"previous patch table")
                z.writestr("source/android/device_prepare.py",b"unchanged preparer")
            reuse.refresh_patch_source(archive)
            with zipfile.ZipFile(archive) as z:
                self.assertEqual(z.read("python/bin/python3.13"),b"unchanged interpreter")
                self.assertEqual(z.read("source/android/device_prepare.py"),b"unchanged preparer")
                self.assertEqual(z.read("source/android/tlou_patches.py"),(reuse.ROOT/"android/tlou_patches.py").read_bytes())
            before=archive.read_bytes();reuse.refresh_patch_source(archive);self.assertEqual(archive.read_bytes(),before)

    def test_unsafe_archive_paths_rejected(self):
        for name in ("../assets/tools.zip", "assets/../../evil", "/lib/arm64-v8a/evil"):
            with self.assertRaises(ValueError):reuse.payload(name)
