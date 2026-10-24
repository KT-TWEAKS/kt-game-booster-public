# CHANGELOG

## [1.0] — 2026-10-24

Primeira versão pública do KT Game Booster.

### Motor
- Execução 100% em modo usuário, sem driver, sem serviço e sem patch de
  memória: prioridade HIGH, timer 0,5 ms via `NtSetSystemInformation`,
  CPU Sets nos P-cores (API oficial), I/O e memória altas para o jogo.
- Governor autônomo por tick de 1 segundo com journal de snapshots: cada
  mudança é registrada e desfeita na ordem inversa — inclusive em Ctrl+C ou
  crash.
- Fundo em EcoQoS com limitação suave por CPU, trilha de rede em economia e
  lista de exceção editável.
- Planos de energia gaming: um plano para a sessão, o original restaurado na
  saída.
- Serviços não essenciais suspensos com justificativa visível e proteção de
  itens críticos.

### Detecção e presets
- 260+ executáveis embutidos com níveis de confiança: nome, caminho de loja
  (Steam, Epic, Riot, GOG, Xbox, Ubisoft, EA, Battle.net), launcher-chain e
  manifest do EGS.
- 61 títulos com preset automático (101 regras de EXE, contando variantes e
  alias): competitivos herdam `torneio`, jogos de mundo/campanha herdam
  `segundo plano` — sempre antes da trava de anticheat.
- Perfis `torneio` / `segundo plano` / `streaming` sobre o perfil global
  `gamemode.toml`, aberto e editável.

### Bench de latência v2
- Score composto 0–100 com pesos 35/35/30 (timer / scheduler / I/O).
- Timer real via ntdll, P95 de milhões de amostras de agendamento por sessão
  e leitura 4KB sem cache (`FILE_FLAG_NO_BUFFERING`).
- Mediana, P95 e pico sempre visíveis; medição que falha renormaliza os pesos
  e o card mostra honestamente `—`.

### Interface
- Painel WPF com abas Visão Geral / Processos / Otimizações / Jogos / Log,
  sparklines, log filtrável (TODOS / GAME / CORE / POWER / CONFIG / GUI),
  temas claro/escuro e badge de administrador.
- Modo jogo no painel: 20 fps de teto, sem animação nem sombra, com opção de
  sumir para a bandeja.
- Hotkeys: `Ctrl+Shift+O` motor · `Ctrl+Alt+G` torneio · `P` pausa ·
  `M` limpa RAM · `B` bench · `T` tema.

### CLI e update
- CLI completa no mesmo EXE: `--daemon`, `--dry-run`, `--list-games`,
  `--list-threads`, `--tournament` e flags por lever (`--no-power`,
  `--no-background`, `--no-eco-qos`, `--no-io-priority`,
  `--no-thread-power`).
- Update automático com SHA-256 publicado: consulta ao abrir e a cada 6
  horas, valida o hash antes de aplicar e preserva `gamemode.toml` e perfis.
  Canal público — download anônimo via REST, sem credencial no app.

### Site oficial
- Lançado com a release: `https://kt-game-booster-kelvenapks-projects.vercel.app/`
  (backup em `https://kt-tweaks.github.io/kt-game-booster-public/`).
