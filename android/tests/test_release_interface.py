from pathlib import Path
import unittest
ROOT=Path(__file__).resolve().parents[2]
class ReleaseInterfaceTests(unittest.TestCase):
    def test_no_research_labels_in_any_translation(self):
        text=(ROOT/'android-launcher/app/src/main/assets/ui-translations.tsv').read_text()
        for label in ('log 32','log-32','readback'):
            self.assertNotIn(label,text.lower())
        rows=[x.split('\t') for x in text.splitlines() if x and not x.startswith('#')]
        self.assertTrue(all(len(r)==10 and all(r) for r in rows))
        self.assertEqual(len(rows),len({r[0] for r in rows}))
