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

    def test_release_requires_native_aot_and_removes_fex_ui_fallback(self):
        code=(JAVA/"MainActivity.java").read_text(encoding="utf-8")
        game=(JAVA/"GameActivity.java").read_text(encoding="utf-8")
        about=(JAVA/"AboutPanel.java").read_text(encoding="utf-8")
        self.assertIn("RuntimeAot.Ready check=RuntimeAot.prepare(this,prepared)",code)
        self.assertIn("startPreparedGame(prepared);",code)
        method=code[code.index("private void showAotUnavailable("):
                    code.index("private void showPendingAotPatches(")]
        self.assertNotIn('"Usar FEX"',method)
        self.assertIn('.setPositiveButton(TlouLanguage.t(this,"Compilar CPU ARM64")',method)
        self.assertNotIn('putString("cpu_backend","fex")',code)
        self.assertNotIn('choice(cpu,"cpu_backend"',code)
        self.assertNotIn('toggleSetting(cpu,"aot_detailed_diagnostics"',code)
        self.assertNotIn('toggleSetting(cpu,"fast_render_cores"',code)
        self.assertNotIn('toggleSetting(box,"shader_scan_scalar"',code)
        self.assertNotIn('toggleSetting(box,"fps_unlock"',code)
        self.assertIn('new String[]{"30 FPS","60 FPS","120 FPS"}',code)
        self.assertIn('RuntimeAot.Ready nativeAot;',game)
        self.assertIn('nativeAot=RuntimeAot.prepare(this,base)',game)
        self.assertNotIn('"cpu_backend"',game)
        self.assertNotIn('"FEX-Emu"',about)
        self.assertIn("RuntimeAot.hasCompletedVariantBuild(this)",code)
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
