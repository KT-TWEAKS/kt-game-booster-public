using System;
using System.Runtime.InteropServices;

namespace KTGameBooster
{
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    public struct KtState
    {
        public int running;
        public int paused;
        public int gameActive;
        public uint gamePid;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 64)] public string gameName;
        public uint bgCount;
        public uint suspendedThreads;
        public ulong memFreed;
        public ulong etwEvents;
        public uint totalRuns;
        public uint totalGameSessions;
        public ulong totalGameSeconds;
        public uint uptimeSec;
        public uint gameUptimeSec;
        public double sysCpu;
        public ulong sysRamUsedMB;
        public ulong sysRamTotalMB;
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
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)] public string gameProfile;
    }

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    public struct KtProc
    {
        public uint pid;
        public int isGame;
        public int throttled;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)] public string klass;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 64)] public string name;
        public ulong affinity;
    }

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    public struct KtSysProc
    {
        public uint pid;
        public int isGame;
        public int throttled;
        public int gameChild;
        public double cpuPct;
        public uint ramMB;
        public ulong affinity;
        public int priority;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 64)] public string name;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)] public string klass;
        public ulong creationTime;
        public double gpuPct;
    }

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    public struct KtService
    {
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 64)] public string name;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 96)] public string displayName;
        public uint state;
        public uint pid;
        public int canStop;
        public int canPause;
        public int gamingTarget;
        public int protectedService;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct KtMetricsSample
    {
        public double cpuPct;
        public ulong ramUsedMB;
        public ulong ramTotalMB;
        public uint fps;
        public uint pingMs;
        public ulong tickMs;
        public double gpuPct;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct KtBenchResult
    {
        public uint latencyUsAvg;
        public uint latencyUsP50;
        public uint latencyUsP95;
        public uint latencyUsMax;
        public uint samples;
        public uint durationMs;
        public ulong tickMs;
        public int valid;
    }

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    public struct KtLogLine
    {
        public int level;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 512)] public string text;
    }

    public static class KtFeature
    {
        public const int DwmMmcss           = 0x00000001;
        public const int GpuPriority        = 0x00000002;
        public const int BackgroundLimiter  = 0x00000004;
        public const int UnthrottleGame     = 0x00000008;
        public const int IoBackground       = 0x00000010;
        public const int EcoQosBackground   = 0x00000020;
        public const int SuspendGameInput   = 0x00000040;
        public const int CStates            = 0x00000080;
        public const int InputBoost         = 0x00000100;
        public const int HighPerfPlan       = 0x00000200;
        public const int TurboBoost         = 0x00000400;
        public const int CoreParking        = 0x00000800;
        public const int PinPerf            = 0x00001000;
        public const int TrimBgWs           = 0x00002000;
        public const int Network            = 0x00004000;
        public const int MemCompression     = 0x00008000;
        public const int GpuHwSched         = 0x00010000;
        public const int AutoProfile        = 0x00020000;
        public const int Timer500us         = 0x00040000;
        public const int CpuSets            = 0x00080000;
        public const int GamingPlans        = 0x00100000;
        public const int MemoryLock         = 0x00200000;
        public const int ExtremeShell       = 0x00400000;
        public const int ExtremeDwm         = 0x00800000;
        public const int AutoCleanRam       = 0x01000000;
        public const int GameTreeBoost      = 0x02000000;
        public const int ServicePause       = 0x04000000;
        public const int DwmThreadPriority  = 0x08000000;
        public const int BackgroundPowerThrottle = 0x10000000;
        public const int IoGameHigh         = 0x20000000;
        public const int MmcssGame          = 0x40000000;
        public const int PagePriorityGame   = unchecked((int)0x80000000);
    }

    public static class KtFeature2
    {
        public const int GameDvrDisable = 0x00000001;
        public const int FsoDisable     = 0x00000002;
        public const int FpsMonitor     = 0x00000004;
        public const int AntiCheatSafeMode = 0x00000008;
    }

    public static class KtEngine
    {
        private const string Dll = "ktengine";

        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern int KT_Initialize();
        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern void KT_Shutdown();
        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern int KT_Start();
        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern void KT_Stop();
        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern void KT_SetSafeMode(int enabled);
        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern int KT_RecoverPendingSession();
        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern void KT_SetPaused(int paused);
        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern void KT_SetTournament(int on);
        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Unicode)]
        public static extern void KT_SetTestTarget([MarshalAs(UnmanagedType.LPWStr)] string exe);
        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi, BestFitMapping = false, ThrowOnUnmappableChar = true)]
        public static extern int KT_SetFeature([MarshalAs(UnmanagedType.LPStr)] string name, int value);
        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern int KT_IsElevated();
        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern void KT_RestoreShell();

        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern void KT_GetState(out KtState state);
        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern int KT_GetProcesses([Out] KtProc[] outBuf, int maxCount);
        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern int KT_GetSysProcesses([Out] KtSysProc[] outBuf, int maxCount);
        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern int KT_GovernProcess(uint pid, int action, ulong value);
        [DllImport("ktengine.dll", CallingConvention = CallingConvention.Cdecl)]
        public static extern int KT_GovernProcessEx(uint pid, ulong expectedCreationTime, int action, ulong value);
        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern int KT_GetServices([Out] KtService[] outBuf, int maxCount);
        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Unicode)]
        public static extern int KT_ServiceControl([MarshalAs(UnmanagedType.LPWStr)] string name, int action);
        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Unicode)]
        public static extern int KT_AddExclusion([MarshalAs(UnmanagedType.LPWStr)] string exe);
        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Unicode)]
        public static extern int KT_RemoveExclusion([MarshalAs(UnmanagedType.LPWStr)] string exe);
        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Unicode)]
        public static extern int KT_AddGame([MarshalAs(UnmanagedType.LPWStr)] string exe);
        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Unicode)]
        public static extern void KT_LoadProfile([MarshalAs(UnmanagedType.LPWStr)] string name);
        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern int KT_ListProfiles(IntPtr outBuf, int maxCount, int nameLen);
        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern ulong KT_CleanRam();
        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern int KT_GetLogV2(int minLevel, int maxLines, [Out] KtLogLine[] outBuf);
        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern int KT_GetMetricsHistory([Out] KtMetricsSample[] outBuf, int maxCount);
        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
        public static extern int KT_RunBench(uint durationMs, out KtBenchResult result);

        public static KtState GetState() { KtState s; KT_GetState(out s); return s; }

        public static KtProc[] GetProcesses(int cap = 1024)
        {
            var buf = new KtProc[cap];
            int n = KT_GetProcesses(buf, cap);
            if (n < 0) n = 0;
            Array.Resize(ref buf, n);
            return buf;
        }

        public static KtSysProc[] GetSysProcesses(int cap = 2048)
        {
            var buf = new KtSysProc[cap];
            int n = KT_GetSysProcesses(buf, cap);
            if (n < 0) n = 0;
            Array.Resize(ref buf, n);
            return buf;
        }

        public static int GovernProcess(uint pid, int action, ulong value)
        {
            return KT_GovernProcess(pid, action, value);
        }

        public static int GovernProcess(uint pid, ulong expectedCreationTime, int action, ulong value)
        {
            return KT_GovernProcessEx(pid, expectedCreationTime, action, value);
        }

        public static KtService[] GetServices(int cap = 512)
        {
            var buf = new KtService[cap];
            int n = KT_GetServices(buf, cap);
            if (n < 0) n = 0;
            Array.Resize(ref buf, n);
            return buf;
        }

        public static ulong CleanRam() { return KT_CleanRam(); }
        public static void Start() { KT_Start(); }
        public static void Stop() { KT_Stop(); }
        public static void SetSafeMode(bool enabled) { KT_SetSafeMode(enabled ? 1 : 0); }
        public static bool RecoverPendingSession() { return KT_RecoverPendingSession() == 1; }
        public static void Pause(bool on) { KT_SetPaused(on ? 1 : 0); }
        public static void Tournament(bool on) { KT_SetTournament(on ? 1 : 0); }
        public static void TestTarget(string exe) { KT_SetTestTarget(exe ?? ""); }
        public static int SetFeature(string name, bool v) { return KT_SetFeature(name ?? "", v ? 1 : 0); }
        public static int ServiceControl(string name, int action) { return KT_ServiceControl(name ?? "", action); }
        public static int AddExclusion(string exe) { return KT_AddExclusion(exe ?? ""); }
        public static int RemoveExclusion(string exe) { return KT_RemoveExclusion(exe ?? ""); }
        public static int AddGame(string exe) { return KT_AddGame(exe ?? ""); }
        public static void LoadProfile(string name) { KT_LoadProfile(name ?? ""); }

        public static string[] ListProfiles()
        {
            const int kNameLen = 64;
            const int kMax = 16;
            IntPtr buf = Marshal.AllocHGlobal(kMax * kNameLen * sizeof(char));
            try
            {
                int n = KT_ListProfiles(buf, kMax, kNameLen);
                if (n <= 0) return new string[0];
                var arr = new string[n];
                for (int i = 0; i < n; i++)
                    arr[i] = Marshal.PtrToStringUni(buf + i * kNameLen * sizeof(char), kNameLen).TrimEnd('\0');
                return arr;
            }
            finally { Marshal.FreeHGlobal(buf); }
        }

        public static KtLogLine[] GetLogV2(int minLevel = 0, int maxLines = 400)
        {
            var buf = new KtLogLine[maxLines];
            int n = KT_GetLogV2(minLevel, maxLines, buf);
            if (n <= 0) return new KtLogLine[0];
            Array.Resize(ref buf, n);
            return buf;
        }

        public static KtMetricsSample[] GetMetrics(int max = 240)
        {
            var buf = new KtMetricsSample[max];
            int n = KT_GetMetricsHistory(buf, max);
            if (n <= 0) return new KtMetricsSample[0];
            Array.Resize(ref buf, n);
            return buf;
        }

        public static KtBenchResult RunBench(uint durationMs = 1500)
        {
            KtBenchResult r; KT_RunBench(durationMs, out r); return r;
        }
    }
}
