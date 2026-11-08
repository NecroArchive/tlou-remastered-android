"""Production release 0.1.1: version, worker policy, icon, dialogs, and APK naming."""
from pathlib import Path
import unittest

ROOT=Path(__file__).resolve().parents[2]
ANDROID=ROOT/"android-launcher/app"
JAVA=ANDROID/"src/main/java/ro/cobrabm/fexdroid"

class Release011(unittest.TestCase):
    def test_app_version_matches_visible_ui_and_upgrade_code(self):
        build=(ANDROID/"build.gradle").read_text()
        main=(JAVA/"MainActivity.java").read_text()
        about=(JAVA/"AboutPanel.java").read_text()
        self.assertIn("versionName '0.1.1'",build)
        self.assertIn("versionCode 41",build)
        self.assertIn('text("v"+TlouDialogs.version(this)',main)
        self.assertIn('String version="0.1.1"',about)
        self.assertIn('artFrame.addView(artVersion,artBadge)',main)
        self.assertNotIn('text("v0.1",',main)

    def test_zero_worker_setting_is_never_overridden_by_111(self):
        game=(JAVA/"GameActivity.java").read_text()
        settings=(JAVA/"PortSettings.java").read_text()
        self.assertIn('env.put("GOW3_PREP_WORKERS","0");',settings)
        self.assertNotIn('env.put("GOW3_PREP_WORKERS","2");',game)
        self.assertNotIn('env.put("TLOU_PREP_NORMAL_PRIORITY","1");',game)

    def test_new_icon_and_dialogs_use_offline_art_and_theme(self):
        icon=(ANDROID/"src/main/res/drawable/tlou_app_icon.xml").read_text()
        manifest=(ANDROID/"src/main/AndroidManifest.xml").read_text()
        theme=(ANDROID/"src/main/res/values/styles.xml").read_text()
        dialogs=(JAVA/"TlouDialogs.java").read_text()
        self.assertIn('@drawable/tlou_hero',icon)
        self.assertIn('@drawable/tlou_app_icon',manifest)
        self.assertIn('@style/TlouTheme',manifest)
        self.assertIn('TlouAlertTheme',theme)
        self.assertIn('static Dialog showLogs(',dialogs)
        for label in ('Copiar','Exportar','Fechar'):
            self.assertIn('"'+label+'"',dialogs)
        self.assertIn('ClipboardManager',dialogs)
        self.assertIn('RunLog.export(this,output)',(JAVA/"GameActivity.java").read_text())
        self.assertIn('TlouDialogs.showLogs(this,RunLog.summary(this),this::exportLogs)',(JAVA/"MainActivity.java").read_text())

    def test_release_artifacts_use_new_filename_but_keep_pinned_source_identity(self):
        name="TheLastOfUsRemastered-Android-v0.1.1-arm64.apk"
        package=(ROOT/"android/bionic/package_apk.sh").read_text()
        workflow=(ROOT/".github/workflows/tlou-native-apk.yml").read_text()
        self.assertIn(name,package)
        self.assertIn(name,workflow)
        self.assertIn('out/source-apk/TheLastOfUsRemastered-Android-v0.1-arm64.apk',workflow)
        self.assertIn('name: TheLastOfUsRemastered-Android-v0.1.1-arm64',workflow)
        self.assertIn('v0.1.1',(ROOT/"RELEASE_NOTES.md").read_text())

if __name__=="__main__":
    unittest.main()
