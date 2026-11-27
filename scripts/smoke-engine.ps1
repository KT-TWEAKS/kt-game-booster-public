[CmdletBinding()]
param(
    [string]$ReleaseDirectory,
    [switch]$RequireEtw
)

$ErrorActionPreference = 'Stop'
if (-not $ReleaseDirectory) {
    $scriptRoot = if ($PSScriptRoot) { $PSScriptRoot } else {
        Split-Path -Parent $script:MyInvocation.MyCommand.Path
    }
    $ReleaseDirectory = Join-Path $scriptRoot '..\release'
}
$releaseDir = [System.IO.Path]::GetFullPath($ReleaseDirectory)
$enginePath = Join-Path $releaseDir 'ktengine.dll'
if (-not (Test-Path -LiteralPath $enginePath)) {
    throw "Engine não encontrada: $enginePath"
}

$nativeSource = @'
using System;
using System.Runtime.InteropServices;

// Mirrors typedef struct KtState in src/ktengine_api.h EXACTLY (byte-for-byte,
// sequential layout). KT_GetState memsets-and-fills sizeof(KtState) on the
// native side, so a shorter managed buffer would overflow the marshaler.
[StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
public struct KtSmokeState {
    public int running;
    public int paused;
    public int gameActive;
    public UInt32 gamePid;
    [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 64)] public string gameName;
    public UInt32 bgCount;
    public UInt32 suspendedThreads;
    public UInt64 memFreed;
    public UInt64 etwEvents;
    public UInt32 totalRuns;
    public UInt32 totalGameSessions;
    public UInt64 totalGameSeconds;
    public UInt32 uptimeSec;
    public UInt32 gameUptimeSec;
    public double sysCpu;
    public UInt64 sysRamUsedMB;
    public UInt64 sysRamTotalMB;
    public int procCount;
    public int tournament;
    public int detectionConfidence;
    public int extremeActive;
    public int featureMask;
    public int autoCleanCount;
    public int servicePauseCount;
    public int boostedChildCount;
    public int peakCpuPctX100;
    public int peakRamPctX100;
    public int elevated;
    public int etwActive;
    public int fpsX100;
    public int frameP95us;
    public int frameP99us;
    public int frameSamples;
    public int featureMask2;
    public int gpuPctX100;
    [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 64)] public string gpuName;
    public int anticheatSafe;
    public int anticheatKind;
    // v2.5: automatic per-game preset (must stay the LAST field, mirroring KtState).
    [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)] public string gameProfile;
}

public static class KtSmokeNative {
    [DllImport("kernel32.dll", EntryPoint = "LoadLibraryExW", SetLastError = true, CharSet = CharSet.Unicode)]
    public static extern IntPtr LoadLibraryEx(string fileName, IntPtr hFile, uint flags);

    // Pin the engine by ABSOLUTE path before any KT_* call: resolving
    // "ktengine.dll" by name lets the loader search order (process working
    // directory, PATH) silently bind whatever copy it hits first, including
    // stray older builds - assertions would then run against the wrong engine.
    public static void PinEngine(string path) {
        const uint LOAD_WITH_ALTERED_SEARCH_PATH = 0x00000008;
        if (LoadLibraryEx(path, IntPtr.Zero, LOAD_WITH_ALTERED_SEARCH_PATH) == IntPtr.Zero)
            throw new System.IO.IOException("ktengine.dll não carregou por caminho absoluto (Win32 " + Marshal.GetLastWin32Error() + ").");
    }

