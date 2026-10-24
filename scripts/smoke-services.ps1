[CmdletBinding()]
param([string]$ReleaseDirectory = (Join-Path $PSScriptRoot '..\release'))

$ErrorActionPreference = 'Stop'
$releaseDir = [IO.Path]::GetFullPath($ReleaseDirectory)
$tempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
$probeDir = Join-Path $tempRoot "ktgamebooster-service-smoke-$PID"
$probeName = "kt-service-smoke-$PID.exe"
$probePath = Join-Path $probeDir $probeName
$probe = $null
$initialized = $false
$started = $false
$sysMainWasRunning = $false

$nativeSource = @'
using System;
using System.Runtime.InteropServices;
[StructLayout(LayoutKind.Sequential, CharSet=CharSet.Unicode)]
public struct KtServiceSmokeRow {
    [MarshalAs(UnmanagedType.ByValTStr, SizeConst=64)] public string name;
    [MarshalAs(UnmanagedType.ByValTStr, SizeConst=96)] public string displayName;
    public UInt32 state, pid;
    public int canStop, canPause, gamingTarget, protectedService;
}
public static class KtServiceSmokeNative {
    [DllImport("ktengine.dll", CallingConvention=CallingConvention.Cdecl)] public static extern int KT_Initialize();
    [DllImport("ktengine.dll", CallingConvention=CallingConvention.Cdecl)] public static extern void KT_Shutdown();
    [DllImport("ktengine.dll", CallingConvention=CallingConvention.Cdecl)] public static extern int KT_Start();
    [DllImport("ktengine.dll", CallingConvention=CallingConvention.Cdecl)] public static extern void KT_Stop();
    [DllImport("ktengine.dll", CallingConvention=CallingConvention.Cdecl, CharSet=CharSet.Unicode)] public static extern void KT_SetConfigPath(string path);
    [DllImport("ktengine.dll", CallingConvention=CallingConvention.Cdecl, CharSet=CharSet.Unicode)] public static extern void KT_SetTestTarget(string exe);
    [DllImport("ktengine.dll", CallingConvention=CallingConvention.Cdecl, CharSet=CharSet.Ansi)] public static extern int KT_SetFeature(string name, int value);
    [DllImport("ktengine.dll", CallingConvention=CallingConvention.Cdecl)] public static extern int KT_GetServices([Out] KtServiceSmokeRow[] rows, int cap);
    [DllImport("ktengine.dll", CallingConvention=CallingConvention.Cdecl, CharSet=CharSet.Unicode)] public static extern int KT_ServiceControl(string name, int action);
}
'@

function Get-ServiceRow([string]$Name) {
    $rows = New-Object KtServiceSmokeRow[] 512
    $count = [KtServiceSmokeNative]::KT_GetServices($rows, 512)
    if ($count -le 0) { return $null }
    return $rows[0..($count - 1)] | Where-Object name -EQ $Name | Select-Object -First 1
}

Push-Location $releaseDir
try {
    Add-Type -TypeDefinition $nativeSource
    New-Item -ItemType Directory -Path $probeDir -Force | Out-Null
    Copy-Item -LiteralPath "$env:SystemRoot\System32\ping.exe" -Destination $probePath
    $probe = Start-Process $probePath -ArgumentList '-t', '127.0.0.1' -WindowStyle Hidden -PassThru

    [KtServiceSmokeNative]::KT_SetConfigPath((Join-Path $releaseDir 'gamemode.toml'))
    [KtServiceSmokeNative]::KT_Initialize() | Out-Null
    $initialized = $true

    $before = Get-ServiceRow 'SysMain'
    if (-not $before -or $before.state -ne 4 -or $before.canStop -ne 1) {
        throw 'SysMain precisa estar rodando e aceitar STOP para este teste controlado.'
    }
    $sysMainWasRunning = $true

    $features = @(
        'dwm_mmcss','dwm_thread_priority','gpu_priority','background_limiter',
        'thread_power_background_throttle','thread_power_game_unthrottle',
        'io_priority_background','io_priority_game_high','eco_qos_background',
        'suspend_gameinput_threads','disable_c_states','disable_priority_boost',
        'background_cpu_pin','boost_input_threads',
        'high_performance_plan','turbo_boost','core_parking_disable','mmcss_game',
        'page_priority_game','memory_priority_background','memory_priority_game_high',
        'trim_background_ws','tournament_mode',
        'memory_compression_disable','gpu_hardware_scheduling',
        'auto_profile_switching','gaming_power_plans','game_tree_boost','pause_services_gaming'
    )
    foreach ($feature in $features) {
        [KtServiceSmokeNative]::KT_SetFeature($feature, 0) | Out-Null
    }
    [KtServiceSmokeNative]::KT_SetFeature('pause_services_gaming', 1) | Out-Null
    [KtServiceSmokeNative]::KT_SetTestTarget($probeName)
    [KtServiceSmokeNative]::KT_Start() | Out-Null
    $started = $true

    $deadline = [DateTime]::UtcNow.AddSeconds(10)
    do {
        Start-Sleep -Milliseconds 250
        $during = Get-ServiceRow 'SysMain'
    } while ($during.state -notin 1, 3 -and [DateTime]::UtcNow -lt $deadline)
    if ($during.state -notin 1, 3) { throw "SysMain não parou; estado=$($during.state)." }

    [KtServiceSmokeNative]::KT_Stop()
    $started = $false
    $deadline = [DateTime]::UtcNow.AddSeconds(10)
    do {
        Start-Sleep -Milliseconds 250
        $after = Get-ServiceRow 'SysMain'
    } while ($after.state -ne 4 -and [DateTime]::UtcNow -lt $deadline)
    if ($after.state -ne 4) { throw "SysMain não foi restaurado; estado=$($after.state)." }

    [pscustomobject]@{
        Result = 'PASS'
        ProbePid = $probe.Id
        Service = 'SysMain'
        Before = $before.state
        During = $during.state
        After = $after.state
        Restored = $true
    }
}
finally {
    if ($started) { try { [KtServiceSmokeNative]::KT_Stop() } catch { } }
    if ($sysMainWasRunning) {
        try {
            $current = Get-ServiceRow 'SysMain'
            if ($current -and $current.state -ne 4) {
                [KtServiceSmokeNative]::KT_ServiceControl('SysMain', 1) | Out-Null
            }
        } catch { }
    }
    if ($initialized) { [KtServiceSmokeNative]::KT_Shutdown() }
    if ($probe -and -not $probe.HasExited) { Stop-Process -Id $probe.Id -Force }
    if ($probeDir.StartsWith($tempRoot, [StringComparison]::OrdinalIgnoreCase) -and
        (Test-Path -LiteralPath $probeDir)) {
        Remove-Item -LiteralPath $probeDir -Recurse -Force
    }
    Pop-Location
}
