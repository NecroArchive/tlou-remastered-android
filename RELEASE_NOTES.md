# The Last of Us Remastered — Daedalus Android v0.2.0

## Português (Brasil)

Versão **0.2.0** do APK estável, compilada na branch `android-bionic-stage1`
com o mesmo identificador de pacote e certificado de assinatura do release 0.1.1.

- CPU ARM64 recompilada (AOT) obrigatória. A compilação inicial inclui todas
  as variantes de patches da atualização 1.11; não é preciso recompilar por
  mudança de resolução ou de opção de patch.
- Atualizações 1.11 que ainda usam o método antigo de overlay são migradas
  para a pasta do jogo antes da compilação, com cópia transacional e validação.
- Saída Vulkan fixa em 1280 × 720 para o Android, independentemente do SoC
  (sem alterar a resolução interna do jogo).
- Limite de 120 FPS movido para Vídeo; opções de teste A/B, afinidade manual
  dos núcleos, alternância FEX e diagnóstico profundo AOT removidas.
- Distribuição automática de threads nos processadores Oryon.
- **L3 + R3 no controle físico** simula a agitação do DualShock para a recarga
  da lanterna. Botão **AGITAR** independente e reposicionável na tela.
- GPU Vulkan atualizada com tentativas de compatibilidade para drivers
  Qualcomm; funcionamento nesses drivers ainda não confirmado em todos os aparelhos.

A assinatura é conferida automaticamente contra o APK original 0.1.1 no
GitHub Actions. A instalação experimental `.aotnative2` é separada e
**não** pode ser atualizada diretamente pelo APK estável.
Nenhum arquivo do jogo é incluído. A campanha completa e o comportamento
da lanterna no aparelho ainda precisam de validação.

## English

Stable **v0.2.0** APK built from `android-bionic-stage1`, retaining the
original stable 0.1.1 package name and signing certificate.

- Mandatory native ARM64 AOT backend, compiling all supported 1.11 patch
  variants together; no recompilation for changing patch or resolution.
- Transactional migration of older 1.11 overlay updates into the game folder
  before compilation, with version verification.
- Fixed 1280 × 720 Vulkan output to Android on supported devices while
  retaining independently selected internal game resolution.
- 120 FPS option moved to Video; removed FEX backend selection, manual
  fast-core switches, and experimental CPU/AOT benchmark controls.
- Automatic Oryon CPU scheduling.
- Physical **L3 + R3** combination and movable **SHAKE** touchscreen button
  simulate DualShock motion for flashlight recharging.
- New Qualcomm proprietary Vulkan compatibility attempts; device-specific
  gameplay success is not yet established.

CI verifies the release APK's certificate against the original 0.1.1 APK.
The experimental `.aotnative2` package is a separate installation and cannot
be updated directly with this stable APK. Game assets are not included.

---

# v0.1 — First public Android build / Primeiro lançamento Android

## Português (Brasil)

**The Last of Us Remastered — Daedalus Android v0.1**

Primeira build pública da adaptação de The Last of Us Remastered para Android,
baseada no port de PC de ravencaio, com runtime Bionic ARM64, FEX e Vulkan.

### Recursos do lançamento

- Instalação por PKG ou pasta extraída, com preparação automática no celular.
- Launcher e ícone com tema de The Last of Us.
- Controles na tela, controle físico e menu pelo gesto da esquerda para a direita.
- Tela cheia ajustável durante o jogo.
- HUD de FPS, GPU e CPU em uma linha, com posição configurável.
- Resolução original, 720p, 540p e 360p; patches gráficos opcionais.
- Limites de 30/60 FPS e VSync.
- Turnip Gen8 V38 Patched para Adreno 8xx e Turnip 27.0.0 R1 para Adreno 7xx/6xx;
  seleção e importação de drivers ZIP/SO.
- Importação/exportação de saves e lista compacta com dados e miniaturas reais
  quando disponíveis no save.
- Interface em dez idiomas, incluindo português brasileiro, turco e polonês;
  idioma do sistema como padrão e inglês como fallback.
- Visualização e exportação de logs em ZIP.

### Correções incorporadas

- Sincronização seletiva dos resultados de física para a deformação relatada
  dos personagens, preservando Readback 1.
- Seleção de saves na opção Carregar jogo.
- Proteções para fonte de transformações ausente e para resultados de colisão
  estática com índice de submesh inválido.
- Workers de preparação de draws em zero e remoção das opções experimentais
  da interface pública.

### Compatibilidade e observações

Android 11+, ambiente ARM64 e Vulkan. Perfil do jogo 01.00 validado pelo
executável; CUSA00552 é a referência e variantes regionais precisam do mesmo
binário reconhecido. Os testes se concentraram no Redmagic 10 Pro / Snapdragon
8 Elite. A build não inclui arquivos do jogo.

Esta é uma versão inicial: podem ocorrer pausas de compilação de shaders e
falhas específicas de algumas cenas. A campanha inteira no Android não foi
validada. A nova proteção do crash de submesh passou nos testes automatizados,
mas a repetição do trecho no aparelho ainda precisa confirmar seu resultado.
Nenhum ganho fixo de FPS ou compatibilidade universal é prometido.

Atualize por cima da build compatível anterior para manter os dados. Ao relatar
uma falha, exporte os logs e indique se ela ocorreu na execução atual ou anterior.

## English

**The Last of Us Remastered — Daedalus Android v0.1**

First public Android build, based on ravencaio's PC port, using an ARM64 Bionic
runtime, FEX CPU translation and Vulkan rendering.

### Release features

- PKG installation or extracted-folder selection, with automatic on-device preparation.
- TLOU-themed launcher and icon; touch and physical controller support.
- In-game menu opened by swiping right from the left edge; live fullscreen settings.
- Single-line FPS/GPU/CPU overlay with configurable placement.
- Original, 720p, 540p and 360p resolution profiles; optional graphics patches.
- 30/60 FPS limits and VSync.
- Bundled Turnip Gen8 V38 Patched for Adreno 8xx and Turnip 27.0.0 R1 for
  Adreno 7xx/6xx, plus driver selection and ZIP/SO import.
- Save ZIP import/export and compact save selection using actual metadata and
  game-provided thumbnails when available.
- Ten app languages, including Brazilian Portuguese, Turkish and Polish;
  system-language selection with English fallback.
- Log viewing and ZIP export.

### Included fixes

Selective physics-result synchronization addresses the reported character
deformation while retaining Readback 1. Save selection now handles Load Game.
Executable guards handle a missing transform source and invalid static ray-hit
submesh indices. Draw-preparation workers default to zero; experimental test
switches are removed from the public interface.

### Compatibility and status

Requires Android 11+, an ARM64 Android environment, Vulkan, and an owned game
dump matching the verified 01.00 executable profile. CUSA00552 is the reference;
regional variants require the same recognized binary. Gameplay testing has
focused on Redmagic 10 Pro / Snapdragon 8 Elite. No game files are included.

This initial build may still have shader-compilation pauses and scene-specific
issues. A complete Android playthrough has not been validated. The new submesh
crash guard passed automated tests; its effect on the reported device route
still needs confirmation. No fixed FPS gain or universal compatibility is claimed.

Install over a compatible earlier build to retain app data. Include exported
logs with bug reports and identify the current or previous execution.
