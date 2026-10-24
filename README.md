# KT Game Booster

![versão](https://img.shields.io/badge/vers%C3%A3o-1.0-9b7cff?style=flat-square)
![plataforma](https://img.shields.io/badge/Windows-10%20build%2019045%2B%20%C2%B7%2011%20x64-56d9e8?style=flat-square)
![licença](https://img.shields.io/badge/licen%C3%A7a-MIT-3fb950?style=flat-square)
![execução](https://img.shields.io/badge/modo-user--mode%20%C2%B7%20sem%20driver-9a97b0?style=flat-square)

Otimizador de jogos para Windows que roda inteiramente em **modo usuário**.
Quando um jogo abre, o KT detecta o processo, aplica o perfil daquele título
(prioridade, timer, CPU Sets, EcoQoS de fundo) e devolve tudo ao estado
original quando a sessão acaba. A **1.0 é a primeira versão pública** —
código aberto, licença MIT, sem driver e sem telemetria.

## Como funciona

1. **Detectar** — mais de 260 executáveis embutidos com níveis de confiança:
   nome, caminho de loja (Steam, Epic, Riot, GOG, Xbox, Ubisoft, EA,
   Battle.net), launcher-chain e manifest do EGS.
2. **Aplicar** — o perfil sobe em ordem verificada antes da trava de
   anticheat: prioridade HIGH, timer 0,5 ms, CPU Sets nos P-cores, I/O e
   memória altas, fundo em EcoQoS, plano de energia gaming.
3. **Monitorar** — governor por tick de 1 segundo, FPS monitor via ETW e log
   confrontado no painel.
4. **Restaurar** — cada mudança é registrada em um **journal de snapshots**
   e desfeita na ordem inversa, inclusive em Ctrl+C ou crash.

## O que a 1.0 faz

- **61 títulos com preset automático** (101 regras de EXE, contando
  variantes e alias) — competitivos entram com `torneio`, jogos de
  mundo/campanha com `segundo plano`.
- **Timer de 0,5 ms** via `NtSetSystemInformation` (o padrão do Windows é
  ~15,6 ms); o card TIMER do Bench mostra o valor real entregue.
- **CPU Sets** nos P-cores em máquinas híbridas, via API oficial.
- **EcoQoS de fundo**: apps de fundo caem no modo de eficiência de CPU e I/O
  enquanto o jogo roda, com lista de exceção editável.
- **Planos de energia gaming**: um plano para a sessão, o original na saída.
- **Serviços**: não essenciais suspensos com justificativa visível e
  proteção de itens críticos.
- **Modo jogo no painel**: 20 fps de teto, sem animação nem sombra, com
  opção de sumir para a bandeja.
- **Bench de latência v2**: score composto 0–100 (timer 35% / scheduler
  35% / I/O 30%), timer real via ntdll, P95 de milhões de amostras e leitura
  4KB sem cache — mediana, p95 e pico sem esconder nada.
- **Updater com SHA-256**: consulta o GitHub ao abrir e a cada 6 horas,
  baixa a próxima release, valida o hash antes de aplicar, preserva
  `gamemode.toml` e perfis. Canal público — download anônimo via REST, sem
  login e sem credencial embutida no app.
- **Restauração por journal**: reversível até em encerramento inesperado; a
  próxima inicialização restaura a sessão pendente antes de otimizar de novo.

## Interface

Painel WPF com cinco abas — **Visão Geral** (cards de estado: alvo,
limitados, suspensas, eventos ETW, RAM), **Processos** (tabela por CPU com
sparkline na linha do jogo), **Otimizações** (switches ao vivo + seletor de
perfil), **Jogos** (lista, exceções e Testar jogo) e **Log** (filtrável:
TODOS / GAME / CORE / POWER / CONFIG / GUI).

- Hotkeys: `Ctrl+Shift+O` liga/desliga o motor · `Ctrl+Alt+G` modo torneio ·
  `P` pausa · `M` limpa RAM · `B` roda o Bench · `T` troca o tema.
- Bandeja: Abrir / Pausar / Restaurar / Limpar RAM / Sair, com balão de
  início/fim de otimização.
- Temas claro/escuro e badge de administrador na sidebar.

## Perfis

`torneio` (`profiles/tournament.toml`) · `segundo plano`
(`profiles/bg_only.toml`) · `streaming` (`profiles/streaming.toml`) — todos
por cima do perfil global `gamemode.toml`, texto puro, comentado e editável.
Regra por título tem prioridade; sem regra, vale o perfil global escolhido
no seletor. Um bloco `[[process]]` também pode declarar
`profile = "streaming"` para associar um perfil a um executável.

## CLI

O mesmo EXE carrega um modo console completo:

```bat
KTGameBooster.exe                     REM roda e mostra a tabela de processos
KTGameBooster.exe --daemon            REM sem a tabela periódica
KTGameBooster.exe --dry-run           REM mostra o que faria, sem aplicar
KTGameBooster.exe --list-games        REM lista os títulos conhecidos
KTGameBooster.exe --list-threads      REM threads do jogo detectado
KTGameBooster.exe --tournament         REM perfil torneio na sessão
KTGameBooster.exe --no-eco-qos         REM desliga levers específicos
```

`Ctrl+C` restaura tudo e sai limpo.

## Requisitos

- Windows 10 (build 19045+) ou Windows 11, x64
- Privilégio de administrador (a UI pede uma vez)
- ~500 KB — sem runtime adicional

## Instalação e uso

1. Baixe o EXE único em [releases/latest] e confira o `.sha256`.
2. Execute como administrador.
3. Abra o jogo — detecção, preset e restauração são automáticas.

Para remover: feche o app (restauração imediata) e apague a pasta, ou
desinstale pelo Windows. A única entrada de sistema é uma chave HKCU de
auto-início, removida pelo próprio app.

## Build

```bat
build.bat
:: ou via Python (bypass de vars do Git-Bash/MSYS):
python build.py
```

Pacote portátil + SHA-256: `.\scripts\package-release.ps1 -Version 1.0` ·
Instalador NSIS: `.\scripts\package-installer.ps1 -Version 1.0`. O
empacotamento roda um smoke test isolado e só cria o ZIP quando aplicação,
Governor e restauração terminam com sucesso.

Requer **VS Build Tools 18** (`cl` x64 + Windows SDK) e **.NET Framework
4.8** (targeting pack + SDK do MSBuild); o caminho automático via `vswhere`
cobre a instalação.

## Links

- **Site oficial**: <https://kt-game-booster-kelvenapks-projects.vercel.app/>
- **Download**: [releases/latest](https://github.com/KT-TWEAKS/kt-game-booster-public/releases/latest)

## Documentação

- [Guia rápido](docs/GUIA-RAPIDO.md)
- [Arquitetura](docs/ARQUITETURA.md)
- [Configuração](docs/CONFIGURACAO.md)
- [Segurança](docs/SEGURANCA.md)
- [Testes](docs/TESTES.md)
- [API da engine](docs/API.md)
- [Changelog](docs/CHANGELOG.md)

## Avisos

- **Shell do Windows**: o app não encerra Explorer, DWM ou suspende winlogon —
  por segurança e por ser flag conhecida de anticheat.
- **Elevação**: prioridade, CPU Sets e timer dependem de permissões elevadas;
  o badge verde na sidebar confirma execução como administrador.
- **Anticheat**: em sessões com AC de kernel, o modo seguro derruba
  limitadores e suspensões e usa apenas APIs user-mode comuns. Nada de
  injeção de input.
- **Autor** — [kelvenapk](https://github.com/kelvenapk).

## Licença

MIT — veja [`LICENSE`](LICENSE).

[releases/latest]: https://github.com/KT-TWEAKS/kt-game-booster-public/releases/latest
