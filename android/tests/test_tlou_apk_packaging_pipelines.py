"""Guard against shell SIGPIPE regressions in the real TLOU APK packager."""
from pathlib import Path
import subprocess
import unittest


ROOT = Path(__file__).resolve().parents[2]
PACKAGER = ROOT / "android/bionic/package_apk.sh"


class TlouApkPackagingPipelines(unittest.TestCase):
    def test_checks_do_not_use_early_closing_pipelines(self):
        source = PACKAGER.read_text(encoding="utf-8")
        self.assertNotIn("| grep -q", source)
        self.assertIn("require_command_regex '/system/bin/linker64'", source)
        self.assertIn("require_command_regex 'Machine:.*AArch64'", source)
        self.assertIn("require_command_regex 'assets/tools[.]zip'", source)

    def test_packager_shell_syntax(self):
        subprocess.run(["bash", "-n", str(PACKAGER)], check=True)

    def test_output_check_preserves_producer_failures_and_long_output(self):
        source = PACKAGER.read_text(encoding="utf-8")
        helper = source[source.index("require_command_regex() {"):source.index("\nrequire_command_regex 'Machine:.*AArch64'")]
        script = "set -Eeuo pipefail\n" + helper + """
require_command_regex 'needle' python3 -c 'import sys; sys.stdout.write("needle\\n" + "x"*1000000)'
if require_command_regex 'needle' bash -c 'printf needle; exit 23'; then
  echo 'nonzero producer erroneously accepted' >&2
  exit 4
fi
if require_command_regex 'missing' printf 'needle'; then
  echo 'missing pattern erroneously accepted' >&2
  exit 5
fi
"""
        subprocess.run(["bash", "-c", script], check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)


if __name__ == "__main__":
    unittest.main()
