# KT Game Booster v1.0 — primeira versão pública

Primeira release pública do KT Game Booster, com foco em ganho real de
desempenho, segurança de restauração e uma interface moderna para
Windows 10/11.

## Destaques

- Motor em modo usuário: prioridade HIGH, timer 0,5 ms via
  `NtSetSystemInformation`, CPU Sets nos P-cores (API oficial) e I/O e memória
  altas para o jogo — tudo em APIs públicas do Windows, sem driver.
- Governor autônomo por tick de 1 segundo: o perfil da sessão entra sozinho
  quando o jogo é detectado e o estado original é restaurado ao sair —
  inclusive em Ctrl+C ou crash, via journal de snapshots.
- 61 títulos com preset automático (101 regras de EXE): competitivos herdam
  `torneio`, jogos de mundo/campanha herdam `segundo plano`, antes da trava de
  anticheat.
- Fundo em EcoQoS com limitação suave por CPU e lista de exceção editável.
- Bench de latência v2: score composto 0–100 (timer 35% / scheduler 35% /
  I/O 30%), P95 de milhões de amostras de agendamento e leitura 4KB sem cache.
- Painel WPF com abas Visão Geral / Processos / Otimizações / Jogos / Log,
  sparklines, log filtrável, temas claro/escuro e modo jogo econômico
  (20 fps de teto, bandeja opcional).
- CLI completa no mesmo EXE: `--daemon`, `--dry-run`, `--list-games`,
  `--tournament` e flags por lever.
- Update automático com SHA-256, preservando `gamemode.toml` e perfis —
  canal público, download anônimo via REST, sem credencial no app.
- Detecção aprimorada, inclusive quando minimizada, com proteção contra
  falsos positivos de aplicativos comuns (Spotify, Discord e OBS nunca são
  tocados).

## Validação deste pacote

- Build Release x64 aprovado sem erros.
- Smoke isolado aprovado: prioridade `Idle → High → Idle`.
- Governor de memória aprovado nos três níveis.
- ETW aprovado com recebimento real de eventos.
- Serviço SysMain validado em `Rodando → Parado → Rodando` durante sessão
  isolada.
- Instalador automático validado em pasta isolada: aplicação, reinício e
  preservação de `gamemode.toml`/perfis aprovados.
- Conteúdo do ZIP e SHA-256 conferidos.

Requisitos: Windows 10 (build 19045+) ou Windows 11, x64, privilégio de
administrador (pedido uma vez). Licença MIT — veja [`LICENSE`](../LICENSE).
