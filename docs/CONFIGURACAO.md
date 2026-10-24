# CONFIGURAÇÃO

O KT Game Booster carrega `gamemode.toml` do diretório atual. Se o arquivo não existir
ou for inválido, usa a configuração embutida (`EmbeddedDefaultConfig`). A GUI
salva switches em `HKCU\Software\KTGameBooster` e **aplica por cima** do TOML.

> Importante: valores do registro (GUI) têm precedência sobre o TOML.

## `[config]`

| Chave | Tipo | Default | Descrição |
|---|---|---|---|
| `poll_interval_ms` | int (≥50) | 1000 | Intervalo do ciclo de monitoramento. |

## `[features]`

| Chave | Default | Descrição |
|---|---|---|
| `dwm_mmcss` | true | MMCSS no DWM (agendamento multimídia). |
| `dwm_thread_priority` | true | Prioridade das threads do DWM. |
| `gpu_priority` | true | Prioridade de GPU do jogo (`D3DKMT`). |
| `background_limiter` | true | **Master switch** de toda limitação de fundo. |
| `thread_power_background_throttle` | true | Throttle de energia nas threads de fundo. |
| `thread_power_game_unthrottle` | true | Unthrottle de energia nas threads do jogo. |
| `io_priority_background` | true | I/O VeryLow no fundo. |
| `io_priority_game_high` | false | I/O High para o jogo (toggle da GUI). |
| `eco_qos_background` | true | EcoQoS (throttle) no fundo. |
| `suspend_gameinput_threads` | true | Congela serviços GameInput durante a partida. |
| `game_tree_boost` | true | Boost da árvore (processos filhos do jogo): prioridade + EcoQoS off + page priority. |
| `pause_services_gaming` | false | Pausa/para serviços opcionais na partida e restaura as mudanças exatas. |
| `anticheat_safe_mode` | true | **Modo seguro anticheat**: sessão de jogo com AC desliga suspensões/serviços/limiter automaticamente (ver [SEGURANCA.md](SEGURANCA.md)). |
| `disable_c_states` | true | Desliga C-States via `PowerWriteACValueIndex`. |
| `disable_priority_boost` | true | `SetThreadPriorityBoost` nas threads do jogo. |
| `high_performance_plan` | true | Ativa o plano "Alto desempenho" no start e restaura no exit. |
| `turbo_boost` | true | Boost mode Aggressive + policy 100% (user-mode). |
| `core_parking_disable` | true | Min core parking 0 / max 100 (user-mode). |
| `mmcss_game` | false | Threads do jogo como críticas (MM critical) via `NtSetInformationThread`. |
| `page_priority_game` | false | Page priority das threads do jogo (17). |
| `memory_priority_background` | true | Memory priority Low (1) no fundo. |
| `memory_priority_game_high` | false | Memory priority High (7) para o jogo. |
| `tournament_mode` | false | Perfil de FPS: mantém prioridade High e força MMCSS, page priority e memória high para o jogo, sem prioridade `realtime`. |
| `background_cpu_pin` | false | Pin de CPU de fundo (schema). |
| `boost_input_threads` | false | Time-Critical em threads com "Input" no nome. |

> Power (plano/turbo/parking): como os C-States, aplicado **no start** e restaurado
> no exit/shutdown — não apenas durante o jogo. Tudo user-mode; sem driver
> necessário. Na VM as gravações ACPI podem falhar sem prejuízo.

Os controles experimentais foram removidos. Chaves antigas de perfis/TOML para
`memory_lock`, `pin_perf_cores`, `network_throttling_disable`,
`timer_resolution_500us`, `cpu_sets_pin`, `kill_shell_processes`,
`extreme_shell_mode`, `extreme_kill_dwm`, `auto_clean_ram` e `fso_disable` são
ignoradas e mantidas desativadas ao iniciar ou atualizar a configuração. A
limpeza manual de RAM continua disponível; a limpeza automática foi removida.

## `[background_cpu]`

| Chave | Default | Descrição |
|---|---|---|
| `auto_load` | false | Reservado. |
| `main_frame_cpu_pct` | 20 | Quota de CPU % para a main thread do fundo. |
| `worker_cpu_pct` | 3 | Quota de CPU % para demais threads do fundo. |
| `cpus` | `[]` | Núcleos permitidos para o fundo (schema). |

## `[[game]]`

Jogos extras, também editáveis pela GUI ("Adicionar jogo..."). O `exe`
(case-insensitive) suporta wildcards `*` (match via `WildMatch`) — útil para
famílias de launchers ou nomes renomeados/packed.

```toml
[[game]]
exe = "meujogo.exe"

[[game]]
exe = "mygame*.exe"      # wildcard cobre variações como mygame_dx12.exe
```

## `[[process]]`

Duas funções: **alvo do jogo** (tem `priority_class`) e **proteção de fundo**
(só o `exe`, sem `priority_class` → nunca é limitado).