    [DllImport("ktengine.dll", CallingConvention = CallingConvention.Cdecl)]
    public static extern int KT_Initialize();
    [DllImport("ktengine.dll", CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Unicode)]
    public static extern void KT_SetConfigPath(string path);
    [DllImport("ktengine.dll", CallingConvention = CallingConvention.Cdecl)]
    public static extern void KT_Shutdown();
    [DllImport("ktengine.dll", CallingConvention = CallingConvention.Cdecl)]
    public static extern int KT_Start();
    [DllImport("ktengine.dll", CallingConvention = CallingConvention.Cdecl)]
    public static extern void KT_Stop();
    [DllImport("ktengine.dll", CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Unicode)]
    public static extern void KT_SetTestTarget(string exe);
    [DllImport("ktengine.dll", CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
    public static extern int KT_SetFeature(string name, int value);
    [DllImport("ktengine.dll", CallingConvention = CallingConvention.Cdecl)]
    public static extern void KT_GetState(out KtSmokeState state);
    [DllImport("ktengine.dll", CallingConvention = CallingConvention.Cdecl)]
    public static extern int KT_GovernProcess(UInt32 pid, int action, UInt64 value);
}
'@

Push-Location $releaseDir
$probe = $null
$eventProbe = $null
$initialized = $false
$tempRoot = [System.IO.Path]::GetFullPath([System.IO.Path]::GetTempPath())
$probeDir = Join-Path $tempRoot "ktgamebooster-smoke-$PID"
$probeExeName = "kt-smoke-$PID.exe"
$probeExe = Join-Path $probeDir $probeExeName
try {
    Add-Type -TypeDefinition $nativeSource
    [KtSmokeNative]::PinEngine($enginePath)
    New-Item -ItemType Directory -Path $probeDir -Force | Out-Null
    Copy-Item -LiteralPath "$env:SystemRoot\System32\ping.exe" -Destination $probeExe
    $probe = Start-Process $probeExe `
        -ArgumentList '-t', '127.0.0.1' -WindowStyle Hidden -PassThru
    $originalPriority = $probe.PriorityClass

    [KtSmokeNative]::KT_SetConfigPath((Join-Path $releaseDir 'gamemode.toml'))
    $initResult = [KtSmokeNative]::KT_Initialize()
    if ($initResult -notin 0, 1) { throw "KT_Initialize retornou $initResult." }
    $initialized = $true

    $features = @(
        'dwm_mmcss', 'dwm_thread_priority', 'gpu_priority',
        'background_limiter', 'thread_power_background_throttle',
        'thread_power_game_unthrottle', 'io_priority_background',
        'io_priority_game_high', 'eco_qos_background',
        'suspend_gameinput_threads', 'disable_c_states',
        'disable_priority_boost', 'background_cpu_pin',
        'boost_input_threads', 'high_performance_plan',
        'turbo_boost', 'core_parking_disable', 'mmcss_game',
        'page_priority_game', 'memory_priority_background',
        'memory_priority_game_high', 'trim_background_ws',
        'tournament_mode',
        'memory_compression_disable', 'gpu_hardware_scheduling',
        'auto_profile_switching', 'gaming_power_plans', 'game_tree_boost',
        'pause_services_gaming'
    )
    $autoGovernorFeatures = @(
        'io_priority_game_high', 'mmcss_game',
        'page_priority_game', 'memory_priority_game_high'
    )
    foreach ($feature in $features) {
        if ($RequireEtw -and $feature -eq 'thread_power_game_unthrottle') { continue }
        if ($feature -in $autoGovernorFeatures) { continue }
        if ([KtSmokeNative]::KT_SetFeature($feature, 0) -ne 1) {
            throw "Feature desconhecida no smoke test: $feature"
        }
    }

    [KtSmokeNative]::KT_SetTestTarget($probeExeName)
    if ([KtSmokeNative]::KT_Start() -ne 1) { throw 'KT_Start falhou.' }

    $deadline = [DateTime]::UtcNow.AddSeconds(8)
    $state = [KtSmokeState]::new()
    do {
        Start-Sleep -Milliseconds 200
        [KtSmokeNative]::KT_GetState([ref]$state)
    } while (($state.gameActive -ne 1 -or $state.gamePid -ne $probe.Id) -and [DateTime]::UtcNow -lt $deadline)

    if ($state.gameActive -ne 1 -or $state.gamePid -ne $probe.Id) {
        throw "Processo controlado não foi detectado. PID esperado: $($probe.Id); recebido: $($state.gamePid)."
    }
    if ($state.bgCount -ne 0) { throw "Limitador de fundo tocou $($state.bgCount) processo(s)." }
    if ($state.gameProfile -notin @('', 'streaming')) {
        throw "gameProfile publicou '$($state.gameProfile)' para um processo fora das regras."
    }
    if ($RequireEtw -and $state.etwActive -ne 1) {
        throw 'Fastpath ETW solicitado, mas a sessão não ficou ativa.'
    }
    if ($RequireEtw) {
        $eventProbe = Start-Process $probeExe `
            -ArgumentList '-n', '2', '127.0.0.1' -WindowStyle Hidden -PassThru
        $etwDeadline = [DateTime]::UtcNow.AddSeconds(4)
        do {
            Start-Sleep -Milliseconds 200
            [KtSmokeNative]::KT_GetState([ref]$state)
        } while ($state.etwEvents -eq 0 -and [DateTime]::UtcNow -lt $etwDeadline)
        if ($state.etwEvents -eq 0) { throw 'Sessão ETW ativa, mas nenhum evento chegou ao consumidor.' }
    }

    $probe.Refresh()
    $optimizedPriority = $probe.PriorityClass
    if ($optimizedPriority -ne [System.Diagnostics.ProcessPriorityClass]::High) {
        throw "Prioridade durante a otimização foi $optimizedPriority, esperada: High."
    }
    if ($state.anticheatSafe -ne 0) {
        throw 'Modo seguro anticheat ativou para um jogo fora da lista AC.'
    }

    $memoryResults = foreach ($level in 0..2) {
        [KtSmokeNative]::KT_GovernProcess([uint32]$probe.Id, 5, [uint64]$level)
    }
    if ($memoryResults -contains 0) { throw 'Governor de memória falhou em um dos níveis.' }

    [KtSmokeNative]::KT_Stop()
    $probe.Refresh()
    $restoredPriority = $probe.PriorityClass
    if ($restoredPriority -ne $originalPriority) {
        throw "Restauração falhou: antes=$originalPriority, depois=$restoredPriority."
    }

    $acProbeName = 'valorant-win64-shipping.exe'
    $acProbeExe = Join-Path $probeDir $acProbeName
    Copy-Item -LiteralPath "$env:SystemRoot\System32\ping.exe" -Destination $acProbeExe
    $acProbe = Start-Process $acProbeExe -ArgumentList '-t', '127.0.0.1' -WindowStyle Hidden -PassThru
    $acSafeResult = -1
    try {
        [KtSmokeNative]::KT_SetTestTarget($acProbeName)
        if ([KtSmokeNative]::KT_Start() -ne 1) { throw 'KT_Start (passada anticheat) falhou.' }
        $acDeadline = [DateTime]::UtcNow.AddSeconds(8)
        do {
            Start-Sleep -Milliseconds 200
            [KtSmokeNative]::KT_GetState([ref]$state)
        } while (($state.gameActive -ne 1 -or $state.gamePid -ne $acProbe.Id) -and [DateTime]::UtcNow -lt $acDeadline)
        if ($state.gameActive -ne 1 -or $state.gamePid -ne $acProbe.Id) {
            throw 'Passada anticheat: processo não foi detectado.'
        }
        if ($state.anticheatSafe -ne 1) {
            throw 'Modo seguro anticheat não ativou para um jogo da lista AC.'
        }
        if ($state.bgCount -ne 0) { throw 'Passada AC limitou processos de fundo.' }
        if ($state.gameProfile -ne 'tournament') {
            throw "Passada anticheat: esperado preset automático 'tournament', recebido '$($state.gameProfile)'."
        }
        $acProbe.Refresh()
        if ($acProbe.PriorityClass -ne [System.Diagnostics.ProcessPriorityClass]::High) {
            throw "Passada anticheat: prioridade durante foi $($acProbe.PriorityClass), esperada High."
        }
        [KtSmokeNative]::KT_Stop()
        $acProbe.Refresh()
        if ($acProbe.PriorityClass -ne $originalPriority) {
            throw "Passada anticheat: restauração falhou (antes=$originalPriority, depois=$($acProbe.PriorityClass))."
        }
        $acSafeResult = 1
    }
    finally {
        if ($acProbe -and -not $acProbe.HasExited) { Stop-Process -Id $acProbe.Id -Force }
        Remove-Item -LiteralPath $acProbeExe -Force -ErrorAction SilentlyContinue
    }

    [pscustomobject]@{
        Result = 'PASS'
        ProbePid = $probe.Id
        Before = $originalPriority
        During = $optimizedPriority
        After = $restoredPriority
        MemoryGovernor = ($memoryResults -join ',')
        AutoGovernor = 'I/O alta + memória alta + MMCSS + page priority'
        EtwActive = $state.etwActive
        EtwEvents = $state.etwEvents
        AcSafeMode = $acSafeResult
        GameProfile = $state.gameProfile
    }
}
finally {
    if ($initialized) { [KtSmokeNative]::KT_Shutdown() }
    if ($eventProbe -and -not $eventProbe.HasExited) { Stop-Process -Id $eventProbe.Id -Force }
    if ($probe -and -not $probe.HasExited) { Stop-Process -Id $probe.Id -Force }
    if ($probeDir.StartsWith($tempRoot, [System.StringComparison]::OrdinalIgnoreCase) -and
        (Test-Path -LiteralPath $probeDir)) {
        Remove-Item -LiteralPath $probeDir -Recurse -Force
    }
    Pop-Location
}
