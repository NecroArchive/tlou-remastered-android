"""Production release 0.2.1: version, worker policy, icon and compatibility."""
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
        self.assertIn("versionName '0.2.1'",build)
        # Experimental AOT v2 has its own Android package ID and an upgrade
        # code above 41. The production 0.1.1 release remains versionCode 41.
        if "applicationIdSuffix '.aotnative2'" in build:
            self.assertIn("versionCode 201",build)
        else:
            self.assertIn("versionCode 201",build)
        self.assertIn('text("v"+TlouDialogs.version(this)',main)
        self.assertIn('String version="0.2.1"',about)
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

    def test_stable_020_release_preserves_original_package_and_certificate(self):
        build=(ANDROID/"build.gradle").read_text()
        workflow=(ROOT/".github/workflows/tlou-native-apk.yml").read_text()
        self.assertIn("applicationId 'com.daedalus.tlou.remastered'",build)
        self.assertIn("versionName '0.2.1'",build)
        self.assertIn("versionCode 201",build)
        self.assertIn("name: TLOU Bionic Full Native and APK",workflow)
        self.assertIn("tlou-ui-approval-log32-20261105",workflow)
        self.assertIn("key: tlou-development-signing-v2",workflow)
        self.assertIn("bash android/bionic/package_apk.sh",workflow)
        self.assertIn("Restore signing identity used by existing installations",workflow)
        self.assertIn("app:assembleRelease",workflow)
        self.assertNotIn("app:assembleDebug",workflow)
        self.assertIn("v0.2.1",(ROOT/"RELEASE_NOTES.md").read_text())

if __name__=="__main__":
    unittest.main()
