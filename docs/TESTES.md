# Verificação da versão 1.0

Este projeto combina uma interface WPF x64 (`KTGameBooster.exe`) com uma engine
C++ carregada em processo (`ktengine.dll`). A validação de lançamento cobre
compilação, contrato nativo, aplicação dos ajustes e restauração do Windows.

## Testes automatizados

| Área | Comando | O que valida |
|---|---|---|
| Build Release | `build.bat final` | Engine C++, WPF x64 e pasta `release/` |
| Smoke do motor | `scripts\smoke-engine.ps1` | Detecção isolada, prioridade, Governor de memória e restauração |
| Fastpath ETW | `scripts\smoke-engine.ps1 -RequireEtw` | Sessão ETW ativa e recebimento de eventos |
| Serviços | `scripts\smoke-services.ps1` | Parada controlada do SysMain e restauração exata ao encerrar a sessão |
| Pacote | `scripts\package-release.ps1 -Version 1.0` | Build, smoke, arquivos obrigatórios, ZIP e SHA-256 |

O smoke test copia um executável inofensivo do Windows para um nome único,
força apenas esse alvo como jogo e desativa os recursos invasivos. Ele compara
o estado do processo antes, durante e depois da sessão. Assim, não depende de
um jogo instalado e detecta regressões de restauração.

O smoke mantém ligado o conjunto do Governor automático (I/O alta, memória
alta, MMCSS e page priority) enquanto isola os demais tweaks.

O smoke valida também o campo **`gameProfile`** (state v2.5): na passada do
probe anônimo o campo só pode ficar vazio ou `streaming` (só o fallback por app
de captura), e na passada anticheat ele deve publicar `tournament` — prova de
que a regra por título é carregada e propagada no estado antes do modo seguro.

## Update automático

- Abrir **Sobre** e confirmar `Atualizado · verificação automática pelo GitHub ligada`.
- A consulta é feita na abertura e repetida a cada seis horas.
- Releases novas são baixadas do repositório público via REST anônimo (sem login).
- O ZIP só é aceito quando o SHA-256 confere com o arquivo publicado.
- A instalação preserva `gamemode.toml` e arquivos existentes em `profiles/`.

## Validação estática da interface

- O MSBuild compila o XAML e todos os handlers vinculados.
- A janela transparente implementa resize nativo por `WM_NCHITTEST` e trata
  `WM_GETMINMAXINFO` com a área útil do monitor atual ao maximizar.
- A barra de título mantém os botões clicáveis e a área restante arrastável.
- A janela usa transparência real por pixel; sidebar, top bar e cards preservam
  texto opaco enquanto revelam o conteúdo externo atrás da aplicação.
- Cards, sidebar e top bar usam tintas translúcidas, bordas e sombras do mesmo
  conjunto de tokens em `Styles.xaml`.
- Quando o backdrop nativo não está disponível, luzes difusas internas mantêm
  profundidade e contraste entre as superfícies translúcidas.

## Bench de latência (v2)

O bench combina três medições reais e um score composto. Nada é simulado.

1. **Timer (NtQueryTimerResolution, ntdll, UI-side)** — resolução atual do
   timer do sistema em unidades de 100 ns (10000 = 1 ms). Com o tweak Timer
   0.5 ms ativo o card mostra ~0.500 ms; no padrão do Windows ~15.625 ms.
2. **Scheduler P95 (motor, `KT_RunBench`)** — milhares de medições de jitter
   por tick (a mesma métrica de sempre; o histórico/sparkline continua usando
   o P95 do scheduler em ms).
3. **I/O P95 (UI-side, `ktengine.dll`)** — 48 leituras de 4 KB com buffer
   alinhado e `FILE_FLAG_NO_BUFFERING` (bypass da cache do Windows) na DLL do
   motor, latências medidas por `Stopwatch` de alta resolução; reporta o P95
   em µs (NVMe QD1 tipicamente 30–150 µs).

**Score composto 0–100**: timer 35% + scheduler 35% + I/O 30% (se uma
medição falha, o peso renormaliza). Curvas de referência interpoladas:
timer 0.5 ms→100 … 15.6 ms→30; scheduler 50 µs→96 … 2 ms→32; I/O 30 µs→96
… 3 ms→22. Classificação: ≥85 excelente, ≥70 bom, ≥50 média, <50 sensível.
O card do score recebe a cor da classificação (verde/âmbar/vermelho).

- Verificação feita via UI Automation: abrir a página Bench, invocar
  `btnRunBench`, ler os cards — sessão de referência: score **86
  (excelente)**, timer 0.999 ms, scheduler P95 0.00 ms, I/O P95 177 µs.
