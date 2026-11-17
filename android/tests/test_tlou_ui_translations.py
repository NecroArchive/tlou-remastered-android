#!/usr/bin/env python3
"""Regression checks for English UI completeness and A6xx AOT preflight.

Offline tests use source files only; no game assets or device are required.
"""
import re
from pathlib import Path
import unittest

ROOT=Path(__file__).resolve().parents[2]
ASSETS=ROOT/"android-launcher/app/src/main/assets/ui-translations.tsv"
JAVA=ROOT/"android-launcher/app/src/main/java/ro/cobrabm/fexdroid"

REQUIRED=(
    "CPU ARM64 não disponível",
    "A CPU ARM64 ainda não está pronta para este jogo. Compile a CPU uma vez, ou use FEX para iniciar sem esperar pela compilação.",
    "Motivo técnico (veja o log para detalhes):",
    "Compilar CPU ARM64",
    "Usar FEX",
    "Patches ARM64 ainda não aplicados",
    "Compilar e jogar",
    "Processador do jogo",
    "ARM64 recompilado (AOT experimental)",
    "FEX (estável)",
    "CPU ARM64: ",
    "já compilada",
    "ainda não compilada",
    "Diagnóstico aprofundado AOT (reduz FPS)",
    "Apresentação Vulkan compacta (8 Gen 3)",
    "Correção UBWC do Turnip (tela/DeX)",
    "Automático (somente Adreno 740)",
    "Automático (somente Adreno 750)",
    "Desativar sombras de luzes omni (1.11)",
    "Desativar sombras de holofotes (1.11)",
    "Atualizar por PKG",
    "Atualizar por pasta",
    "Instalando atualização diretamente na pasta do jogo…",
    "Extraindo PKG e atualizando diretamente o jogo…",
    "Compilando a CPU ARM64 com todos os patches disponíveis…",
    "NÃO FOI POSSÍVEL CONTINUAR",
    "VOLTAR AO LAUNCHER",
    "Falha ao exportar logs: ",
    "Logs ZIP salvos.",
    "VSync OFF (desbloqueio)",
)

class UITranslationRegression(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.rows={}
        for number,line in enumerate(ASSETS.read_text(encoding="utf-8").splitlines(),1):
            if not line.strip() or line.startswith("#"):continue
            fields=line.split("\t")
            if len(fields)!=10:
                raise AssertionError(f"ui-translations.tsv:{number}: expected 10 languages, got {len(fields)}")
            if fields[0] in cls.rows:
                raise AssertionError(f"Duplicate translation: {fields[0]} at line {number}")
            if not fields[1]:
                raise AssertionError(f"Missing English fallback for {fields[0]}")
            cls.rows[fields[0]]=fields

    def test_all_recent_visible_english_keys_present(self):
        for key in REQUIRED:
            self.assertIn(key,self.rows,key)
            self.assertNotEqual(self.rows[key][1].strip(), "",key)
        self.assertEqual(self.rows["CPU ARM64 não disponível"][1],"ARM64 CPU unavailable")
        self.assertEqual(self.rows["Usar FEX"][1],"Use FEX")
        self.assertEqual(self.rows["VOLTAR AO LAUNCHER"][1],"RETURN TO LAUNCHER")

    def test_recent_high_visibility_strings_are_localized(self):
        for key in ("Desativar sombras de luzes omni (1.11)",
                    "Desativar sombras de holofotes (1.11)",
                    "CPU ARM64 não disponível", "já compilada",
                    "ainda não compilada", "VOLTAR AO LAUNCHER",
                    "FALHA NO DRIVER GRÁFICO"):
            values=self.rows[key]
            for locale in range(2,10):
                self.assertNotEqual(values[locale],values[1],(key,locale))

    def test_recent_multilingual_rows_fall_back_to_english(self):
        # TlouLanguage.t handles blank localized columns by selecting column 1.
        code=(JAVA/"TlouLanguage.java").read_text(encoding="utf-8")
        self.assertIn("return !values[1].isEmpty()?values[1]:original;",code)
        self.assertIn('return 1; // English fallback',code)
        self.assertIn('"ui_language"',code)

    def test_missing_aot_cannot_launch_or_change_backend_silently(self):
        code=(JAVA/"MainActivity.java").read_text(encoding="utf-8")
        check=code.index("RuntimeAot.Ready check=RuntimeAot.prepare(this,prepared)")
        launch=code.index("startPreparedGame(prepared);",check)
        self.assertLess(check,launch)
        method=code[code.index("private void showAotUnavailable("):
                    code.index("private void showPendingAotPatches(")]
        self.assertIn('.setNeutralButton(TlouLanguage.t(this,"Usar FEX")',method)
        self.assertIn('.setPositiveButton(TlouLanguage.t(this,"Compilar CPU ARM64")',method)
        self.assertIn('prefs.edit().putString("cpu_backend","fex").apply();',method)
        self.assertIn('putBoolean("launch_direct",false)',code)
        # A stale .so or a compiler run that only reached 'staged' must not
        # show "already compiled" on the launcher.
        self.assertIn("RuntimeAot.hasCompletedVariantBuild(this)",code)
        self.assertNotIn('selectedAot.isFile()?"já compilada"',code)
        preflight=(JAVA/"RuntimeAot.java").read_text(encoding="utf-8")
        quick=preflight[preflight.index("static boolean hasCompletedVariantBuild("):
                        preflight.index("static String pendingSelectedPatches(")]
        self.assertIn('"aarch64_generated_native_ready".equals(report.optString("status"))',quick)
        self.assertIn('hasCompleteVariantCatalog(report)',quick)
        self.assertIn('built.optInt("variants_skipped",-1)==0',quick)

    def test_game_failure_screen_uses_app_language(self):
        code=(JAVA/"GameFailureScreen.java").read_text(encoding="utf-8")
        for key in ("reason.title","reason.description","reason.action"):
            self.assertIn(f"TlouLanguage.t(activity,{key})",code)
        self.assertIn('TlouLanguage.t(activity,"VOLTAR AO LAUNCHER")',code)

if __name__=="__main__":
    unittest.main()
