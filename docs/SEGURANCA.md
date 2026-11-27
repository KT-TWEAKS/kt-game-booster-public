# SEGURANÇA E LIMITAÇÕES

## Permissões

- Requer **Administrador** para a maioria das operações (manifesto
  `requireAdministrator` na GUI; a CLI também precisa para C-States, working
  set e prioridades).
- O executável usa privilégio de debug (`SeDebugPrivilege`) para abrir
  processos de terceiros.
- Em Windows 11 24H2+, o modo de usuário (sem driver) cobre: prioridade,
  afinidade, I/O, power plan, turbo, core parking, page/memory priority,
  MMCSS, EcoQoS, C-States e suspensão de threads de fundo.

## Restauração de estado

- Saída **normal** (Ctrl+C, botão Sair, Restaurar, `KT_Stop`, jogo
  encerrado) restaura: prioridades, afinidade, I/O, page/memory priority,
  working set, GPU, threads de fundo suspensas, C-States, power plan e
  serviços GameInput.
- **Kill forçado** deixa alterações até o reboot (comportamento herdado do
  binário original): principalmente C-States/plano de energia e serviços
  GameInput suspensos.

## Anti-cheat

- Anti-cheats (EAC, BattlEye, Vanguard) podem detectar ou reverter ajustes de
  prioridade. O KT Game Booster reaplica a prioridade da regra a cada ciclo para
  cobrir resets, mas o usuário é responsável por cumprir as regras do jogo.
- Whitelists de anti-cheat estão embutidas e nunca são otimizadas.
- O modo **Tournament** aumenta a visibilidade de ajustes — evitar em competições
  com anti-cheat estrito (use `bg_only` nesses casos).

### Modo seguro anticheat (v2.4, default ON)

- Quando a sessão identifica um jogo protegido por anti-cheat de cliente/kernel
  (Valorant/Vanguard, Fortnite/Apex/Rust/EAC, BattlEye, CoD/Ricochet, FACEIT/
  ESEA, nProtect/XignCode, Tencent/ACE, Roblox/Hyperion e afins), o motor
  desliga **só por essa sessão** os levers que interferem em processos de
  terceiros — exatamente os padrões que a telemetria AC acompanha:
  `suspend_gameinput_threads`, `pause_services_gaming`, `game_tree_boost`,
  `background_limiter`, `trim_background_ws`,
  `thread_power_background_throttle`.
- Continuam ativos os levers **user-mode aplicados ao próprio jogo** (prioridade,
  afinidade, MMCSS, I/O/memory priority do jogo, Game DVR off, plano de
  energia), que não sobem serviço/suspensão/limiter e não tocam threads de
  terceiros.
- O gatilho é a identidade do jogo, não a presença de AC no sistema:
  `vgc`/`BEService` ficam residentes com o jogo fechado e disparariam o modo
  seguro para jogos limpos.
- Per-game profiles: quando um jogo AC carrega um perfil dedicado, o modo seguro
  re-sanitiza o perfil no carregamento, vencendo qualquer chave agressiva que
  ele tenha.
- Painel: chip **"ANTICHEAT (<família>) · MODO SEGURO"** na Dashboard confirma
  a sessão reduzida; o toggle "Sequência segura p/ anticheat" liga/desliga o
  comportamento (default ON, embutido no TOML).
- Detecta-se sem executar nada no jogo: zero injeção, zero `SendInput`, zero
  driver durante a sessão AC.

### Smoke anticheat

- `scripts/smoke-engine.ps1` tem uma segunda passagem que inicia um probe
  renomeado para `valorant-win64-shipping.exe` e valida: detecção da sessão AC,
  `anticheatSafe=1` publicado, zero processo de fundo limitado, prioridade High
  mantida para o jogo e restauração de prioridade idêntica à original.

## Heurística de detecção

- Janela foreground **sem menu** + módulos de engine conhecidos (match por nome
  exato) + heurística de fullscreen. Rejeita `WS_CHILD` e janelas com menu.
- Conservadora — reduz falsos positivos (ex.: Spotify, VLC).

## Segredos / segurança do código

- Nenhum segredo ou chave no código-fonte.
- A API exportada só lê/controla o core do próprio processo.

## Limitações conhecidas

| Item | Status |
|---|---|
| ETW | Pode falhar em Windows reduzidos; fallback por polling. |
| Tournament + hot-reload | Numa troca de config o arquivo vence o toggle em memória. |
| RAM limpa | `TrimWorkingSet`; o jogo ativo nunca é trimmado. |
| Tema claro | Funciona mas alguns ícones SVG deixam de ter contraste ideal. |
| Colunas extras (PRI/CORE) | Usam `GetPriorityClass` e `GetProcessAffinityMask` — fallback `-` se falhar. |

## Pipeline de build

- Assinar no CI: `signtool sign /fd sha256 /tr <timestamp> /td sha256 /as`.
- Manter o EV **fora de agentes** (usar HSM cloud / Azure Code Signing).
- Build de teste com `signtool sign /sps` + boot em `Test Mode` enquanto o
  WHQL não estiver pronto.