- A ABI do motor não mudou (`KT_RunBench` intacto); todas as métricas novas
  são computadas no processo da UI, sem toques em ntdll além da consulta
  `NtQueryTimerResolution` (somente leitura).

## Modo jogo no painel

Queda de FPS relatada com o painel aberto durante partida. O modo jogo corta
todo custo de renderização quando o motor detecta um jogo:

1. Grid de glows (`bgFx`) colapsado — os três ellipses com `BlurEffect`
   (raio 72–96, animados) param de ser rasterizados.
2. `Effect` de todo o visual tree salvo e anulado (sombras de cards, Glow
   de botões); restaurado elemento por elemento quando a sessão termina.
3. Animações de respiração do fundo limitadas a 20 fps
   (`Timeline.DesiredFrameRate="20"` na storyboard).
4. Toggle **"Modo jogo: painel leve na bandeja"** (padrão ON, salvo em
   `HKCU\Software\KTGameBooster\HidePanelOnGame`): esconde a janela para a
   bandeja com balão "Modo jogo · painel na bandeja"; reabre pelo ícone.
   Desmarcar o toggle durante sessão reabre o painel imediatamente (já em
   modo econômico).

- Disparo: transição `gameActive` no tick de 800 ms (`ApplyGameUiMode`).
- Idempotente (não reaplica) e tolerante a falhas (try/catch em cada etapa).
- A janela não é reaberta automaticamente ao fim do jogo; balão avisa e o
  ícone da bandeja reabre — evita roubar foco durante a play.

## Presets automáticos por jogo

61 títulos (101 regras de EXE — variantes/alias têm regra própria) têm
perfil automático em `config/gamemode.toml`
(`[[process]] exe + profile`):

1. **Tournament** (45 regras · 25 títulos): competitivos — Valorant, CS2,
   Apex, Fortnite, EFT, R6 Siege, OW/OW2, PUBG, Warzone/CoD, LoL, Destiny 2,
   Rust, Rocket League, Deadlock, The Finals, Marvel Rivals, Delta Force,
   Hunt, Insurgency, Ready or Not, Squad, Hell Let Loose.
2. **bg_only** (56 regras · 36 títulos): mundo/campanha/sim — Elden Ring,
   Souls, Nightreign, RDR2, Starfield, Skyrim, Fallout 4, BG3, Valheim,
   Stardew, Monster Hunter (World/Wilds), STALKER 2, Witcher 3, Cyberpunk,
   PoE 1/2, Diablo IV, FFXIV, Minecraft, Euro Truck/American Truck, MSFS,
   Black Myth Wukong, Space Marine 2, Kingdom Come 2, Ghost of Tsushima,
   TLoU Part I, Hogwarts Legacy, Palworld, Sons of the Forest,
   ARK Ascended/Evolved, Helldivers 2, Once Human.

O que conferir ao editar as listas (sem rodar o motor):

- Nome de EXE em minúsculas (o parser normaliza por `WLower` de qualquer
  forma; anote igual ao detect.cpp para títulos conhecidos).
- Regra só de `exe` + `profile` é inerte nas passagens de prioridade/
  afinidade — só carrega o perfil. Prioridade do jogo continua default
  High (ou pelo alvo de per-game perfil próprio).
- Anticheat-safe é aplicado DEPOIS do preset — títulos de AC online mantêm
  apenas alavancas user-mode mesmo dentro do torneio.

## Checklist manual recomendado

1. Abrir o pacote em Windows 10 e Windows 11.
2. Alternar todas as páginas e confirmar ausência de cortes em DPI 100%, 125%
   e 150%.
3. Redimensionar, maximizar, restaurar e mover a janela entre dois monitores.
4. Minimizar para a bandeja durante um jogo e conferir a notificação de início
   e fim da otimização.
5. Testar um jogo DX11 e um DX12 por pelo menos 20 minutos.
6. Encerrar o jogo e o aplicativo e confirmar plano de energia, prioridade,
   afinidade, EcoQoS, serviços e shell restaurados.
7. Repetir uma vez como usuário comum e outra como administrador.

## Critérios de aprovação

- Nenhum erro de compilação ou warning do código do produto.
- Smoke test e ETW terminam com código zero.
- O processo de teste recupera exatamente sua prioridade original.
- O ZIP contém apenas executável, engine, configuração, perfis, ícones e README.
- A versão visível e os metadados do assembly são `1.0`/`1.0.0.0`.
- A release é publicada no repositório público; o app atualiza anonimamente.
