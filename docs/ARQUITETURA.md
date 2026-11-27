# ARQUITETURA

## Visão geral

```
                    ┌──────────────────────────────┐
                    │        HOST (UI)              │
                    │  CLI main.cpp  ou  GUI kt_gui │
                    └──────────────┬───────────────┘
                                   │ Start/Stop/Pause/UpdateConfig
                                   │ + polls BoosterState (atomics)
                    ┌──────────────▼───────────────┐
                    │        BoosterCore            │  thread de trabalho
                    │  detect -> apply -> restore   │
                    └──┬────┬────┬────┬────┬────┬──┘
                       │    │    │    │    │    │
                   config detect procopt threadopt powersys cpulim etw
```

- **BoosterCore** roda em uma thread própria; a UI só lê `BoosterState`
  (campos `std::atomic` + `Config` sob `std::mutex`) e envia comandos.
- A **CLI** usa o mesmo `BoosterCore`, com um loop de
  polling leve — elimina a duplicação de codigo que existia quando `main.cpp`
  tinha funcoes estaticas equivalentes.
- **Restore completo** no `Stop()`/Ctrl+C/saída: prioridades, afinidade,
  threads, C-States, power plan, working set, I/O, GPU e processos de fundo.

## Pipeline por ciclo (`poll_interval_ms`)

1. **Sem jogo** → `DetectGamePids`:
   - lista conhecida (`kKnownGames` / `MatchGameName`) e
   - heurística (janela foreground sem menu **e** módulos de engine).
   - Em caso de empate, prioriza o processo em foreground.
2. **Jogo encontrado** → abre handle, salva estado original, aplica:
   - prioridade da regra `[[process]]` (default High)
   - afinidade por `affinity_cores` **ou** `affinity_ccd`/`affinity_smt`
     (`BuildTopologyMask` via `GetLogicalProcessorInformationEx`)
   - `ideal_processor` (por thread, se pedido)
   - prioridade GPU (`D3DKMTSetProcessSchedulingPriorityClass`), I/O High
     (`io_priority_game_high`), EcoQoS off e prioridade de memória
   - otimização de threads por regra `[[threads]]` (nome com `*`)
   - suspensão do GameInput (`suspend_gameinput_threads`)
3. **Fundo**: monta lista (excluindo sistema/anti-cheat/config/jogos) e limita:
   Idle + I/O VeryLow + EcoQoS + `Cpulim` (suspensão temporária por quota).
4. **Monitor**: reaplica a prioridade da regra a cada ciclo (anti-cheat reseta),
   re-otimiza threads novas (eventos ETW ou polling a cada 20 ciclos), roda o
   tick do `Cpulim`, faz `RefreshBackground` (limita novos, restaura os que
   saíram) e reaplica MMCSS.
5. **Jogo fechou** → `RestoreGame` completo + volta a procurar.

## Módulos

| Módulo | Responsabilidade |
|---|---|
| `util` | Log, privilégio de debug, timer resolution, NT APIs (thread/process), nomes de thread (`Th_ThreadNameInformation`), amostragem de CPU% |
| `config` | Parser TOML mínimo (UTF-8/UTF-16) + defaults embutidos |
| `detect` | `kKnownGames`, heurística de janela/engine, whitelists (sistema/anti-cheat) |
| `procopt` | Prioridade, afinidade, topologia CCD/SMT, suspend/processo, working set, EcoQoS, I/O, GPU |
| `threadopt` | Enumeração de threads, `GetThreadName`, regras por nome, throttle de energia, `disable_priority_boost`, restore de threads de fundo |
| `powersys` | Power plan, C-States (`PowerWriteACValueIndex`), MMCSS, threads DWM |
| `cpulim` | Limite de CPU por thread (quota %, suspensão temporária); detecção de main thread por tempo de criação; limpeza de threads mortas |
| `etw` | Sessão `KTGameBoosterETW` (kernel logger) + fallback polling; à prova de falhas |
| `booster` | Núcleo em thread: detecção, aplicação, monitoramento, restore |
| `settings` | Persistência de switches da GUI em `HKCU\Software\KTGameBooster` |
| `ktcore` | API exportada `KT_*` (ciclo, estado, governor, serviços, perfis) |
| `kt_gui` | Janela GDI (dark), DPI-aware, cards, tabela, switches, jogos, log rolável, tray |

## Como o GameInput é tratado

Com `suspend_gameinput_threads = true`, durante a partida o core congela
`gameinputsvc.exe` e `gameinputredistservice.exe` via `NtSuspendProcess` (com
exceção de PID próprio/jogo). Na saída/restauração, os processos são retomados.
Se o processo morrer com kill forçado, os serviços ficam suspensos até o
próximo logon — por isso a GUI mostra balão e o restore é o caminho normal.

## Estado compartilhado (thread-safe)

- `BoosterState`: `std::atomic` (gamePid, gameActive, bgCount, etwEvents,
  memFreed, totalRuns, suspendedThreads, startedTick, paused, running) +
  `wchar_t gameName[]`.
- `Config`: lida sob `cfgMutex`; a UI marca `m_cfgDirty` para o core recarregar
  no próximo ciclo.
- Log: `Log()` → sink da GUI (`CaptureLog`) sob mutex.

## ETW (por que o fallback existe)

O `EtwMonitor` cria a sessão `KTGameBoosterETW` com o GUID do kernel logger para
capturar `Thread_Start`. Em sistemas sem suporte (ou com sessão órfã), o
`StartTraceW` falha e o core cai em **polling a cada 20 ciclos**. O
`TakeNewThreads()`/callback só acessam a critical section quando `m_active`,
evitando o crash `EnterCriticalSection` em CS deletada.
# Snapshot e recuperação de sessão

O motor mantém um snapshot central por sessão em `%LOCALAPPDATA%\KTGameBooster\session.journal`. Em instalação portátil ou ambiente que bloqueie essa pasta, usa `.kt-state\session.journal` ao lado da aplicação. Antes de alterar um processo, thread, serviço ou plano de energia, ele grava o estado observado no journal. O arquivo é substituído de forma atômica e só é removido quando a restauração completa termina.

Na inicialização do motor, uma sessão pendente é restaurada antes que qualquer otimização seja aplicada. O journal valida a hora de criação do processo para não restaurar um PID reutilizado. Se a recuperação falhar, o motor não inicia e registra o motivo; a UI continua disponível. Encerrar o jogo, parar o motor ou fechar a UI usa o mesmo caminho de restauração.

O snapshot cobre prioridade, afinidade, CPU Sets quando a API existe, EcoQoS, prioridade de I/O e memória, prioridade das threads capturadas, serviços alterados pelo modo de jogo e o plano de energia ativo. APIs indisponíveis são registradas como indisponíveis, sem fabricar sucesso.