| Chave | Tipo | Descrição |
|---|---|---|
| `exe` | str | Nome do executável (case-insensitive, com sufixo `.exe`). |
| `path` | str | Caminho completo (case-insensitive, substring) — aplicado no match. |
| `priority_class` | str | `idle`/`below_normal`/`normal`/`above_normal`/`high`/`realtime`. |
| `action` | str | `suspend`/`idle`/`below_normal`/`lowest`. |
| `affinity_cores` | int[] | Máscara manual: `[0,1,2,3]`. |
| `affinity_ccd` | int | CCD/CCX (AMD Ryzen): `-1` (todos) ou índice. |
| `affinity_smt` | bool | Com `affinity_ccd`: `false` deixa um thread por núcleo. |
| `ideal_processor` | int | Núcleo ideal; `-1` = auto (com CCD usa o ideal da topologia). |
| `eco_qos` | bool | Força EcoQoS no alvo (schema). |
| `io_priority` | bool | Força I/O no alvo (schema). |

```toml
[[process]]
exe = "cs2.exe"
priority_class = "high"
affinity_ccd = 0        # só o primeiro CCD
affinity_smt = false    # sem SMT (1 thread por núcleo físico)
ideal_processor = -1    # auto: melhor núcleo do CCD
```

## `[[threads]]`

Regras por nome de thread (suporta `*`). Para **fundo**, só regras com
`action` são consideradas (prioridades de jogo nunca tocam no fundo).

| Chave | Tipo | Valores |
|---|---|---|
| `thread_name` | str | Nome com curinga: `RenderThread*`, `AK*`. |
| `priority` | str | `time_critical`/`highest`/`above_normal`/`normal`/`below_normal`/`lowest`/`idle`. |
| `action` | str | `suspend`/`idle`/`below_normal`/`lowest`. |

```toml
[[threads]]
thread_name = "RenderThread*"
priority = "time_critical"
action = "none"

[[threads]]
thread_name = "IoService*"
action = "below_normal"
```

## Registro (`HKCU\Software\KTGameBooster`)

A GUI persiste/restaura estes valores (DWORD 0/1):

`poll_interval_ms`, `auto_start`, `start_minimized`,
`background_limiter`, `eco_qos_background`, `io_priority_background`,
`thread_power_background_throttle`, `thread_power_game_unthrottle`, `dwm_mmcss`,
`dwm_thread_priority`, `gpu_priority`, `io_priority_game_high`,
`disable_c_states`, `suspend_gameinput_threads`,
`high_performance_plan`, `turbo_boost`, `core_parking_disable`, `mmcss_game`,
`page_priority_game`, `memory_priority_background`, `memory_priority_game_high`,
`tournament_mode`, `background_cpu_pin`, `boost_input_threads`,
`anticheat_safe_mode`,
active profile in `active_profile` (REG_SZ), dark theme in `dark_theme` (DWORD 0/1),
and the list of games in `custom_games`.

## Perfis (hot-reload)

Coloque arquivos `.toml` em `profiles/` ao lado do executável (ex.: `profiles/bg_only.toml`,
`profiles/tournament.toml`). A GUI os lista no seletor de perfil (setas no topo do
painel OTIMIZAÇÕES). O perfil `default` corresponde ao `gamemode.toml` raiz.

- O perfil ativo é persistido em `active_profile` e reaplicado no próximo start.
- **Hot-reload**: editar o arquivo do perfil ativo é detectado por mtime; o booster
  troca a config sem reiniciar (~a cada 10 ciclos). Toques da GUI são reaplicados
  por cima do arquivo a cada reload.
- Nota: `tournament_mode` do arquivo vence sobre o toggle da GUI num reload.

## Detecção de jogos

1. **Lista conhecida** (`kKnownGames`, ~343 entradas) → O(1) via `KnownGameSet`.
2. **Jogos custom** (`[[game]]` + lista da GUI em `custom_games`) → `MatchGameName`,
   com wildcards `*`.
3. **Heurística foreground**: janela foreground **sem menu**, não-child e
   (fullscreen ou top-level normal) + evidência de jogo:
   - módulos de engine carregados (`HeuristicEngineModules`, com fallback
     `PROCESS_QUERY_LIMITED_INFORMATION` quando `VM_READ` é negado por
     anti-cheat), **ou**
   - caminho da imagem em root de loja conhecida (`HeuristicGamePath`:
     Steam/Epic/Riot/Battle.net/Ubisoft/EA/GOG/WindowsApps).
4. **Testar jogo** (`KT_SetTestTarget`): força um `.exe` no próximo ciclo
   (one-shot), bypassando tudo.
5. Empate → prioriza o processo em foreground.

> A heurística de path é especialmente útil para **UWP/MS Store/Game Pass**
> (nomes genéricos como `app.exe` sob `WindowsApps`) e launchers que negam
> enumerar módulos. Jogos adicionados na GUI agora também valem para o CLI.

Whitelists que nunca são otimizadas/limitadas: processos de sistema, anti-cheat
(EAC/BE/Vanguard, nprotect, hoyo_protect, anticheatlauncher, …) e os
`[[process]]` sem `priority_class` (ex.: `explorer.exe`, `MsMpEng.exe`,
navegadores, GameInput).
# Perfis por jogo

O perfil global é selecionado na interface. Para escolher um perfil por executável, adicione `profile = "normal"`, `"tournament"`, `"streaming"` ou `"bg_only"` ao respectivo bloco `[[process]]` do `gamemode.toml`. O perfil só é carregado depois de uma detecção confiável; launchers e candidatos não acionam a troca.
