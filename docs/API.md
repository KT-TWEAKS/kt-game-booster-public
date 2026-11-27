# API EXPORTADA

`ktengine.dll` exporta uma API C (`__declspec(dllexport)`) usada pela interface
`KTGameBooster.exe`. Integrações locais também podem carregá-la via
`LoadLibrary`/`GetProcAddress`, desde que respeitem a arquitetura x64 e o ciclo
`KT_Initialize` → uso → `KT_Shutdown`.

O executável WPF não exporta funções: ele carrega a engine nativa em processo.
A lista oficial de exports é o `src/ktengine.def` — todos os nomes seguem o
prefixo `KT_`.

## Exemplo (C)

```cpp
typedef int (*SetFeature_t)(const char* name, int value);
typedef void (*GetState_t)(KT_STATE* out);
HMODULE m = LoadLibraryW(L"ktengine.dll");
auto init = (int (*)())GetProcAddress(m, "KT_Initialize");
auto setFeature = (SetFeature_t)GetProcAddress(m, "KT_SetFeature");
if (init() == 0 && setFeature) setFeature("background_limiter", 1);
```

## Exemplo (C# / AHK)

```csharp
// C#: P/Invoke direto (o mesmo caminho usado pela UI)
[DllImport("ktengine.dll", CallingConvention = CallingConvention.Cdecl)]
static extern int KT_Initialize();
[DllImport("ktengine.dll", CallingConvention = CallingConvention.Cdecl)]
static extern void KT_SetPaused(int paused);

KT_Initialize();
KT_SetPaused(1);
```

```autohotkey
; AutoHotkey: hotkey para pausar o booster
#IfWinActive, ahk_exe Game.exe
F9::DllCall("ktengine.dll\KT_SetPaused", "int", 1, "CDecl Int")
F9 Up::DllCall("ktengine.dll\KT_SetPaused", "int", 0, "CDecl Int")
#IfWinActive
```

## Funções

### Ciclo de vida

| Função | Retorno | Descrição |
|---|---|---|
| `KT_Initialize()` | `int` | Cria o core e prepara telemetria (0 = OK, 1 = já ativo). |
| `KT_Shutdown()` | `void` | Para o core, restaura tudo e libera os recursos. |
| `KT_Start()` | `int` | Inicia o motor com a config atual. |
| `KT_Stop()` | `void` | Para a sessão e restaura o estado do sistema. |
| `KT_SetPaused(int)` | `void` | Pausa/retoma as otimizações sem restaurar nada. |
| `KT_SetSafeMode(int)` | `void` | Bloqueia alterações no sistema (modo seguro). |
| `KT_RecoverPendingSession()` | `int` | Restaura um journal de sessão deixado por crash. |
| `KT_RestoreShell()` | `void` | Recuperação de emergência do shell (hotkey de pânico). |

### Estado e telemetria

| Função | Descrição |
|---|---|
| `KT_GetState(KtState*)` | Snapshot completo: jogo ativo, PID, CPU/RAM, contadores, bitmap de features. |
| `KT_GetProcesses(KtProc*, int)` | Lista de processos tocados na sessão. |
| `KT_GetSysProcesses(KtSysProc*, int)` | Lista completa do sistema com CPU%/RAM por processo. |
| `KT_GetLogV2(int minLevel, int maxLines, KtLogLine*)` | Log do motor com severidade por linha. |
| `KT_GetMetricsHistory(KtMetricsSample*, int)` | Histórico de amostras (CPU/RAM/FPS) do ring buffer. |
| `KT_RunBench(unsigned int, KtBenchResult*)` | Bench de latência de scheduling (avg/p50/p95/máx em μs). |
| `KT_IsElevated()` | Token de administrador ativo (badge da UI). |

### Configuração e alvos

| Função | Descrição |
|---|---|
| `KT_SetFeature(const char*, int)` | Liga/desliga um tweak pelo nome (`background_limiter`, `game_dvr_disable`, ...). |
| `KT_SetTournament(int)` | Alterna o perfil torneio em tempo real. |
| `KT_SetTestTarget(const wchar_t*)` | Força um `.exe` como jogo ativo no próximo ciclo (vazio limpa). |
| `KT_SetConfigPath(const wchar_t*)` | Aponta um `gamemode.toml` alternativo. |
| `KT_LoadProfile(const wchar_t* name)` | Carrega um perfil de `profiles/`. |
| `KT_ListProfiles(wchar_t*, int, int)` | Enumera os perfis disponíveis. |

### Governor, serviços e exclusões

| Função | Descrição |
|---|---|
| `KT_GovernProcess(DWORD, int, ULONGLONG)` | Ação manual do Governor (prioridade, memória, suspensão, trim). |
| `KT_GovernProcessEx(DWORD, ULONGLONG, int, ULONGLONG)` | Mesma ação com validação do creation time (anti-reciclagem de PID). |
| `KT_GetServices(KtService*, int)` | Lista de serviços Win32 com estado e PID. |
| `KT_ServiceControl(const wchar_t*, int)` | Pausa/retoma/para/inicia um serviço da lista segura. |
| `KT_AddExclusion(const wchar_t*)` / `KT_RemoveExclusion(...)` | Exclusões persistentes do limitador. |
| `KT_AddGame(const wchar_t*)` | Cadastra um jogo custom na detecção. |
| `KT_CleanRam()` | Trim imediato de working sets; retorna bytes liberados. |

> Notas: structs e assinaturas completas estão em `src/ktcore.h` e
> `ui/KTGameBoosterUI/KtEngine.cs` (marshaling C#↔C validado em paridade).
