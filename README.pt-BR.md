# The Last of Us Remastered — Daedalus Android

Adaptação do **The Last of Us Remastered de PS4 para Android**, baseada no
[port de PC de ravencaio](https://github.com/ravencaio/tlou-remastered-pc), com
runtime Bionic ARM64, tradução de CPU pelo FEX e renderização Vulkan.

**Versão 0.1 · Primeira build pública para Android**

[English](README.md) · [Notas da versão](RELEASE_NOTES.md) ·
[Releases](https://github.com/erickmendes22/tlou-remastered-android/releases) · [Código Android](https://github.com/erickmendes22/tlou-remastered-android/tree/tlou-ui-approval-log32-20261105) ·
[YouTube — @daedalusemulator](https://www.youtube.com/@daedalusemulator)

## Recursos

- Launcher e ícone com tema de The Last of Us; preparação automática no celular.
- Instalação por PKG ou seleção de uma pasta de jogo já extraída.
- Controles na tela e suporte a controle físico.
- Menu durante o jogo pelo gesto da borda esquerda para a direita.
- Modos de tela cheia ajustáveis durante o gameplay.
- HUD de FPS/GPU/CPU em uma linha, com posição e visibilidade configuráveis.
- Resolução original e patches de 720p, 540p e 360p; patches opcionais para SSAO,
  motion blur, profundidade de campo, bloom, sombras solares e subsurface.
- Limites de 30 ou 60 FPS e configuração de VSync. O limite de 60 FPS não
  significa que todas as cenas ou aparelhos atingirão essa taxa.
- Drivers Vulkan incluídos e importação de drivers ZIP ou SO.
- Importação/exportação de saves em ZIP; seletor de saves com metadados reais
  dos slots e miniaturas quando fornecidas pelo jogo.
- Interface em português brasileiro, inglês, espanhol, francês, alemão,
  italiano, turco, polonês, russo e hindi. O idioma padrão segue o sistema,
  com fallback para inglês. O idioma do jogo é escolhido dentro dele.
- Visualização e exportação de logs em ZIP.

As configurações de desempenho selecionadas nos testes ficam aplicadas
internamente. Os controles experimentais de medição foram retirados da interface.

## Requisitos e versão do jogo

- **Android 11 ou superior**, com ambiente Android ARM de 64 bits e suporte a Vulkan.
- Uma cópia própria do jogo de PS4. O perfil Android validado é o executável
  **01.00**, SHA-256
  `e5f0aa322f60bd90637cc10d83de08c77885b1921084021c630e04ed3d6febf7`.
- O título de referência é **CUSA00552**. Outras regiões são reconhecidas quando
  usam o mesmo executável validado; apenas o código CUSA não garante os patches.
- Espaço para o jogo extraído. A instalação por PKG precisa de espaço para a
  extração enquanto o pacote original ainda está armazenado.

Os testes de gameplay no Android se concentraram no **Redmagic 10 Pro,
Snapdragon 8 Elite e Adreno 830**. A cobertura de outros aparelhos está em
expansão. Os requisitos de jogo 01.11 e os resultados de FPS do port de Windows
não são os requisitos nem as medições desta build Android.

| Família da GPU | Driver incluído por padrão |
| --- | --- |
| Adreno 8xx | Turnip Gen8 V38 Patched |
| Adreno 7xx / 6xx | Turnip 27.0.0 R1 |

É possível selecionar ou importar outro driver nos ajustes. Essa seleção
padrão não confirma compatibilidade com todos os modelos dessas famílias.

## Como instalar

1. Instale o APK distribuído pelo projeto. Para atualizar uma build compatível,
   instale por cima da anterior, preservando os dados e saves.
2. Conceda o acesso a arquivos solicitado e selecione a pasta extraída do jogo
   ou use a instalação por PKG no launcher.
3. Aguarde a preparação automática e toque em **Jogar**.
4. Ajuste resolução, patches e controles. Durante o jogo, deslize da borda
   esquerda para a direita para abrir o menu.

Exporte os saves antes de desinstalar o aplicativo ou limpar os dados dele.

## Estado da versão 0.1

Esta é uma primeira build pública com gameplay demonstrado em Android.
A compatibilidade completa e a campanha inteira ainda não foram validadas no port.

- A compilação de shaders pode causar pausas, principalmente ao entrar em uma
  área pela primeira vez.
- A sincronização seletiva dos resultados de física trata a deformação
  relatada dos personagens mantendo **Readback 1**. Esta build não usa Readback 2.
- A proteção do processamento de colisões estáticas evita o acesso ao submesh
  inválido identificado no crash. Ela mantém os resultados válidos e descarta
  apenas o inválido. A proteção passou nos testes automatizados; falta confirmar
  o mesmo trecho no aparelho que apresentou a falha.
- Ainda podem existir problemas gráficos, de colisão ou de estabilidade em cenas específicas.

Ao relatar um problema, use **Sobre → Exportar logs ZIP**, informe a cena,
passos, aparelho, GPU, driver, versão do jogo e configurações. Indique se a falha
está na execução atual ou na anterior do ZIP.
[Relatar problema](https://github.com/erickmendes22/tlou-remastered-android/issues).

## Desenvolvimento, créditos e licença

O código Android e o fluxo de build estão na
[árvore de fontes Android](https://github.com/erickmendes22/tlou-remastered-android/tree/tlou-ui-approval-log32-20261105). Consulte o [guia de build](android/README.md).
Alterações apenas na documentação não exigem gerar outro APK.

Adaptação Android: **Erick Mendes / Daedalus Emulator**. Referência de PC:
**ravencaio/tlou-remastered-pc**. Tradução de CPU: **FEX-Emu**. Drivers:
**Mesa/Turnip e libadrenotools**. A base também inclui contribuições de
BilalReloaded / bc160201983, FelipePrado0 / SpartanPort, SuperMedo, HayatoG e
shadPS4. Os demais componentes preservam suas próprias licenças. Os créditos
detalhados herdados estão na [documentação histórica de PC](docs/PC_PORT_REFERENCE.md).

Código sob **GPL-2.0-or-later**; consulte [LICENSE](LICENSE). Nenhum arquivo do
jogo é distribuído no repositório ou APK. Projeto não oficial, sem vínculo com
Sony Interactive Entertainment ou Naughty Dog.
