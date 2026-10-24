using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Interop;
using System.Windows.Media;
using System.Windows.Threading;
using System.Windows.Forms;
using System.Threading.Tasks;
using System.Collections.Concurrent;
using SysDrawing = System.Drawing;

namespace KTGameBooster
{
    public partial class MainWindow : Window
    {
        private bool _engineReady = false;
        private static readonly ConcurrentDictionary<string, ImageSource> ProcessIconCache = new ConcurrentDictionary<string, ImageSource>();
        private static readonly Queue<string> ProcessIconCacheOrder = new Queue<string>();
        private static readonly HashSet<string> ProcessIconRequests = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        private static readonly object ProcessIconLock = new object();
        private DispatcherTimer _timer;
        private DispatcherTimer _updateTimer;
        private UpdateCheckResult _pendingUpdate;
        private bool _updateCheckRunning;
        private bool _updatePromptShown;
        private bool _paused = false;
        private bool _tournament = false;
        private int _tickCount = 0;
        private bool _syncingToggles = false;
        private int _lastFeatureMask = int.MinValue;
        private int _lastFeatureMask2 = int.MinValue;
        private KtSysProc[] _lastSysProcs = new KtSysProc[0];
        private KtService[] _lastServices = new KtService[0];
        private int _lastSvcPauseCount = 0;
        private int _lastBoostedChildCount = 0;
        private bool _lastExtremeActive = false;
        private uint _lastNotifiedGamePid = 0;
        private bool _isElevated = false;
        private double[] _cpuSeries = new double[0];
        private double[] _ramSeries = new double[0];
        private readonly List<double> _benchSeries = new List<double>();
        private KtLogLine[] _lastLog = new KtLogLine[0];
        private int _logMinLevel = 0;
        private string _logFilter = "";
        private bool _logAutoScroll = true;
        private string _lastEngineError = "";
        private string _activeProfile = "normal";
        private ulong _offlineIdleTicks;
        private ulong _offlineKernelTicks;
        private ulong _offlineUserTicks;
        private bool _offlineCpuReady;
        private readonly Dictionary<string, bool> _stagedTweaks = new Dictionary<string, bool>();
        private readonly System.Collections.Generic.Dictionary<Border, DispatcherTimer> _infoBarHideTimers =
            new System.Collections.Generic.Dictionary<Border, DispatcherTimer>();
        private bool? _lastTournament;

        private static readonly System.Collections.Generic.Dictionary<string, int> TagToFeature =
            new System.Collections.Generic.Dictionary<string, int>
            {
                { "dwm_mmcss",                   KtFeature.DwmMmcss },
                { "gpu_priority",                KtFeature.GpuPriority },
                { "background_limiter",          KtFeature.BackgroundLimiter },
                { "thread_power_game_unthrottle", KtFeature.UnthrottleGame },
                { "io_priority_background",      KtFeature.IoBackground },
                { "eco_qos_background",          KtFeature.EcoQosBackground },
                { "suspend_gameinput_threads",   KtFeature.SuspendGameInput },
                { "disable_c_states",             KtFeature.CStates },
                { "boost_input_threads",         KtFeature.InputBoost },
                { "high_performance_plan",       KtFeature.HighPerfPlan },
                { "turbo_boost",                 KtFeature.TurboBoost },
                { "core_parking_disable",        KtFeature.CoreParking },
                { "trim_background_ws",          KtFeature.TrimBgWs },
                { "memory_compression_disable",  KtFeature.MemCompression },
                { "gpu_hardware_scheduling",     KtFeature.GpuHwSched },
                { "auto_profile_switching",      KtFeature.AutoProfile },
                { "gaming_power_plans",          KtFeature.GamingPlans },
                { "game_tree_boost",             KtFeature.GameTreeBoost },
                { "pause_services_gaming",       KtFeature.ServicePause },
                { "dwm_thread_priority",         KtFeature.DwmThreadPriority },
                { "thread_power_background_throttle", KtFeature.BackgroundPowerThrottle },
                { "io_priority_game_high",       KtFeature.IoGameHigh },
                { "mmcss_game",                  KtFeature.MmcssGame },
                { "page_priority_game",          KtFeature.PagePriorityGame },
            };

        private NotifyIcon _tray;
        private bool _forceClose = false;
        private static readonly SolidColorBrush FallbackAccent  = new SolidColorBrush(Color.FromRgb(0x5B, 0x8C, 0xFF));
        private static readonly SolidColorBrush FallbackAccent2 = new SolidColorBrush(Color.FromRgb(0x8B, 0x5B, 0xFF));
        private static readonly SolidColorBrush FallbackWarn    = new SolidColorBrush(Color.FromRgb(0xFB, 0xBF, 0x24));
        private static readonly SolidColorBrush FallbackDanger  = new SolidColorBrush(Color.FromRgb(0xF8, 0x71, 0x71));
        private static readonly SolidColorBrush FallbackSuccess = new SolidColorBrush(Color.FromRgb(0x22, 0xC5, 0x5E));
        private static readonly SolidColorBrush FallbackAccent3 = new SolidColorBrush(Color.FromRgb(0x22, 0xD3, 0xEE));
        private static SolidColorBrush BrushAccent  => ThemeRes.Res("AccentBrush", FallbackAccent);
        private static SolidColorBrush BrushAccent2 => ThemeRes.Res("Accent2Brush", FallbackAccent2);
        private static SolidColorBrush BrushWarn    => ThemeRes.Res("WarnBrush", FallbackWarn);
        private static SolidColorBrush BrushDanger  => ThemeRes.Res("DangerBrush", FallbackDanger);
        private static SolidColorBrush BrushSuccess => ThemeRes.Res("SuccessBrush", FallbackSuccess);
        private static SolidColorBrush BrushAccent3 => ThemeRes.Res("Accent3Brush", FallbackAccent3);
        private static readonly System.Collections.Generic.Dictionary<string, string> ServiceReasons =
            new System.Collections.Generic.Dictionary<string, string>(StringComparer.OrdinalIgnoreCase)
            {
                { "WSearch", "reduz indexação e I/O em disco" },
                { "SysMain", "reduz pré-carregamento em segundo plano" },
                { "wuauserv", "evita downloads e instalação de updates" },
                { "Spooler", "remove atividade de impressão durante o jogo" },
                { "WpcMonSvc", "reduz monitoramento de controle parental" },
                { "MapsBroker", "evita atualização de mapas offline" },
                { "lfsvc", "suspende geolocalização não essencial" },
                { "Fax", "serviço opcional sem impacto no jogo" },
                { "DiagTrack", "reduz telemetria em segundo plano" },
                { "WerSvc", "adia coleta de relatórios de erro" },
                { "DoSvc", "evita distribuição de downloads do Windows" },
                { "RemoteRegistry", "serviço remoto dispensável durante o jogo" },
                { "TrkWks", "reduz rastreamento de links distribuídos" },
                { "RetailDemo", "modo de demonstração não necessário" },
                { "WMPNetworkSvc", "suspende compartilhamento de mídia" },
                { "PhoneSvc", "suspende integração com telefone" },
                { "WalletService", "serviço de carteira não usado pelo jogo" },
                { "shpamsvc", "suspende hotspot móvel em segundo plano" },
                { "DusmSvc", "reduz monitoramento de uso de dados" },
                { "PrintNotify", "remove notificações de impressão" },
                { "BcastDVRUserService", "pausa captura e Game DVR opcional" },
                { "CaptureService", "pausa captura de tela em segundo plano" },
                { "OneSyncSvc", "adia sincronização de dados pessoais" },
                { "PimIndexMaintenanceSvc", "adia indexação de contatos e calendário" },
                { "UnistoreSvc", "adia armazenamento de dados de aplicativos" },
                { "UserDataSvc", "adia sincronização de dados do usuário" },
                { "CDPUserSvc", "pausa experiências entre dispositivos" },
            };

        public bool IsElevated { get { return _isElevated; } }
        private bool _xamlLoaded = false;
        private bool IsXamlLoaded { get { return _xamlLoaded; } }

        public MainWindow()
        {
            InitializeComponent();
            ThemeManager.Apply(ThemeManager.Load());
            if (btnThemeToggle != null)
                btnThemeToggle.Content = "Tema: " + ThemeManager.NextLabel(ThemeManager.Current);
            if (Environment.GetCommandLineArgs().Any(a =>
                    string.Equals(a, "--visual-test", StringComparison.OrdinalIgnoreCase)))
                Topmost = true;
            SourceInitialized += (s, e) =>
            {
                WindowBackdrop.Apply(this, WindowBackdrop.Style.Acrylic);
                WindowBackdrop.StyleChrome(this);
                var source = PresentationSource.FromVisual(this) as HwndSource;
                if (source != null) source.AddHook(WindowProc);
            };
            Loaded += MainWindow_Loaded;
            Closing += MainWindow_Closing;
            Closed += (s, e) => { try { KtEngine.KT_Shutdown(); } catch { } };
        }

        private void MainWindow_Loaded(object sender, RoutedEventArgs e)
        {
            chkSafeMode.IsChecked = App.SafeMode;
            _xamlLoaded = true;
            txtBackdropStatus.Text = "material: " + WindowBackdrop.ActiveMaterial.ToLowerInvariant() +
                                     (WindowBackdrop.IsMaterialActive ? " ativo" : " (fallback)");
            txtMaterialChip.Text = WindowBackdrop.ActiveMaterial.ToUpperInvariant();
            backdropDot.Fill = WindowBackdrop.IsMaterialActive ? BrushAccent3 : BrushWarn;

            if (AllowsTransparency)
            {
                BeginAnimation(UIElement.OpacityProperty, null);
                Opacity = 1;
            }
            else
            {
                Opacity = 0;
                var fadeIn = new System.Windows.Media.Animation.DoubleAnimation
                {
                    From = 0,
                    To = 1,
                    Duration = TimeSpan.FromMilliseconds(220),
                    EasingFunction = new System.Windows.Media.Animation.QuadraticEase { EasingMode = System.Windows.Media.Animation.EasingMode.EaseOut }
                };
                BeginAnimation(UIElement.OpacityProperty, fadeIn);
            }

            EnsureConnected();
            ApplySafeModeUi();
            if (App.SafeMode)
            {
                txtStatus.Text = "Modo seguro: restauração pendente executada; otimizações desativadas.";
                ShowBalloon("Modo seguro", "O motor não aplicará tweaks nesta sessão.");
            }
            LoadProfilesIntoCombo();
            if (_engineReady) SyncTogglesFromState(KtEngine.GetState(), true);
            LoadGameList();
            LoadExclusions();

            _timer = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(800) };
            _timer.Tick += Tick;
            _timer.Start();

            InitTray();
            LoadAutoRunState();
            LoadHideOnGamePref();
            RegisterGlobalHotkeys();
            UpdateNavHeader("Dashboard", "visão geral do motor e do jogo ativo");
            OpenStartupPage();

            if (Environment.GetCommandLineArgs().Any(a =>
                    string.Equals(a, "--minimized", StringComparison.OrdinalIgnoreCase)))
                Dispatcher.BeginInvoke(new Action(Hide), DispatcherPriority.ApplicationIdle);
            txtAboutLine.Text = "KT Game Booster v1.0 · por kelvenapk";
            txtUpdateChannel.Text = "canal: " + UpdateService.CurrentTag +
                " · repositório: " + UpdateService.Repository + " · download sempre validado por SHA-256";

            _updateTimer = new DispatcherTimer { Interval = TimeSpan.FromHours(6) };
            _updateTimer.Tick += (s, args) => BeginUpdateCheck(false);
            _updateTimer.Start();
            BeginUpdateCheck(false);

            string updated = Environment.GetCommandLineArgs().FirstOrDefault(a =>
                a.StartsWith("--updated=", StringComparison.OrdinalIgnoreCase));
            if (!string.IsNullOrEmpty(updated))
                txtUpdateStatus.Text = "Atualização instalada: " + updated.Substring("--updated=".Length);
        }

        private void OpenStartupPage()
        {
            string arg = Environment.GetCommandLineArgs()
                .FirstOrDefault(a => a.StartsWith("--page=", StringComparison.OrdinalIgnoreCase));
            if (string.IsNullOrEmpty(arg)) return;
            string page = arg.Substring("--page=".Length).Trim().ToLowerInvariant();
            System.Windows.Controls.RadioButton target = null;
            switch (page)
            {
                case "dashboard": target = navDashboard; break;
                case "tweaks": target = navTweaks; break;
                case "processes": case "processos": target = navProcesses; break;
                case "governor": target = navGovernor; break;
                case "services": case "servicos": case "serviços": target = navServices; break;
                case "bench": target = navBench; break;
                case "games": case "jogos": target = navGames; break;
                case "about": case "sobre": target = navAbout; break;
            }
            if (target == null) return;
            target.IsChecked = true;
        }

        private void Nav_Changed(object sender, RoutedEventArgs e)
        {
            if (!IsXamlLoaded) return;
            var rb = sender as System.Windows.Controls.RadioButton;
            if (rb == null) return;
            if (!IsXamlLoaded || pageDashboard == null || txtPageTitle == null) return;
            string page = (rb.Tag as string) ?? (rb.Content as string) ?? "";
            pageDashboard.Visibility = page == "Dashboard" ? Visibility.Visible : Visibility.Collapsed;
            pageTweaks.Visibility    = page == "Tweaks"    ? Visibility.Visible : Visibility.Collapsed;
            pageProcesses.Visibility = page == "Processos" ? Visibility.Visible : Visibility.Collapsed;
            pageGovernor.Visibility = page == "Governor" ? Visibility.Visible : Visibility.Collapsed;
            pageServices.Visibility  = page == "Servicos" ? Visibility.Visible : Visibility.Collapsed;
            pageBench.Visibility     = page == "Bench"     ? Visibility.Visible : Visibility.Collapsed;
            pageGames.Visibility     = page == "Jogos"     ? Visibility.Visible : Visibility.Collapsed;
            pageAbout.Visibility     = page == "Sobre"     ? Visibility.Visible : Visibility.Collapsed;

            string title, sub;
            switch (page)
            {
                case "Dashboard":  title = "Dashboard"; sub = "visão geral do motor e do jogo ativo"; break;
                case "Tweaks":     title = "Tweaks";    sub = "revise as opções e aplique as alterações quando estiver pronto"; break;
                case "Processos":  title = "Processos"; sub = "todos os processos com CPU% e RAM"; break;
                case "Governor":   title = "Governor"; sub = "automático no jogo · controles manuais opcionais"; break;
                case "Servicos":   title = "Serviços";  sub = "alvos automáticos, proteção crítica e controle manual"; break;
                case "Bench":      title = "Bench";     sub = "mede a latência de scheduling antes/depois"; break;
                case "Jogos":      title = "Jogos";     sub = "jogos adicionados + exceções do limitador"; break;
                case "Sobre":      title = "Sobre";     sub = "informações e changelog"; break;
                default:           title = page;        sub = ""; break;
            }
            UpdateNavHeader(title, sub);

            if (page == "Servicos") LoadServices();
            if (page == "Governor")
            {
                _lastSysProcs = KtEngine.GetSysProcesses();
                ApplyGovernorFilter();
            }
        }

        private void UpdateNavHeader(string title, string sub)
        {
            txtPageTitle.Text = title;
            txtPageSub.Text = sub;
        }

        private const int WM_HOTKEY = 0x0312;
        private const int HK_TOGGLE = 1;
        private const int HK_PAUSE = 2;
        private const int HK_CLEAN = 3;
        private const int HK_BENCH = 4;

        [DllImport("user32.dll", SetLastError = true)]
        private static extern bool RegisterHotKey(IntPtr hWnd, int id, uint modKeys, uint vk);
        [DllImport("user32.dll", SetLastError = true)]
        private static extern bool UnregisterHotKey(IntPtr hWnd, int id);

        private const uint MOD_CONTROL = 0x2, MOD_SHIFT = 0x4;

        private void RegisterGlobalHotkeys()
        {
            var helper = new WindowInteropHelper(this);
            var source = HwndSource.FromHwnd(helper.Handle);
            source?.AddHook(WndProc);
            var conflicts = new List<string>();
            if (!RegisterHotKey(helper.Handle, HK_TOGGLE, MOD_CONTROL | MOD_SHIFT, 0x4F)) conflicts.Add("Ctrl+Shift+O");
            if (!RegisterHotKey(helper.Handle, HK_PAUSE,  MOD_CONTROL | MOD_SHIFT, 0x50)) conflicts.Add("Ctrl+Shift+P");
            if (!RegisterHotKey(helper.Handle, HK_CLEAN,  MOD_CONTROL | MOD_SHIFT, 0x4D)) conflicts.Add("Ctrl+Shift+M");
            if (!RegisterHotKey(helper.Handle, HK_BENCH,  MOD_CONTROL | MOD_SHIFT, 0x42)) conflicts.Add("Ctrl+Shift+B");
            if (txtHotkeyStatus != null)
            {
                if (conflicts.Count == 0)
                {
                    txtHotkeyStatus.Text = "todos os atalhos foram registrados";
                    txtHotkeyStatus.Opacity = 0.8;
                }
                else
                {
                    txtHotkeyStatus.Text = "não registrados: " + string.Join(", ", conflicts) +
                        " (combinação em uso por outro aplicativo; os demais funcionam)";
                    txtHotkeyStatus.Opacity = 1;
                }
            }
        }

        private void UnregisterGlobalHotkeys()
        {
            try
            {
                var helper = new WindowInteropHelper(this);
                UnregisterHotKey(helper.Handle, HK_TOGGLE);
                UnregisterHotKey(helper.Handle, HK_PAUSE);
                UnregisterHotKey(helper.Handle, HK_CLEAN);
                UnregisterHotKey(helper.Handle, HK_BENCH);
            }
            catch { }
        }

        private IntPtr WndProc(IntPtr hwnd, int msg, IntPtr wParam, IntPtr lParam, ref bool handled)
        {
            if (msg == WM_HOTKEY)
            {
                int id = wParam.ToInt32();
                Dispatcher.Invoke(() =>
                {
                    try
                    {
                        EnsureConnected();
                        switch (id)
                        {
                            case HK_TOGGLE:
                                var st = KtEngine.GetState();
                                if (st.running != 0) KtEngine.Stop(); else KtEngine.Start();
                                break;
                            case HK_PAUSE:
                                KtEngine.Pause(!_paused);
                                break;
                            case HK_CLEAN:
                                var freed = KtEngine.CleanRam();
                                ShowBalloon("Limpeza de RAM", (freed / (1024.0 * 1024)).ToString("0") + " MB liberados");
                                break;
                            case HK_BENCH:
                                navBench.IsChecked = true;
                                Nav_Changed(navBench, null);
                                RunBench();
                                break;
                        }
                    }
                    catch { }
                });
                handled = true;
            }
            return IntPtr.Zero;
        }

        private void ShowBalloon(string title, string body)
        {
            try { _tray?.ShowBalloonTip(2000, title, body, ToolTipIcon.Info); } catch { }
        }

        private void InitTray()
        {
            _tray = new NotifyIcon();
            _tray.Icon = SysDrawing.SystemIcons.Application;
            try
            {
                var iconUri = new Uri("pack://application:,,,/Assets/ktgam.ico", UriKind.Absolute);
                var iconResource = System.Windows.Application.GetResourceStream(iconUri);
                if (iconResource != null)
                    using (iconResource.Stream)
                        _tray.Icon = new SysDrawing.Icon(iconResource.Stream);
            }
            catch { }
            _tray.Text = "KT Game Booster";
            _tray.Visible = true;
            var menu = new ContextMenuStrip();
            menu.Items.Add(new ToolStripMenuItem("Abrir", null, (s, ev) => ShowWindow()));
            menu.Items.Add(new ToolStripMenuItem("Pausar / Retomar", null, (s, ev) =>
            {
                try { EnsureConnected(); KtEngine.Pause(!_paused); } catch { }
            }));
            menu.Items.Add(new ToolStripMenuItem("Limpar RAM", null, (s, ev) =>
            {
                try { EnsureConnected(); ulong f = KtEngine.CleanRam(); ShowBalloon("RAM", (f / 1048576.0).ToString("0") + " MB liberados"); } catch { }
            }));
            menu.Items.Add(new ToolStripMenuItem("Restaurar shell (pânico)", null, (s, ev) =>
            {
                try { EnsureConnected(); KtEngine.KT_RestoreShell(); } catch { }
                RestoreShellFallback();
            }));
            menu.Items.Add(new ToolStripSeparator());
            menu.Items.Add(new ToolStripMenuItem("Encerrar motor", null, (s, ev) => QuitApp()));
            _tray.ContextMenuStrip = menu;
            _tray.MouseClick += (s, ev) => { if (ev.Button == MouseButtons.Left) ShowWindow(); };
        }

        private void ShowWindow()
        {
            Dispatcher.Invoke(() =>
            {
                Show();
                WindowState = WindowState.Normal;
                Activate();
            });
        }

        private void QuitApp()
        {
            _forceClose = true;
            try { _updateTimer?.Stop(); } catch { }
            try { _timer?.Stop(); } catch { }
            try { _tray?.Dispose(); } catch { }
            System.Windows.Application.Current.Shutdown();
        }

        private void MainWindow_Closing(object sender, System.ComponentModel.CancelEventArgs e)
        {
            if (!_forceClose)
            {
                e.Cancel = true;
                Hide();
                WindowState = WindowState.Minimized;
            }
            else
            {
                try { _updateTimer?.Stop(); } catch { }
                try { _timer?.Stop(); } catch { }
                try { _infoBarHideTimers.Clear(); } catch { }
                UnregisterGlobalHotkeys();
                try { _tray?.Dispose(); } catch { }
            }
        }

        private int _lastRunFlag = 0;
        private int _lastGameFlag = 0;

        private void Window_StateChanged(object sender, EventArgs e)
        {
            if (WindowState == WindowState.Minimized)
            {
                Hide();
                string body;
                if (App.SafeMode) body = "Minimizado para a bandeja. Modo seguro: nenhum tweak será aplicado.";
                else if (!_engineReady) body = "Minimizado para a bandeja. Motor indisponível; rode a recuperação ao reabrir.";
                else if (_lastRunFlag == 0) body = "Minimizado para a bandeja. Motor parado.";
                else if (_paused) body = "Minimizado para a bandeja. Motor pausado.";
                else body = "Minimizado para a bandeja. O motor continua ativo.";
                _tray?.ShowBalloonTip(2000, "KT Game Booster", body, ToolTipIcon.Info);
            }
        }

        private void TitleBar_MouseLeftButtonDown(object sender, System.Windows.Input.MouseButtonEventArgs e)
        {
            var source = e.OriginalSource as DependencyObject;
            if (source != null && source.GetAncestor<System.Windows.Controls.Button>() != null) return;
            if (e.ClickCount == 2)
                WindowState = WindowState == WindowState.Maximized ? WindowState.Normal : WindowState.Maximized;
            else if (e.LeftButton == System.Windows.Input.MouseButtonState.Pressed)
                try { DragMove(); } catch { }
        }

        private const int WM_GETMINMAXINFO = 0x0024;
        private const int WM_NCHITTEST = 0x0084;
        private const uint MONITOR_DEFAULTTONEAREST = 2;
        private const int HTCLIENT = 1, HTLEFT = 10, HTRIGHT = 11, HTTOP = 12,
                          HTTOPLEFT = 13, HTTOPRIGHT = 14, HTBOTTOM = 15,
                          HTBOTTOMLEFT = 16, HTBOTTOMRIGHT = 17;

        [StructLayout(LayoutKind.Sequential)]
        private struct NativePoint { public int x; public int y; }

        [StructLayout(LayoutKind.Sequential)]
        private struct MinMaxInfo
        {
            public NativePoint ptReserved;
            public NativePoint ptMaxSize;
            public NativePoint ptMaxPosition;
            public NativePoint ptMinTrackSize;
            public NativePoint ptMaxTrackSize;
        }

        [StructLayout(LayoutKind.Sequential)]
        private struct NativeRect { public int left; public int top; public int right; public int bottom; }

        [StructLayout(LayoutKind.Sequential)]
        private struct MonitorInfo
        {
            public int cbSize;
            public NativeRect rcMonitor;
            public NativeRect rcWork;
            public uint dwFlags;
        }

        [DllImport("user32.dll")]
        private static extern IntPtr MonitorFromWindow(IntPtr hwnd, uint flags);

        [DllImport("user32.dll", SetLastError = true)]
        private static extern bool GetMonitorInfo(IntPtr monitor, ref MonitorInfo info);

        [DllImport("user32.dll", SetLastError = true)]
        private static extern bool GetWindowRect(IntPtr hwnd, out NativeRect rect);

        private IntPtr WindowProc(IntPtr hwnd, int msg, IntPtr wParam, IntPtr lParam, ref bool handled)
        {
            if (msg == WM_NCHITTEST && WindowState == WindowState.Normal)
            {
                NativeRect rect;
                if (!GetWindowRect(hwnd, out rect)) return IntPtr.Zero;
                long packed = lParam.ToInt64();
                int x = unchecked((short)(packed & 0xFFFF));
                int y = unchecked((short)((packed >> 16) & 0xFFFF));
                const int grip = 8;
                bool left = x >= rect.left && x < rect.left + grip;
                bool right = x <= rect.right && x > rect.right - grip;
                bool top = y >= rect.top && y < rect.top + grip;
                bool bottom = y <= rect.bottom && y > rect.bottom - grip;
                handled = left || right || top || bottom;
                if (top && left) return new IntPtr(HTTOPLEFT);
                if (top && right) return new IntPtr(HTTOPRIGHT);
                if (bottom && left) return new IntPtr(HTBOTTOMLEFT);
                if (bottom && right) return new IntPtr(HTBOTTOMRIGHT);
                if (left) return new IntPtr(HTLEFT);
                if (right) return new IntPtr(HTRIGHT);
                if (top) return new IntPtr(HTTOP);
                if (bottom) return new IntPtr(HTBOTTOM);
                return new IntPtr(HTCLIENT);
            }

            if (msg != WM_GETMINMAXINFO || lParam == IntPtr.Zero) return IntPtr.Zero;

            var info = (MinMaxInfo)Marshal.PtrToStructure(lParam, typeof(MinMaxInfo));
            IntPtr monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
            if (monitor != IntPtr.Zero)
            {
                var monitorInfo = new MonitorInfo { cbSize = Marshal.SizeOf(typeof(MonitorInfo)) };
                if (GetMonitorInfo(monitor, ref monitorInfo))
                {
                    info.ptMaxPosition.x = monitorInfo.rcWork.left - monitorInfo.rcMonitor.left;
                    info.ptMaxPosition.y = monitorInfo.rcWork.top - monitorInfo.rcMonitor.top;
                    info.ptMaxSize.x = monitorInfo.rcWork.right - monitorInfo.rcWork.left;
                    info.ptMaxSize.y = monitorInfo.rcWork.bottom - monitorInfo.rcWork.top;
                    Marshal.StructureToPtr(info, lParam, true);
                    handled = true;
                }
            }
            return IntPtr.Zero;
        }

        private void TitleMinimize_Click(object sender, RoutedEventArgs e)
        {
            WindowState = WindowState.Minimized;
        }

        private void TitleMaximize_Click(object sender, RoutedEventArgs e)
        {
            WindowState = WindowState == WindowState.Maximized ? WindowState.Normal : WindowState.Maximized;
        }

        private void btnGitHub_Click(object sender, RoutedEventArgs e)
        {
            try
            {
                Process.Start(new ProcessStartInfo("https://github.com/kelvenapk") { UseShellExecute = true });
            }
            catch { }
        }

        private async void BeginUpdateCheck(bool manual)
        {
            if (_updateCheckRunning) return;
            _updateCheckRunning = true;
            if (txtUpdateStatus != null) txtUpdateStatus.Text = "Verificando atualização privada no GitHub...";
            try
            {
                var result = await Task.Run(() => UpdateService.CheckAndDownload());
                _pendingUpdate = result.UpdateAvailable ? result : null;
                txtUpdateStatus.Text = result.Message;
                btnInstallUpdate.Visibility = result.UpdateAvailable ? Visibility.Visible : Visibility.Collapsed;
                if (result.UpdateAvailable)
                {
                    ShowBalloon("Atualização pronta", result.Tag + " foi baixada e validada por SHA-256.");
                    if (manual || !_updatePromptShown)
                    {
                        _updatePromptShown = true;
                        if (System.Windows.MessageBox.Show(
                            result.Tag + " foi baixada e verificada. Reiniciar e instalar agora?",
                            "KT Game Booster · atualização automática",
                            MessageBoxButton.YesNo, MessageBoxImage.Information) == MessageBoxResult.Yes)
                            InstallPendingUpdate();
                    }
                }
            }
            catch (Exception ex)
            {
                if (txtUpdateStatus != null) txtUpdateStatus.Text = "Falha na verificação: " + ex.Message;
            }
            finally { _updateCheckRunning = false; }
        }

        private void btnCheckUpdate_Click(object sender, RoutedEventArgs e)
        {
            BeginUpdateCheck(true);
        }

        private void btnInstallUpdate_Click(object sender, RoutedEventArgs e)
        {
            InstallPendingUpdate();
        }

        private void InstallPendingUpdate()
        {
            try
            {
                if (_pendingUpdate == null) return;
                txtUpdateStatus.Text = "Preparando instalação segura...";
                if (UpdateService.StartInstaller(_pendingUpdate, Process.GetCurrentProcess().Id,
                    AppDomain.CurrentDomain.BaseDirectory))
                    QuitApp();
            }
            catch (Exception ex)
            {
                txtUpdateStatus.Text = "Não foi possível iniciar a atualização: " + ex.Message;
            }
        }

        private void EnsureConnected()
        {
            if (_engineReady) return;
            try
            {
                int initialized = KtEngine.KT_Initialize();
                if (initialized == 0 || initialized == 1)
                {
                    KtEngine.SetSafeMode(App.SafeMode);
                    if (App.SafeMode)
                    {
                        if (!KtEngine.RecoverPendingSession())
                            throw new InvalidOperationException("Não foi possível restaurar a sessão pendente.");
                    }
                    else if (KtEngine.KT_Start() != 1)
                    {
                        throw new InvalidOperationException("O motor recusou iniciar; consulte o diagnóstico.");
                    }
                    _engineReady = true;
                    _lastEngineError = "";
                    if (txtEngineHint != null) txtEngineHint.Visibility = Visibility.Collapsed;
                    if (btnRecoverEngine != null) btnRecoverEngine.Visibility = Visibility.Collapsed;
                    btnCleanRam.IsEnabled = !App.SafeMode;
                    cmbProfile.IsEnabled = !App.SafeMode;
                    chkAutoProfile.IsEnabled = !App.SafeMode;
                    pageTweaks.IsEnabled = !App.SafeMode;
                }
                else throw new InvalidOperationException("Falha ao inicializar o motor (código " + initialized + ").");
            }
            catch (Exception ex)
            {
                _engineReady = false;
                ReportUiError("motor", ex);
            }
        }

        private void btnRecoverEngine_Click(object sender, RoutedEventArgs e)
        {
            try
            {
                EnsureConnected();
                KtEngine.SetSafeMode(true);
                bool ok = KtEngine.RecoverPendingSession();
                KtEngine.SetSafeMode(false);
                txtStatus.Text = ok ? "sessão pendente restaurada" : "a sessão ainda precisa de recuperação";
                if (ok) { _engineReady = false; EnsureConnected(); }
            }
            catch (Exception ex) { ReportUiError("recuperação", ex); }
            Tick(null, EventArgs.Empty);
        }

        private void ReportUiError(string area, Exception ex)
        {
            string message = ex.GetType().Name + ": " + ex.Message;
            if (_lastEngineError == area + message) return;
            _lastEngineError = area + message;
            try
            {
                File.AppendAllText(Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "kt-ui.log"),
                    "[" + DateTime.Now.ToString("yyyy-MM-dd HH:mm:ss") + "] " + area.ToUpperInvariant() + "\r\n" + ex + "\r\n\r\n");
            }
            catch { }
            if (txtStatus != null) txtStatus.Text = area + ": " + ex.Message;
        }

        private void btnExportTelemetry_Click(object sender, RoutedEventArgs e)
        {
            try
            {
                EnsureConnected();
                string folder = TelemetryExportService.Export(KtEngine.GetMetrics(600));
                txtBenchStatus.Text = "Exportação local criada em: " + folder;
            }
            catch (Exception ex) { ReportUiError("telemetria", ex); }
        }

        private void RestoreShellFallback()
        {
            try
            {
                foreach (var p in Process.GetProcessesByName("winlogon"))
                {
                    IntPtr h = OpenProcess(0x1FFFFF, false, (uint)p.Id);
                    if (h != IntPtr.Zero)
                    {
                        NtResumeProcess(h);
                        CloseHandle(h);
                    }
                }
                if (Process.GetProcessesByName("explorer").Length == 0)
                    Process.Start(Path.Combine(
                        Environment.GetFolderPath(Environment.SpecialFolder.Windows), "explorer.exe"));
            }
            catch { }
            finally
            {
                _lastExtremeActive = false;
            }
        }

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern IntPtr OpenProcess(uint access, bool inherit, uint pid);
        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool CloseHandle(IntPtr h);
        [DllImport("kernel32.dll")]
        private static extern bool GetSystemTimes(out NativeFileTime idle, out NativeFileTime kernel, out NativeFileTime user);
        [DllImport("kernel32.dll")]
        private static extern bool GlobalMemoryStatusEx(ref NativeMemoryStatus status);
        [DllImport("ntdll.dll")]
        private static extern uint NtResumeProcess(IntPtr h);

        [StructLayout(LayoutKind.Sequential)]
        private struct NativeFileTime
        {
            public uint LowDateTime;
            public uint HighDateTime;
        }

        [StructLayout(LayoutKind.Sequential)]
        private struct NativeMemoryStatus
        {
            public uint dwLength;
            public uint dwMemoryLoad;
            public ulong ullTotalPhys;
            public ulong ullAvailPhys;
            public ulong ullTotalPageFile;
            public ulong ullAvailPageFile;
            public ulong ullTotalVirtual;
            public ulong ullAvailVirtual;
            public ulong ullAvailExtendedVirtual;
        }

        private void Tick(object sender, EventArgs e)
        {
            KtState st = default(KtState);
            bool offline;
            try
            {
                EnsureConnected();
                if (!_engineReady)
                {
                    OfflineTick();
                    connDot.Fill = BrushDanger;
                    return;
                }
                st = KtEngine.GetState();
                offline = false;
            }
            catch (Exception ex)
            {
                _engineReady = false;
                ReportUiError("motor", ex);
                OfflineTick();
                connDot.Fill = BrushDanger;
                return;
            }

            try
            {
                if (offline) return;
                if (st.gameActive != 0 && st.gamePid != 0 && st.gamePid != _lastNotifiedGamePid)
                {
                    _lastNotifiedGamePid = st.gamePid;
                    if (!IsVisible || WindowState == WindowState.Minimized)
                        ShowBalloon("Jogo detectado · otimizando",
                            (st.gameName ?? "jogo") + " está recebendo o perfil ativo.");
                }
                else if (st.gameActive == 0 && _lastNotifiedGamePid != 0)
                {
                    if (!IsVisible || WindowState == WindowState.Minimized)
                        ShowBalloon("Sessão finalizada", "Tweaks temporários restaurados com segurança.");
                    _lastNotifiedGamePid = 0;
                }
                _paused = st.paused != 0;
                _tournament = st.tournament != 0;
                _isElevated = st.elevated != 0;
                _lastRunFlag = st.running;
                _lastGameFlag = st.gameActive;

                ApplyGameUiMode(st.gameActive != 0 && st.running != 0, st.gameName);
                adminBadge.Visibility = _isElevated ? Visibility.Visible : Visibility.Collapsed;
                adminMiniDot.Visibility = _isElevated ? Visibility.Visible : Visibility.Collapsed;
                connDot.Fill = _lastExtremeActive ? BrushDanger : BrushAccent;

                string label;
                if (st.running == 0) label = "parado";
                else if (st.paused != 0) label = "pausado";
                else if (st.gameActive != 0) label = "otimizando";
                else label = "rodando";
                txtStatus.Text = label + (st.tournament != 0 ? " · torneio" : "");
                if (txtEngineState != null)
                {
                    txtEngineState.Text = st.running == 0 ? "MOTOR PARADO"
                        : st.paused != 0 ? "MOTOR PAUSADO"
                        : st.gameActive != 0 ? "SESSÃO OTIMIZADA"
                        : "MOTOR MONITORANDO";
                    engineStateDot.Fill = st.running == 0 ? BrushWarn
                        : st.paused != 0 ? BrushWarn
                        : st.gameActive != 0 ? BrushSuccess
                        : BrushAccent3;
                    txtEngineState.Foreground = st.running == 0 || st.paused != 0
                        ? BrushWarn
                        : st.gameActive != 0 ? BrushSuccess : BrushAccent3;
                }
                if (txtGovAutoStatus != null)
                {
                    txtGovAutoStatus.Text = st.running == 0 ? "AUTO · MOTOR PARADO"
                        : st.paused != 0 ? "AUTO · PAUSADO"
                        : st.gameActive != 0 ? "AUTO · OTIMIZANDO"
                        : "AUTO · MONITORANDO";
                    txtGovAutoStatus.Foreground = st.gameActive != 0 ? BrushSuccess
                        : st.running != 0 && st.paused == 0 ? BrushAccent3 : BrushWarn;
                    txtGovAutoDetail.Text = st.gameActive != 0
                        ? "Governor automático ativo em " + (st.gameName ?? "jogo") + " (PID " + st.gamePid + "). " +
                          "Prioridade, memória, I/O, EcoQoS, CPU e helpers são acompanhados pelo motor C++."
                        : "Governor automático ligado: ao detectar um jogo, aplica prioridade alta, remove EcoQoS, " +
                          "eleva I/O e memória e acompanha novos helpers. " +
                          "Tudo volta ao estado original ao terminar.";
                }
                bool ext = st.extremeActive != 0;
                extremeBanner.Visibility = ext ? Visibility.Visible : Visibility.Collapsed;
                txtExtremeStatus.Text = ext
                    ? "Restauração de uma sessão antiga pendente. Use o botão para restaurar o shell."
                    : "";
                _lastExtremeActive = ext;

                SyncTogglesFromState(st, false);

                double cpu = st.sysCpu; if (cpu < 0) cpu = 0; if (cpu > 100) cpu = 100;
                double ramPct = st.sysRamTotalMB > 0 ? (double)st.sysRamUsedMB / st.sysRamTotalMB * 100.0 : 0;
                AppendSeries(ref _cpuSeries, cpu / 100.0);
                AppendSeries(ref _ramSeries, ramPct / 100.0);
                cpuBar.Value = cpu;
                txtCpu.Text = cpu.ToString("0") + "%";
                txtCpu.Foreground = GetUsageBrush(cpu);
                ramBar.Value = ramPct;
                txtRam.Text = (st.sysRamUsedMB / 1024.0).ToString("0.0") + " GB";
                txtBg.Text = st.bgCount.ToString();
                txtPeakCpu.Text = (st.peakCpuPctX100 / 100.0).ToString("0") + "%";
                txtPeakRam.Text = (st.peakRamPctX100 / 100.0).ToString("0") + "%";
                sparkCpu.Values = (double[])_cpuSeries.Clone();
                sparkRam.Values = (double[])_ramSeries.Clone();

                txtGame.Text = st.gameActive != 0 ? (st.gameName ?? "jogo") : "aguardando...";
                txtGameDetail.Text = st.gameActive != 0
                    ? "PID " + st.gamePid + "  ·  sessão " + FormatDuration(st.gameUptimeSec) + "  ·  uptime " + FormatDuration(st.uptimeSec)
                    : "motor " + (st.running != 0 ? "rodando" : "parado") + " · " + st.procCount + " processos · " + FormatDuration(st.uptimeSec);

                if (st.gameActive != 0 && st.frameSamples > 0 && st.fpsX100 > 0)
                {
                    txtFps.Text = "FPS " + (st.fpsX100 / 100.0).ToString("0.#") +
                                  "  ·  frame P95 " + (st.frameP95us / 1000.0).ToString("0.0") + " ms" +
                                  (st.frameSamples >= 100 && st.frameP99us > 0
                                      ? "  ·  1% low " + (1000000.0 / st.frameP99us).ToString("0.#") + " FPS"
                                      : "  ·  1% low coletando");
                    txtFps.Foreground = BrushSuccess;
                }
                else if (st.gameActive != 0)
                {
                    txtFps.Text = "FPS: aguardando eventos Present do jogo...";
                    txtFps.Foreground = System.Windows.Media.Brushes.Gray;
                }
                else
                {
                    txtFps.Text = "";
                }

                if (st.gpuPctX100 >= 0)
                {
                    string gpuName = string.IsNullOrWhiteSpace(st.gpuName) ? "GPU" : st.gpuName.Trim();
                    txtGpu.Text = "GPU " + (st.gpuPctX100 / 100.0).ToString("0.#") + "% · " + gpuName;
                    txtGpu.Foreground = BrushAccent3;
                }
                else
                {
                    txtGpu.Text = "GPU: telemetria indisponível";
                    txtGpu.Foreground = BrushWarn;
                }

                if (st.gameActive != 0 && st.detectionConfidence > 0)
                {
                    confBadge.Visibility = Visibility.Visible;
                    string lvl = st.detectionConfidence >= 90 ? "alta" : st.detectionConfidence >= 60 ? "média" : "baixa";
                    txtConfidence.Text = "detecção: " + lvl + " (" + st.detectionConfidence + "%)";
                }
                else confBadge.Visibility = Visibility.Collapsed;

                if (st.anticheatSafe != 0 && st.gameActive != 0)
                {
                    acChip.Visibility = Visibility.Visible;
                    txtAcChip.Text = "ANTICHEAT " + AnticheatFriendlyName(st.anticheatKind) + " · MODO SEGURO";
                }
                else acChip.Visibility = Visibility.Collapsed;

                if (presetChip != null)
                {
                    string autoPreset = st.gameActive != 0 ? (st.gameProfile ?? "") : "";
                    if (!string.IsNullOrWhiteSpace(autoPreset))
                    {
                        string presetLabel = autoPreset == "tournament" ? "TORNEIO"
                                     : autoPreset == "bg_only" ? "SEGUNDO PLANO"
                                     : autoPreset == "streaming" ? "STREAMING"
                                     : autoPreset.ToUpperInvariant();
                        presetChip.Visibility = Visibility.Visible;
                        txtPresetChip.Text = "PRESET AUTOMÁTICO · " + presetLabel;
                    }
                    else presetChip.Visibility = Visibility.Collapsed;
                }

                btnTopStart.Content = st.running != 0 ? "Parar" : "Iniciar";
                btnTopPause.IsEnabled = st.running != 0;
                btnTopPause.Content = _paused ? "Retomar" : "Pausar";
                btnTournament.Content = _tournament ? "FPS Máximo: ON" : "FPS Máximo";

                txtUptime.Text      = FormatDuration(st.uptimeSec);
                txtSessions.Text    = st.totalGameSessions.ToString();
                txtTotalGameTime.Text = FormatDuration((uint)Math.Min(st.totalGameSeconds, uint.MaxValue));
                txtMemFreed.Text    = (st.memFreed / (1024 * 1024)).ToString() + " MB";
                txtEtw.Text         = st.etwEvents.ToString("N0");
                txtEtwMode.Text     = st.etwActive != 0 ? "ETW ativo · eventos processados" : "polling ativo · ETW indisponível";
                txtEtwMode.Foreground = st.etwActive != 0 ? BrushSuccess : BrushWarn;
                txtSuspended.Text   = st.suspendedThreads.ToString();

                txtHelpers.Text = st.boostedChildCount.ToString();
                if (st.boostedChildCount != _lastBoostedChildCount)
                {
                    _lastBoostedChildCount = st.boostedChildCount;
                    txtHelpers.Foreground = BrushAccent3;
                }
                if (st.servicePauseCount > 0 || _lastSvcPauseCount > 0)
                {
                    txtSvcPause.Text = (st.servicePauseCount > 0 ? "serviços otimizados: " + st.servicePauseCount : "");
                    _lastSvcPauseCount = st.servicePauseCount;
                }

                string prof = st.tournament != 0 ? "torneio" : _activeProfile;
                txtActiveProfile.Text = "perfil: " + prof;

                if (IsVisible && WindowState != WindowState.Minimized &&
                    (navProcesses.IsChecked == true || navGovernor.IsChecked == true))
                {
                    _lastSysProcs = KtEngine.GetSysProcesses();
                    if (navProcesses.IsChecked == true) ApplyProcFilter();
                    if (navGovernor.IsChecked == true) ApplyGovernorFilter();
                }

                if (IsVisible && WindowState != WindowState.Minimized &&
                    navServices.IsChecked == true && _tickCount % 3 == 0)
                    LoadServices();

                if (IsVisible && WindowState != WindowState.Minimized && _tickCount % 2 == 0)
                    RefreshLog();

                _tickCount++;

                try
                {
                    string trayText;
                    if (st.running == 0) trayText = "KT Game Booster - parado";
                    else if (st.paused != 0) trayText = "KT Game Booster - pausado";
                    else if (st.gameActive != 0) trayText = "KT Booster - " + (st.gameName ?? "jogo");
                    else trayText = "KT Game Booster - rodando";
                    if (trayText.Length > 63) trayText = trayText.Substring(0, 63);
                    if (_tray != null && _tray.Text != trayText) _tray.Text = trayText;
                }
                catch { }
            }
            catch (Exception ex)
            {
                ReportUiError("atualização", ex);
                connDot.Fill = BrushAccent;
            }
        }

        private void OfflineTick()
        {
            if (IsVisible && WindowState != WindowState.Minimized &&
                (navProcesses.IsChecked == true || navGovernor.IsChecked == true))
            {
                _lastSysProcs = GetOfflineProcesses();
                if (navProcesses.IsChecked == true) ApplyProcFilter();
                if (navGovernor.IsChecked == true) ApplyGovernorFilter();
            }
            if (_lastExtremeActive) RestoreShellFallback();
            UpdateOfflineTelemetry();
        }

        private void UpdateOfflineTelemetry()
        {
            try
            {
                NativeFileTime idle, kernel, user;
                double cpu = 0;
                if (GetSystemTimes(out idle, out kernel, out user))
                {
                    ulong idleNow = FileTimeValue(idle);
                    ulong kernelNow = FileTimeValue(kernel);
                    ulong userNow = FileTimeValue(user);
                    if (_offlineCpuReady)
                    {
                        ulong totalDelta = (kernelNow - _offlineKernelTicks) + (userNow - _offlineUserTicks);
                        ulong idleDelta = idleNow - _offlineIdleTicks;
                        if (totalDelta > 0)
                            cpu = Math.Max(0, Math.Min(100, 100.0 - (idleDelta * 100.0 / totalDelta)));
                    }
                    _offlineIdleTicks = idleNow;
                    _offlineKernelTicks = kernelNow;
                    _offlineUserTicks = userNow;
                    _offlineCpuReady = true;
                }

                NativeMemoryStatus mem = new NativeMemoryStatus { dwLength = (uint)Marshal.SizeOf(typeof(NativeMemoryStatus)) };
                GlobalMemoryStatusEx(ref mem);
                double ramUsedMB = Math.Max(0, (double)(mem.ullTotalPhys - mem.ullAvailPhys) / (1024 * 1024));
                double ramTotalMB = Math.Max(1, (double)mem.ullTotalPhys / (1024 * 1024));
                double ramPct = ramUsedMB / ramTotalMB * 100.0;
                AppendSeries(ref _cpuSeries, cpu / 100.0);
                AppendSeries(ref _ramSeries, ramPct / 100.0);
                cpuBar.Value = cpu;
                ramBar.Value = ramPct;
                txtCpu.Text = cpu.ToString("0") + "%";
                txtCpu.Foreground = GetUsageBrush(cpu);
                txtRam.Text = (ramUsedMB / 1024.0).ToString("0.0") + " GB";
                txtBg.Text = "—";
                txtPeakCpu.Text = "—";
                txtPeakRam.Text = "—";
                sparkCpu.Values = (double[])_cpuSeries.Clone();
                sparkRam.Values = (double[])_ramSeries.Clone();

                txtGame.Text = "telemetria local";
                txtGameDetail.Text = "motor offline · " + Process.GetProcesses().Length + " processos · recuperação necessária";
                txtFps.Text = "FPS do jogo indisponível enquanto o motor não conclui a recuperação.";
                txtFps.Foreground = BrushWarn;
                acChip.Visibility = Visibility.Collapsed;
                confBadge.Visibility = Visibility.Collapsed;
                if (presetChip != null)
                {
                    presetChip.Visibility = Visibility.Collapsed;
                    txtPresetChip.Text = "";
                }
                txtUptime.Text = "—";
                txtSessions.Text = "—";
                txtTotalGameTime.Text = "sessão não iniciada";
                txtEtw.Text = "—";
                txtEtwMode.Text = "motor offline · ETW pausado";
                txtEtwMode.Foreground = BrushWarn;
                txtSuspended.Text = "—";
                txtHelpers.Text = "—";
                txtMemFreed.Text = "—";
                txtMemThisClean.Text = "recuperação pendente";
                txtSvcPause.Text = "";
                txtStatus.Text = "motor offline · somente leitura";
                txtActiveProfile.Text = "perfil: aguardando motor";
                btnTopStart.Content = "Tentar novamente";
                btnTopPause.IsEnabled = false;
                btnTournament.IsEnabled = false;
                btnCleanRam.IsEnabled = false;
                cmbProfile.IsEnabled = false;
                chkAutoProfile.IsEnabled = false;
                pageTweaks.IsEnabled = false;
                txtEngineState.Text = "RECUPERAÇÃO PENDENTE";
                txtEngineState.Foreground = BrushWarn;
                engineStateDot.Fill = BrushWarn;
                txtGovAutoStatus.Text = "AUTO · BLOQUEADO";
                txtGovAutoStatus.Foreground = BrushWarn;
                txtGovAutoDetail.Text = "O motor não aplicará ajustes até restaurar a sessão anterior. As estatísticas locais continuam disponíveis.";
                txtEngineHint.Text = "sessão anterior não foi restaurada; abra como administrador se a recuperação continuar falhando";
                txtEngineHint.Visibility = Visibility.Visible;
                btnRecoverEngine.Visibility = Visibility.Visible;
                var diagnostic = GetEngineDiagnosticLines();
                if (diagnostic.Length > 0)
                {
                    lstLog.ItemsSource = diagnostic;
                    txtLogCount.Text = "(" + diagnostic.Length + ")";
                }
                else
                {
                    lstLog.ItemsSource = new[] { "motor offline · recuperação da sessão pendente necessária" };
                    txtLogCount.Text = "(1)";
                }
            }
            catch (Exception ex)
            {
                ReportUiError("telemetria local", ex);
            }
        }

        private static ulong FileTimeValue(NativeFileTime value)
        {
            return ((ulong)value.HighDateTime << 32) | value.LowDateTime;
        }

        private string[] GetEngineDiagnosticLines()
        {
            try
            {
                return KtEngine.GetLogV2(0, 24)
                    .Select(x => x.text)
                    .Where(x => !string.IsNullOrWhiteSpace(x))
                    .ToArray();
            }
            catch { return new string[0]; }
        }

        private KtSysProc[] GetOfflineProcesses()
        {
            var rows = new List<KtSysProc>();
            foreach (var process in Process.GetProcesses())
            {
                try
                {
                    long ramMb = Math.Max(0L, process.WorkingSet64 / (1024L * 1024L));
                    rows.Add(new KtSysProc
                    {
                        pid = (uint)process.Id,
                        name = process.ProcessName + ".exe",
                        klass = "normal",
                        ramMB = (uint)Math.Min((long)uint.MaxValue, ramMb),
                        cpuPct = 0,
                        affinity = 0
                    });
                }
                catch { }
                finally { process.Dispose(); }
            }
            return rows.ToArray();
        }

        private void AppendSeries(ref double[] s, double v)
        {
            const int cap = 120;
            if (s.Length >= cap)
            {
                var n = new double[cap];
                Array.Copy(s, s.Length - cap + 1, n, 0, cap - 1);
                s = n;
            }
            var g = new double[s.Length + 1];
            Array.Copy(s, g, s.Length);
            g[g.Length - 1] = v;
            s = g;
        }

        private bool IsChecked(string tag)
        {
            var cb = FindToggleByTag(tag);
            return cb != null && cb.IsChecked == true;
        }

        private System.Windows.Controls.CheckBox FindToggleByTag(string tag)
        {
            foreach (var cb in FindAllToggles())
                if ((string)cb.Tag == tag) return cb;
            return null;
        }

        private static string FormatDuration(uint sec)
        {
            if (sec < 60) return sec + "s";
            if (sec < 3600) return (sec / 60) + "m " + (sec % 60) + "s";
            return (sec / 3600) + "h " + ((sec % 3600) / 60) + "m";
        }

        private static SolidColorBrush GetUsageBrush(double pct)
        {
            if (pct >= 85) return ThemeRes.Res("DangerBrush", FallbackDanger);
            if (pct >= 60) return ThemeRes.Res("WarnBrush", FallbackWarn);
            return ThemeRes.Res("SuccessBrush", FallbackSuccess);
        }

        private static string AnticheatFriendlyName(int kind)
        {
            switch (kind)
            {
                case 1: return "(EAC)";
                case 2: return "(BattlEye)";
                case 3: return "(Vanguard)";
                case 4: return "(FACEIT/ESEA)";
                case 5: return "(nProtect/Xign)";
                case 6: return "(Ricochet)";
                case 7: return "(Tencent/ACE)";
                case 8: return "(outro)";
                default: return "";
            }
        }

        private void btnThemeToggle_Click(object sender, RoutedEventArgs e)
        {
            var next = ThemeManager.Next(ThemeManager.Current);
            ThemeManager.Apply(next);
            if (btnThemeToggle != null)
                btnThemeToggle.Content = "Tema: " + ThemeManager.NextLabel(ThemeManager.Current);
        }

        private void ShowInfoBar(Border bar, TextBlock msg, string text, string severity)
        {
            if (bar == null || msg == null) return;
            string key, glyph, brushKey;
            switch (severity)
            {
                case "Success": key = "InfoBarSuccess"; glyph = "\uE73E"; brushKey = "SuccessBrush"; break;
                case "Warning": key = "InfoBarWarning"; glyph = "\uE7BA"; brushKey = "WarnBrush"; break;
                case "Error":   key = "InfoBarError";   glyph = "\uE711"; brushKey = "DangerBrush"; break;
                default:        key = "InfoBarInfo";    glyph = "\uE946"; brushKey = "InfoBrush"; break;
            }
            var style = TryFindResource(key) as Style;
            if (style != null) bar.Style = style;
            var layout = bar.Child as Grid;
            var icon = layout?.Children.OfType<TextBlock>().FirstOrDefault(t => !ReferenceEquals(t, msg));
            if (icon != null)
            {
                icon.Text = glyph;
                icon.Foreground = TryFindResource(brushKey) as Brush ?? icon.Foreground;
            }
            msg.Text = text;
            bar.Visibility = Visibility.Visible;
        }

        private void ScheduleInfoBarHide(Border bar, int milliseconds)
        {
            if (bar == null) return;
            _infoBarHideTimers.TryGetValue(bar, out DispatcherTimer current);
            current?.Stop();
            var timer = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(milliseconds) };
            timer.Tick += (s, e) =>
            {
                timer.Stop();
                _infoBarHideTimers.Remove(bar);
                bar.Visibility = Visibility.Collapsed;
            };
            _infoBarHideTimers[bar] = timer;
            timer.Start();
        }

        private bool IsInPageTweaks(System.Windows.Controls.CheckBox cb)
        {
            if (cb == null || pageTweaks == null) return false;
            var cur = (DependencyObject)cb;
            while (cur != null)
            {
                if (ReferenceEquals(cur, pageTweaks)) return true;
                cur = LogicalTreeHelper.GetParent(cur);
            }
            return false;
        }

        private void UpdateStagedUi()
        {
            int n = _stagedTweaks.Count;
            if (stagedBar != null)
                stagedBar.Visibility = n > 0 ? Visibility.Visible : Visibility.Collapsed;
            if (btnReviewStaged != null)
                btnReviewStaged.Visibility = n > 0 ? Visibility.Visible : Visibility.Collapsed;
            if (txtStagedCount != null)
                txtStagedCount.Text = n + " alteração(ões) pendente(s) · revise e aplique";
            if (btnApplyStaged != null)
                btnApplyStaged.IsEnabled = n > 0 && _engineReady && !App.SafeMode;
            if (btnUndoStaged != null)
                btnUndoStaged.IsEnabled = n > 0;
        }

        private void btnApplyStaged_Click(object sender, RoutedEventArgs e)
        {
            if (_stagedTweaks.Count == 0) return;
            try
            {
                EnsureConnected();
                if (!_engineReady)
                {
                    ShowInfoBar(infoBarTweaks, infoBarTweaksMsg,
                        "Motor indisponível; não foi possível aplicar as alterações.", "Error");
                    return;
                }
                var failed = new List<string>();
                string profileId = SelectedProfileId;
                bool appliedAny = false;
                foreach (var kv in _stagedTweaks.ToList())
                {
                    if (KtEngine.SetFeature(kv.Key, kv.Value) == 1)
                    {
                        UiPreferenceStore.SaveFeatureOverride(profileId, kv.Key, kv.Value);
                        _stagedTweaks.Remove(kv.Key);
                        appliedAny = true;
                    }
                    else
                        failed.Add(kv.Key);
                }
                if (appliedAny) _activeProfile = "personalizado";
                _lastFeatureMask = int.MinValue;
                SyncTogglesFromState(KtEngine.GetState(), true);
                UpdateStagedUi();
                if (failed.Count == 0)
                {
                    ShowInfoBar(infoBarTweaks, infoBarTweaksMsg, "Alterações aplicadas com sucesso.", "Success");
                    ScheduleInfoBarHide(infoBarTweaks, 3000);
                    txtStatus.Text = "tweaks aplicados";
                }
                else
                {
                    ShowInfoBar(infoBarTweaks, infoBarTweaksMsg,
                        failed.Count + " alteração(ões) falharam e continuam pendentes para revisão.", "Error");
                    txtStatus.Text = "alguns tweaks falharam; pendências mantidas";
                }
            }
            catch (Exception ex)
            {
                ReportUiError("aplicar tweaks", ex);
                ShowInfoBar(infoBarTweaks, infoBarTweaksMsg, "Falha ao aplicar: " + ex.Message, "Error");
            }
        }

        private void btnUndoStaged_Click(object sender, RoutedEventArgs e)
        {
            _stagedTweaks.Clear();
            if (_engineReady)
                SyncTogglesFromState(KtEngine.GetState(), true);
            else
                InitToggleDefaults();
            UpdateStagedUi();
            ShowInfoBar(infoBarTweaks, infoBarTweaksMsg, "Alterações descartadas.", "Warning");
            ScheduleInfoBarHide(infoBarTweaks, 3000);
        }

        private void btnReviewStaged_Click(object sender, RoutedEventArgs e)
        {
            if (navTweaks != null) navTweaks.IsChecked = true;
        }

        private void btnStart_Click(object sender, RoutedEventArgs e)
        {
            try
            {
                EnsureConnected();
                if (App.SafeMode)
                {
                    txtStatus.Text = "Modo seguro ativo: desmarque a opção para iniciar otimizações.";
                    return;
                }
                var state = KtEngine.GetState();
                if (state.running != 0) KtEngine.Stop();
                else KtEngine.Start();
                Tick(null, EventArgs.Empty);
            }
            catch { }
        }

        private void SafeMode_Changed(object sender, RoutedEventArgs e)
        {
            if (!IsXamlLoaded) return;
            bool enabled = chkSafeMode.IsChecked == true;
            try
            {
                EnsureConnected();
                if (enabled)
                {
                    KtEngine.Stop();
                    KtEngine.SetSafeMode(true);
                    if (!KtEngine.RecoverPendingSession())
                        throw new InvalidOperationException("Não foi possível restaurar a sessão pendente.");
                    txtStatus.Text = "Modo seguro ativo: sessão restaurada; nenhum tweak será aplicado.";
                }
                else
                {
                    KtEngine.SetSafeMode(false);
                    txtStatus.Text = "Modo seguro desativado: você pode iniciar o motor quando quiser.";
                }
                App.SetSafeMode(enabled);
                ApplySafeModeUi();
                Tick(null, EventArgs.Empty);
            }
            catch (Exception ex)
            {
                try { KtEngine.SetSafeMode(true); } catch { }
                App.SetSafeMode(true);
                _syncingToggles = true;
                chkSafeMode.IsChecked = true;
                _syncingToggles = false;
                ApplySafeModeUi();
                ReportUiError("modo seguro", ex);
            }
        }

        private void ApplySafeModeUi()
        {
            bool enabled = App.SafeMode;
            btnTopStart.IsEnabled = !enabled;
            btnTopPause.IsEnabled = !enabled;
            btnTournament.IsEnabled = !enabled;
            cmbProfile.IsEnabled = !enabled;
            chkAutoProfile.IsEnabled = !enabled;
            btnCleanRam.IsEnabled = !enabled && _engineReady;
            pageTweaks.IsEnabled = !enabled;
            btnTestTarget.IsEnabled = !enabled;
            btnBoostTarget.IsEnabled = !enabled;
            btnExcludeProc.IsEnabled = !enabled;
            btnSvcStop.IsEnabled = !enabled;
            btnSvcPause.IsEnabled = !enabled;
            btnSvcStart.IsEnabled = !enabled;
            btnGovernorApply.IsEnabled = !enabled;
            btnGovernorProfileApply.IsEnabled = !enabled;
            btnGovernorSuspend.IsEnabled = !enabled;
            btnGovernorResume.IsEnabled = !enabled;
            btnGovernorTrim.IsEnabled = !enabled;
            chkGovEco.IsEnabled = !enabled;
            cmbGovernorProfile.IsEnabled = !enabled;
            cmbGovPriority.IsEnabled = !enabled;
            cmbGovIo.IsEnabled = !enabled;
            cmbGovMemory.IsEnabled = !enabled;
        }

        private void btnPause_Click(object sender, RoutedEventArgs e)
        {
            try { EnsureConnected(); KtEngine.Pause(!_paused); } catch { }
        }

        private void btnTournament_Click(object sender, RoutedEventArgs e)
        {
            try { EnsureConnected(); KtEngine.Tournament(!_tournament); } catch { }
        }

        private void btnQuit_Click(object sender, RoutedEventArgs e)
        {
            if (System.Windows.MessageBox.Show("Encerrar o motor e sair?", "KT Game Booster", MessageBoxButton.YesNo, MessageBoxImage.Question) == MessageBoxResult.Yes)
                QuitApp();
        }

        private void btnCleanRam_Click(object sender, RoutedEventArgs e)
        {
            try
            {
                EnsureConnected();
                ulong freed = KtEngine.CleanRam();
                txtMemFreed.Text = (freed / (1024.0 * 1024)).ToString("0") + " MB";
                txtMemThisClean.Text = "última: " + (freed / (1024.0 * 1024)).ToString("0") + " MB";
            }
            catch { }
        }

        private void btnPanic_Click(object sender, RoutedEventArgs e)
        {
            try { EnsureConnected(); KtEngine.KT_RestoreShell(); } catch { }
            RestoreShellFallback();
            txtExtremeStatus.Text = "restauração de shell executada";
        }

        private void btnTestTarget_Click(object sender, RoutedEventArgs e)
        {
            string exe = (txtTestTarget.Text ?? "").Trim();
            if (exe.Length == 0)
            {
                ShowInfoBar(infoBarDashboard, infoBarDashboardMsg,
                    "Informe um .exe no campo acima ou selecione um processo na página Processos e use \"Otimizar como jogo\".",
                    "Warning");
                txtStatus.Text = "teste de jogo: caminho vazio";
                return;
            }
            if (App.SafeMode)
            {
                txtStatus.Text = "Modo seguro ativo: otimizações bloqueadas.";
                return;
            }
            try
            {
                EnsureConnected();
                if (!_engineReady) return;
                KtEngine.TestTarget(exe);
                txtStatus.Text = "otimizando como jogo: " + exe;
            }
            catch (Exception ex) { ReportUiError("teste", ex); }
        }

        private void btnAddGame_Click(object sender, RoutedEventArgs e)
        {
            var name = txtAddGame.Text?.Trim();
            if (string.IsNullOrEmpty(name)) return;
            try
            {
                EnsureConnected();
                int r = KtEngine.AddGame(name);
                txtAddGame.Text = "";
                if (r == 1)
                {
                    LoadGameList();
                    txtStatus.Text = "jogo adicionado: " + Path.GetFileName(name).ToLowerInvariant();
                }
                else txtStatus.Text = "jogo já existe / inválido";
            }
            catch { }
        }

        private void Toggle_Changed(object sender, RoutedEventArgs e)
        {
            if (!IsXamlLoaded) return;
            if (_syncingToggles) return;
            if (App.SafeMode)
            {
                txtStatus.Text = "Modo seguro ativo: tweaks estão bloqueados.";
                return;
            }
            var cb = sender as System.Windows.Controls.CheckBox;
            if (cb == null || cb.Tag == null) return;
            try
            {
                string tag = cb.Tag.ToString();
                if (IsInPageTweaks(cb))
                {
                    bool requested = cb.IsChecked == true;
                    if (TryGetCurrentFeatureValue(tag, out bool current) && current == requested)
                        _stagedTweaks.Remove(tag);
                    else
                        _stagedTweaks[tag] = requested;
                    UpdateStagedUi();
                    txtStatus.Text = _stagedTweaks.ContainsKey(tag)
                        ? "alteração pendente: " + (cb.Content ?? tag)
                        : "alteração revertida ao valor atual: " + (cb.Content ?? tag);
                    return;
                }

                EnsureConnected();
                if (!_engineReady) return;
                int result = KtEngine.SetFeature(tag, cb.IsChecked == true);
                if (result != 1)
                {
                    _syncingToggles = true;
                    cb.IsChecked = cb.IsChecked != true;
                    _syncingToggles = false;
                    txtStatus.Text = "tweak não reconhecido: " + tag;
                    return;
                }
                UiPreferenceStore.SaveFeatureOverride(SelectedProfileId, tag, cb.IsChecked == true);
                _activeProfile = "personalizado";
                _lastFeatureMask = int.MinValue;
                SyncTogglesFromState(KtEngine.GetState(), true);
                txtStatus.Text = "tweak atualizado: " + (cb.Content ?? tag);
            }
            catch (Exception ex) { ReportUiError("tweak", ex); }
        }

        private void cmbProfile_Changed(object sender, SelectionChangedEventArgs e)
        {
            if (!IsXamlLoaded) return;
            var cbi = (sender as System.Windows.Controls.ComboBox)?.SelectedItem as ComboBoxItem;
            if (cbi == null || cbi.Tag == null) return;
            var combo = sender as System.Windows.Controls.ComboBox;
            string previousProfile = combo?.Tag as string;
            string requestedProfile = cbi.Tag.ToString();
            if (string.Equals(previousProfile, requestedProfile, StringComparison.OrdinalIgnoreCase)) return;
            if (_stagedTweaks.Count > 0)
            {
                if (combo != null && !string.IsNullOrEmpty(previousProfile))
                    combo.SelectedItem = combo.Items.Cast<ComboBoxItem>().FirstOrDefault(i =>
                        string.Equals(i.Tag as string, previousProfile, StringComparison.OrdinalIgnoreCase));
                ShowInfoBar(infoBarTweaks, infoBarTweaksMsg,
                    "Aplique ou descarte as alterações pendentes antes de trocar de perfil.", "Warning");
                return;
            }
            try
            {
                EnsureConnected();
                KtEngine.LoadProfile(cbi.Tag.ToString());
                bool customized = ApplyStoredFeatureOverrides(cbi.Tag.ToString());
                ((System.Windows.Controls.ComboBox)sender).Tag = cbi.Tag.ToString();
                UiPreferenceStore.SaveActiveProfile(cbi.Tag.ToString());
                _activeProfile = customized ? "personalizado" : ProfileLabel(cbi.Tag.ToString());
                _lastFeatureMask = int.MinValue;
                SyncTogglesFromState(KtEngine.GetState(), true);
                txtStatus.Text = "perfil carregado: " + _activeProfile;
            }
            catch (Exception ex) { ReportUiError("perfil", ex); }
        }

        private void SyncTogglesFromState(KtState st, bool force)
        {
            if (!force && _lastFeatureMask == st.featureMask && _lastFeatureMask2 == st.featureMask2)
            {
                _lastTournament = st.tournament != 0;
                return;
            }
            _lastFeatureMask = st.featureMask;
            _lastFeatureMask2 = st.featureMask2;
            _lastTournament = st.tournament != 0;
            _syncingToggles = true;
            try
            {
                foreach (var cb in FindAllToggles())
                {
                    if (cb.Tag == null) continue;
                    string tag = cb.Tag.ToString();
                    if (_stagedTweaks.TryGetValue(tag, out bool stagedValue))
                    {
                        cb.IsChecked = stagedValue;
                        continue;
                    }
                    if (TagToFeature.TryGetValue(tag, out int bit))
                        cb.IsChecked = (st.featureMask & bit) != 0;
                }
                var dvr = FindToggleByTag("game_dvr_disable");
                if (dvr != null)
                    dvr.IsChecked = _stagedTweaks.TryGetValue("game_dvr_disable", out bool stagedDvr)
                        ? stagedDvr : (st.featureMask2 & KtFeature2.GameDvrDisable) != 0;
                var acSafe = FindToggleByTag("anticheat_safe_mode");
                if (acSafe != null)
                    acSafe.IsChecked = _stagedTweaks.TryGetValue("anticheat_safe_mode", out bool stagedAc)
                        ? stagedAc : (st.featureMask2 & KtFeature2.AntiCheatSafeMode) != 0;
                var svcAuto = FindToggleByTag("pause_services_gaming");
                if (svcAuto != null)
                    svcAuto.IsChecked = _stagedTweaks.TryGetValue("pause_services_gaming", out bool stagedSvc)
                        ? stagedSvc : (st.featureMask & KtFeature.ServicePause) != 0;
            }
            finally { _syncingToggles = false; }
        }

        private bool TryGetCurrentFeatureValue(string tag, out bool enabled)
        {
            enabled = false;
            if (string.Equals(tag, "tournament_mode", StringComparison.OrdinalIgnoreCase) && _lastTournament.HasValue)
            {
                enabled = _lastTournament.Value;
                return true;
            }
            if (TagToFeature.TryGetValue(tag, out int bit))
            {
                if (_lastFeatureMask == int.MinValue) return false;
                enabled = (_lastFeatureMask & bit) != 0;
                return true;
            }
            if (string.Equals(tag, "game_dvr_disable", StringComparison.OrdinalIgnoreCase))
            {
                if (_lastFeatureMask2 == int.MinValue) return false;
                enabled = (_lastFeatureMask2 & KtFeature2.GameDvrDisable) != 0;
                return true;
            }
            if (string.Equals(tag, "anticheat_safe_mode", StringComparison.OrdinalIgnoreCase))
            {
                if (_lastFeatureMask2 == int.MinValue) return false;
                enabled = (_lastFeatureMask2 & KtFeature2.AntiCheatSafeMode) != 0;
                return true;
            }
            return false;
        }

        private void LoadProfilesIntoCombo()
        {
            try
            {
                var profiles = new System.Collections.Generic.List<string> { "normal" };
                try
                {
                    var list = KtEngine.ListProfiles();
                    if (list != null)
                        foreach (var profile in list)
                            if (!profiles.Contains(profile, StringComparer.OrdinalIgnoreCase)) profiles.Add(profile);
                }
                catch { }
                cmbProfile.Items.Clear();
                foreach (var p in profiles)
                {
                    string label = p.Equals("tournament", StringComparison.OrdinalIgnoreCase) ? "FPS Máximo"
                                 : p.Equals("bg_only", StringComparison.OrdinalIgnoreCase) ? "Foco em segundo plano"
                                 : "Equilibrado";
                    var it = new ComboBoxItem { Content = label, Tag = p };
                    cmbProfile.Items.Add(it);
                }
                string preferred = UiPreferenceStore.LoadActiveProfile();
                if (!profiles.Contains(preferred, StringComparer.OrdinalIgnoreCase))
                    preferred = "normal";
                var selected = cmbProfile.Items.Cast<ComboBoxItem>().FirstOrDefault(i =>
                    string.Equals(i.Tag as string, preferred, StringComparison.OrdinalIgnoreCase))
                    ?? cmbProfile.Items.Cast<ComboBoxItem>().FirstOrDefault();
                if (selected == null) return;

                preferred = selected.Tag?.ToString() ?? "normal";
                cmbProfile.Tag = preferred;
                cmbProfile.SelectedItem = selected;
                _activeProfile = ProfileLabel(preferred);
                if (_engineReady)
                {
                    if (!preferred.Equals("normal", StringComparison.OrdinalIgnoreCase))
                        KtEngine.LoadProfile(preferred);
                    if (ApplyStoredFeatureOverrides(preferred))
                        _activeProfile = "personalizado";
                }
                if (_engineReady) UiPreferenceStore.SaveActiveProfile(preferred);
            }
            catch { }
        }

        private string SelectedProfileId
        {
            get { return cmbProfile?.Tag as string ?? "normal"; }
        }

        private static string ProfileLabel(string profile)
        {
            return string.Equals(profile, "tournament", StringComparison.OrdinalIgnoreCase) ? "fps máximo"
                : string.Equals(profile, "bg_only", StringComparison.OrdinalIgnoreCase) ? "foco em segundo plano"
                : "equilibrado";
        }

        private bool ApplyStoredFeatureOverrides(string profile)
        {
            var preferenceNames = TagToFeature.Keys.Concat(new[] { "game_dvr_disable" });
            var preferences = UiPreferenceStore.LoadFeatureOverrides(profile, preferenceNames);
            bool appliedAny = false;
            foreach (var preference in preferences)
                if (KtEngine.SetFeature(preference.Key, preference.Value) == 1)
                    appliedAny = true;
            return appliedAny;
        }

        private void InitToggleDefaults()
        {
            string[] on = { "trim_background_ws", "background_limiter",
                            "turbo_boost", "gpu_priority", "core_parking_disable",
                            "dwm_mmcss", "eco_qos_background", "io_priority_background",
                            "high_performance_plan",
                            "thread_power_game_unthrottle", "game_tree_boost", "dwm_thread_priority",
                            "thread_power_background_throttle", "io_priority_game_high",
                            "mmcss_game", "page_priority_game",
                            "game_dvr_disable" };
            var set = new System.Collections.Generic.HashSet<string>(on);
            _syncingToggles = true;
            try
            {
                foreach (var cb in FindAllToggles())
                    if (cb.Tag != null) cb.IsChecked = set.Contains(cb.Tag.ToString());
            }
            finally { _syncingToggles = false; }
        }

        private System.Collections.Generic.IEnumerable<System.Windows.Controls.CheckBox> FindAllToggles()
        {
            if (chkAutoProfile != null) yield return chkAutoProfile;
            foreach (var cb in pageTweaks.FindLogicalChildren<System.Windows.Controls.CheckBox>())
                yield return cb;
        }

        private void txtTweakFilter_TextChanged(object sender, System.Windows.Controls.TextChangedEventArgs e)
        {
            if (!IsXamlLoaded) return;
            ApplyTweakFilter();
        }

        private void ApplyTweakFilter()
        {
            string f = (txtTweakFilter.Text ?? "").Trim().ToLowerInvariant();
            var tweakCardStyle = TryFindResource("TweakCard") as Style;
            foreach (var card in pageTweaks.FindLogicalChildren<Border>())
            {
                if (card.Style != tweakCardStyle) continue;
                bool matches = f.Length == 0 || card.FindLogicalChildren<System.Windows.Controls.CheckBox>()
                    .Any(cb => (cb.Content?.ToString() ?? "").ToLowerInvariant().Contains(f)
                            || (cb.Tag?.ToString() ?? "").ToLowerInvariant().Contains(f));
                card.Visibility = matches ? Visibility.Visible : Visibility.Collapsed;
            }
        }

        private void btnTweakAll_Click(object sender, RoutedEventArgs e)
        {
            foreach (var cb in pageTweaks.FindLogicalChildren<System.Windows.Controls.CheckBox>())
            {
                string tag = cb.Tag?.ToString() ?? "";
                if (!tag.StartsWith("extreme_", StringComparison.OrdinalIgnoreCase))
                    cb.IsChecked = true;
            }
            txtStatus.Text = "Ajustes não extremos selecionados. Revise o escopo e reinícios antes de aplicar.";
        }

        private void btnTweakNone_Click(object sender, RoutedEventArgs e)
        {
            foreach (var cb in pageTweaks.FindLogicalChildren<System.Windows.Controls.CheckBox>())
                cb.IsChecked = false;
        }

        private void ApplyProcFilter()
        {
            string f = (txtProcFilter.Text ?? "").Trim().ToLowerInvariant();
            var rows = new System.Collections.Generic.List<ProcRow>();
            foreach (var p in _lastSysProcs)
            {
                string name = p.name ?? "";
                if (f.Length > 0 && name.ToLowerInvariant().IndexOf(f, StringComparison.Ordinal) < 0)
                    continue;
                string state = p.isGame != 0 ? "JOGO"
                             : p.gameChild != 0 ? "helper do jogo"
                             : p.throttled != 0 ? "limitado" : "-";
                rows.Add(new ProcRow
                {
                    pid = p.pid.ToString(),
                    name = name,
                    klass = p.klass,
                    stateStr = state,
                    cpuStr = p.cpuPct <= 0 ? "" : p.cpuPct.ToString("0.0") + "%",
                    ramStr = p.ramMB > 0 ? p.ramMB + " MB" : "",
                    pidNum = p.pid,
                    cpuPct = p.cpuPct,
                    ramMB = p.ramMB,
                    rawName = name,
                    icon = GetCachedProcessIcon(p.pid, name),
                    creationTime = p.creationTime
                });
                RequestProcessIcon(p.pid, name);
            }
            lstProcs.ItemsSource = rows;
            txtProcCount.Text = "(" + rows.Count + " processos)";
            ApplyProcSort();
        }

        private string _procSortProp = null;
        private bool _procDesc = false;

        private void ApplyProcSort()
        {
            if (_procSortProp == null || lstProcs == null || lstProcs.ItemsSource == null) return;
            var view = System.Windows.Data.CollectionViewSource.GetDefaultView(lstProcs.ItemsSource);
            if (view == null) return;
            using (view.DeferRefresh())
            {
                view.SortDescriptions.Clear();
                view.SortDescriptions.Add(new System.ComponentModel.SortDescription(_procSortProp,
                    _procDesc ? System.ComponentModel.ListSortDirection.Descending
                              : System.ComponentModel.ListSortDirection.Ascending));
            }
        }

        private void UpdateProcHeaderArrows()
        {
            var gv = lstProcs?.View as System.Windows.Controls.GridView;
            if (gv == null) return;
            foreach (var col in gv.Columns)
            {
                var h = col.Header as System.Windows.Controls.GridViewColumnHeader;
                if (h == null || !(h.Content is string s)) continue;
                if (s.EndsWith(" ▼") || s.EndsWith(" ▲"))
                    h.Content = s.Substring(0, s.Length - 2);
            }
            if (_procSortProp == null) return;
            foreach (var col in gv.Columns)
            {
                var h = col.Header as System.Windows.Controls.GridViewColumnHeader;
                string tag = h?.Tag as string;
                if (tag == _procSortProp && h.Content is string stem)
                    h.Content = stem + (_procDesc ? " ▼" : " ▲");
            }
        }

        private void ProcCol_Click(object sender, System.Windows.RoutedEventArgs e)
        {
            if (!IsXamlLoaded) return;
            var header = sender as System.Windows.Controls.GridViewColumnHeader;
            if (header == null || header.Column == null) return;
            string tag = header.Tag as string;
            if (string.IsNullOrEmpty(tag)) return;
            if (_procSortProp == tag) _procDesc = !_procDesc;
            else { _procSortProp = tag; _procDesc = false; }
            RefreshSorting();
        }

        private void RefreshSorting()
        {
            UpdateProcHeaderArrows();
            ApplyProcSort();
        }

        private void txtProcFilter_TextChanged(object sender, System.Windows.Controls.TextChangedEventArgs e)
        {
            if (!IsXamlLoaded) return;
            ApplyProcFilter();
        }

        private void btnBoostTarget_Click(object sender, RoutedEventArgs e)
        {
            var sel = lstProcs.SelectedItem as ProcRow;
            if (sel == null || string.IsNullOrEmpty(sel.rawName))
            {
                ShowInfoBar(infoBarDashboard, infoBarDashboardMsg,
                    "Selecione um processo na tabela antes de aplicar o perfil de jogo.",
                    "Warning");
                return;
            }
            if (App.SafeMode)
            {
                txtStatus.Text = "Modo seguro ativo: otimizações bloqueadas.";
                return;
            }
            try
            {
                EnsureConnected();
                if (!_engineReady) return;
                KtEngine.TestTarget(sel.rawName);
                txtStatus.Text = "otimizando como jogo: " + sel.rawName;
            }
            catch (Exception ex) { ReportUiError("teste", ex); }
        }

        private void btnExcludeProc_Click(object sender, RoutedEventArgs e)
        {
            var sel = lstProcs.SelectedItem as ProcRow;
            if (sel == null || string.IsNullOrEmpty(sel.rawName)) return;
            try
            {
                EnsureConnected();
                if (KtEngine.AddExclusion(sel.rawName) == 1)
                {
                    LoadExclusions();
                    txtStatus.Text = "excluído do limitador: " + sel.rawName;
                }
            }
            catch { }
        }

        private void ApplyGovernorFilter()
        {
            if (lstGovernor == null) return;
            uint selectedPid = (lstGovernor.SelectedItem as ProcRow)?.pidNum ?? 0;
            string f = (txtGovernorFilter?.Text ?? "").Trim().ToLowerInvariant();
            var rows = new List<ProcRow>();
            foreach (var p in _lastSysProcs)
            {
                string name = p.name ?? "";
                if (f.Length > 0 && name.ToLowerInvariant().IndexOf(f, StringComparison.Ordinal) < 0) continue;
                rows.Add(new ProcRow
                {
                    pid = p.pid.ToString(), pidNum = p.pid, name = name, rawName = name,
                    icon = GetCachedProcessIcon(p.pid, name),
                    creationTime = p.creationTime,
                    klass = p.klass, cpuPct = p.cpuPct, ramMB = p.ramMB,
                    cpuStr = p.cpuPct.ToString("0.0") + "%", ramStr = p.ramMB + " MB",
                    affinityHex = "0x" + p.affinity.ToString("X"),
                    stateStr = p.isGame != 0 ? "JOGO" : p.gameChild != 0 ? "helper" : p.throttled != 0 ? "limitado" : "normal",
                    stateBrush = p.isGame != 0 ? BrushSuccess : p.gameChild != 0 ? BrushAccent3 : p.throttled != 0 ? BrushWarn : BrushAccent
                });
                RequestProcessIcon(p.pid, name);
            }
            rows = rows.OrderByDescending(x => x.stateStr == "JOGO")
                       .ThenByDescending(x => x.cpuPct)
                       .ToList();
            lstGovernor.ItemsSource = rows;
            if (txtGovCount != null)
                txtGovCount.Text = rows.Count + (rows.Count == 1 ? " processo visível" : " processos visíveis");
            if (selectedPid != 0)
                lstGovernor.SelectedItem = rows.FirstOrDefault(x => x.pidNum == selectedPid);
            else
                lstGovernor.SelectedItem = rows.FirstOrDefault(x => x.stateStr == "JOGO");
        }

        private ProcRow SelectedGovernorProcess()
        {
            var row = lstGovernor?.SelectedItem as ProcRow;
            if (row == null) txtStatus.Text = "Governor: selecione um processo";
            return row;
        }

        private int Govern(ProcRow row, int action, ulong value)
        {
            if (row == null) return 0;
            if (App.SafeMode)
            {
                txtStatus.Text = "Governor: modo seguro bloqueia ações manuais.";
                return -2;
            }
            EnsureConnected();
            if (row.creationTime == 0)
            {
                txtStatus.Text = "identidade do processo ainda não verificada; tente novamente em instantes";
                return -3;
            }
            int result = KtEngine.GovernProcess(row.pidNum, row.creationTime, action, value);
            txtStatus.Text = result == 1 ? "Governor aplicado: " + row.name
                           : result == -1 ? "Governor: processo crítico protegido"
                           : result == -3 ? "Governor: processo mudou desde a seleção; selecione-o novamente"
                           : "Governor: ação não suportada ou acesso negado";
            return result;
        }

        private static string ProcessIconKey(uint pid, string name)
        {
            return pid.ToString() + "|" + Path.GetFileNameWithoutExtension(name ?? "").ToLowerInvariant();
        }

        private static ImageSource GetCachedProcessIcon(uint pid, string name)
        {
            ImageSource icon;
            return ProcessIconCache.TryGetValue(ProcessIconKey(pid, name), out icon) ? icon : null;
        }

        private void RequestProcessIcon(uint pid, string processName)
        {
            string key = ProcessIconKey(pid, processName);
            string expectedName = Path.GetFileNameWithoutExtension(processName ?? "");
            if (pid == 0 || ProcessIconCache.ContainsKey(key)) return;
            lock (ProcessIconLock)
            {
                if (ProcessIconRequests.Count >= 128) return;
                if (!ProcessIconRequests.Add(key)) return;
            }

            Task.Run(() =>
            {
                ImageSource image = null;
                try
                {
                    using (var process = Process.GetProcessById((int)pid))
                    {
                        if (!string.Equals(process.ProcessName, expectedName, StringComparison.OrdinalIgnoreCase))
                            return;
                        string path = process.MainModule?.FileName;
                        if (!string.IsNullOrEmpty(path) && File.Exists(path))
                        {
                            using (var icon = SysDrawing.Icon.ExtractAssociatedIcon(path))
                            {
                                if (icon != null)
                                {
                                    image = Imaging.CreateBitmapSourceFromHIcon(icon.Handle,
                                        Int32Rect.Empty, System.Windows.Media.Imaging.BitmapSizeOptions.FromWidthAndHeight(32, 32));
                                    image.Freeze();
                                }
                            }
                        }
                    }
                }
                catch {   }
                finally
                {
                    lock (ProcessIconLock) ProcessIconRequests.Remove(key);
                }

                if (image == null) return;
                lock (ProcessIconLock)
                {
                    if (ProcessIconCache.TryAdd(key, image))
                    {
                        ProcessIconCacheOrder.Enqueue(key);
                        while (ProcessIconCacheOrder.Count > 512)
                            ProcessIconCache.TryRemove(ProcessIconCacheOrder.Dequeue(), out _);
                    }
                }
                try
                {
                    Dispatcher.BeginInvoke(new Action(() =>
                    {
                        foreach (var row in (lstProcs.ItemsSource as IEnumerable<ProcRow>) ?? Enumerable.Empty<ProcRow>())
                            if (row.pidNum == pid && string.Equals(row.name, processName, StringComparison.OrdinalIgnoreCase))
                                row.icon = image;
                        foreach (var row in (lstGovernor.ItemsSource as IEnumerable<ProcRow>) ?? Enumerable.Empty<ProcRow>())
                            if (row.pidNum == pid && string.Equals(row.name, processName, StringComparison.OrdinalIgnoreCase))
                                row.icon = image;
                    }));
                }
                catch {   }
            });
        }

        private void txtGovernorFilter_TextChanged(object sender, TextChangedEventArgs e)
        {
            if (!IsXamlLoaded) return;
            ApplyGovernorFilter();
        }

        private void lstGovernor_SelectionChanged(object sender, SelectionChangedEventArgs e)
        {
            var row = lstGovernor?.SelectedItem as ProcRow;
            txtGovSelected.Text = row?.name ?? "nenhum";
            txtGovCpu.Text = row == null ? "—" : row.cpuStr;
            txtGovRam.Text = row == null ? "—" : row.ramStr;
            txtGovAffinity.Text = row == null ? "—" : row.affinityHex;
        }

        private void btnGovernorProfile_Click(object sender, RoutedEventArgs e)
        {
            var row = SelectedGovernorProcess();
            if (row == null) return;
            int profile = cmbGovernorProfile.SelectedIndex;
            ulong[] values = profile == 0
                ? new ulong[] { 1, 1, 1, 0, 0 }
                : profile == 1
                    ? new ulong[] { 2, 0, 2, 1, 1 }
                    : new ulong[] { 4, 0, 3, 2, 1 };
            int[] actions = { 1, 3, 4, 5, 8 };
            int applied = 0;
            for (int i = 0; i < actions.Length; i++)
                if (Govern(row, actions[i], values[i]) == 1) applied++;

            txtStatus.Text = applied == actions.Length
                ? "Governor: perfil aplicado (5/5 ajustes) em " + row.name
                : "Governor: aplicação parcial (" + applied + "/5 ajustes); revise as permissões do processo " + row.name;
        }

        private void btnGovernorAdvanced_Click(object sender, RoutedEventArgs e)
        {
            var row = SelectedGovernorProcess();
            if (row == null) return;
            int ok = 0;
            ok += Govern(row, 1, (ulong)cmbGovPriority.SelectedIndex) == 1 ? 1 : 0;
            ok += Govern(row, 3, chkGovEco.IsChecked == true ? 1UL : 0UL) == 1 ? 1 : 0;
            ok += Govern(row, 4, (ulong)cmbGovIo.SelectedIndex) == 1 ? 1 : 0;
            ok += Govern(row, 5, (ulong)cmbGovMemory.SelectedIndex) == 1 ? 1 : 0;
            txtStatus.Text = "Governor: " + ok + "/4 ajustes aplicados em " + row.name;
        }

        private void btnGovernorSuspend_Click(object sender, RoutedEventArgs e)
        {
            var row = SelectedGovernorProcess();
            if (row == null) return;
            if (System.Windows.MessageBox.Show(
                    "Suspender \"" + row.name + "\" (PID " + row.pidNum + ")?\n\nO processo fica congelado até você clicar Retomar ou ele encerrar. " +
                    "Evite para algo em uso (downloads, chamadas de voz, programas que gravam dados).",
                    "Governor · suspender processo",
                    MessageBoxButton.YesNo, MessageBoxImage.Warning) != MessageBoxResult.Yes)
                return;
            if (Govern(row, 6, 1) == 1) txtStatus.Text = "Governor: suspenso " + row.name;
        }

        private void btnGovernorResume_Click(object sender, RoutedEventArgs e)
        {
            var row = SelectedGovernorProcess();
            if (row != null) Govern(row, 6, 0);
        }

        private void btnGovernorTrim_Click(object sender, RoutedEventArgs e)
        {
            var row = SelectedGovernorProcess();
            if (row != null) Govern(row, 7, 0);
        }

        private void LoadServices()
        {
            try
            {
                EnsureConnected();
                if (!_engineReady) return;
                _lastServices = KtEngine.GetServices();
                ApplySvcFilter();
            }
            catch (Exception ex) { ReportUiError("serviços", ex); }
        }

        private static string ServiceStateName(uint state)
        {
            switch (state)
            {
                case 1: return "Parado";
                case 2: return "Iniciando";
                case 3: return "Parando";
                case 4: return "Rodando";
                case 5: return "Continuando";
                case 6: return "Pausando";
                case 7: return "Pausado";
                default: return "Desconhecido";
            }
        }

        private void ApplySvcFilter()
        {
            string f = (txtSvcFilter.Text ?? "").Trim().ToLowerInvariant();
            int filterMode = cmbSvcState != null ? cmbSvcState.SelectedIndex : 0;
            var rows = new System.Collections.Generic.List<SvcRow>();
            foreach (var s in _lastServices.OrderByDescending(x => x.gamingTarget).ThenBy(x => x.displayName ?? x.name))
            {
                if (filterMode == 0 && s.gamingTarget == 0) continue;
                if (filterMode == 1 && s.state != 4 && s.state != 7) continue;
                string nm = s.name ?? "", dn = s.displayName ?? "";
                if (f.Length > 0 && nm.ToLowerInvariant().IndexOf(f, StringComparison.Ordinal) < 0
                    && dn.ToLowerInvariant().IndexOf(f, StringComparison.Ordinal) < 0) continue;
                bool target = s.gamingTarget != 0;
                bool protectedService = s.protectedService != 0;
                string plan;
                if (protectedService) plan = "PROTEGIDO";
                else if (!target) plan = "MANUAL";
                else if (s.state == 1) plan = "JÁ PARADO";
                else if (s.state == 7) plan = "JÁ PAUSADO";
                else if (s.canPause != 0) plan = "PAUSAR";
                else if (s.canStop != 0) plan = "PARAR";
                else plan = "SEM CONTROLE";
                rows.Add(new SvcRow
                {
                    name = nm,
                    display = dn,
                    planStr = plan,
                    planBrush = protectedService ? BrushDanger :
                                (target ? (plan == "PAUSAR" || plan == "PARAR" ? BrushAccent3 : BrushWarn) : BrushAccent),
                    reasonStr = target ? GetServiceReason(nm) :
                                (protectedService ? "serviço crítico bloqueado" : "somente leitura · fora da lista segura"),
                    stateStr = ServiceStateName(s.state),
                    stateBrush = s.state == 7 ? BrushWarn : (s.state == 4 ? BrushSuccess : (s.state == 1 ? BrushDanger : BrushAccent)),
                    pid = s.pid != 0 ? s.pid.ToString() : "-",
                    rawName = nm,
                    state = s.state,
                    canStop = s.canStop != 0,
                    canPause = s.canPause != 0,
                    isTarget = target,
                    isProtected = protectedService
                });
            }
            lstServices.ItemsSource = rows;
            int targetCount = _lastServices.Count(s => s.gamingTarget != 0);
            int activeTargets = _lastServices.Count(s => s.gamingTarget != 0 && s.state == 4);
            int actionableTargets = _lastServices.Count(s => s.gamingTarget != 0 && s.state == 4 && (s.canPause != 0 || s.canStop != 0));
            txtSvcCount.Text = rows.Count + " exibidos · " + _lastServices.Length + " serviços detectados";
            txtSvcPlanSummary.Text = targetCount + " alvos encontrados · " + activeTargets +
                " rodando · " + actionableTargets + (actionableTargets == 1 ? " ação disponível" : " ações disponíveis") +
                " · restauração automática no fim do jogo";
            UpdateSvcButtons();
        }

        private static string GetServiceReason(string name)
        {
            if (ServiceReasons.TryGetValue(name ?? "", out var exact)) return exact;
            foreach (var entry in ServiceReasons)
                if ((name ?? "").StartsWith(entry.Key + "_", StringComparison.OrdinalIgnoreCase))
                    return entry.Value;
            return "serviço opcional pausado somente durante o jogo";
        }

        private void lstServices_SelectionChanged(object sender, SelectionChangedEventArgs e)
        {
            if (!IsXamlLoaded) return;
            UpdateSvcButtons();
        }

        private void UpdateSvcButtons()
        {
            var sel = lstServices != null ? lstServices.SelectedItem as SvcRow : null;
            if (App.SafeMode)
            {
                btnSvcStop.IsEnabled = false;
                btnSvcStart.IsEnabled = false;
                btnSvcPause.IsEnabled = false;
                return;
            }
            bool safe = sel != null && !sel.isProtected && sel.isTarget && _engineReady;
            btnSvcStop.IsEnabled = safe && sel.canStop && sel.state != 1;
            btnSvcStart.IsEnabled = safe && sel.state == 1;
            btnSvcPause.IsEnabled = safe && ((sel.state == 4 && sel.canPause) || sel.state == 7);
            if (sel != null && sel.state == 7) btnSvcPause.Content = "Retomar";
            else btnSvcPause.Content = "Pausar";
        }

        private void txtSvcFilter_TextChanged(object sender, System.Windows.Controls.TextChangedEventArgs e)
        {
            if (!IsXamlLoaded) return;
            ApplySvcFilter();
        }

        private void cmbSvcState_Changed(object sender, SelectionChangedEventArgs e)
        {
            if (!IsXamlLoaded) return;
            ApplySvcFilter();
        }

        private bool _svcActionRunning;

        private async void RunServiceAction(Action control, string statusOk)
        {
            if (_svcActionRunning) return;
            _svcActionRunning = true;
            btnSvcStop.IsEnabled = false;
            btnSvcPause.IsEnabled = false;
            btnSvcStart.IsEnabled = false;
            txtStatus.Text = "aplicando ação de serviço...";
            try
            {
                await System.Threading.Tasks.Task.Run(control);
                await System.Threading.Tasks.Task.Delay(400);
                LoadServices();
                txtStatus.Text = statusOk;
            }
            catch (Exception ex)
            {
                ReportUiError("serviço", ex);
            }
            finally { _svcActionRunning = false; }
        }

        private void btnSvcStop_Click(object sender, RoutedEventArgs e)
        {
            var sel = lstServices.SelectedItem as SvcRow;
            if (sel == null || sel.isProtected || !sel.isTarget || !sel.canStop) return;
            if (System.Windows.MessageBox.Show(
                    "Parar o serviço '" + sel.display + "'?\n\nO modo automático restaura apenas serviços alterados por ele; esta ação manual não será revertida automaticamente.",
                    "Confirmar parada de serviço", MessageBoxButton.YesNo, MessageBoxImage.Warning) != MessageBoxResult.Yes)
                return;
            try { EnsureConnected(); if (!_engineReady) return; } catch { return; }
            RunServiceAction(() => KtEngine.ServiceControl(sel.rawName, 2), "serviço parado: " + sel.display);
        }

        private void btnSvcPause_Click(object sender, RoutedEventArgs e)
        {
            var sel = lstServices.SelectedItem as SvcRow;
            if (sel == null || sel.isProtected || !sel.isTarget) return;
            int action = sel.state == 7 ? 4 : 3;
            if (action == 3 && !sel.canPause) return;
            try { EnsureConnected(); if (!_engineReady) return; } catch { return; }
            bool resume = action == 4;
            RunServiceAction(() => KtEngine.ServiceControl(sel.rawName, action),
                (resume ? "serviço retomado: " : "serviço pausado: ") + sel.display);
        }

        private void btnSvcStart_Click(object sender, RoutedEventArgs e)
        {
            var sel = lstServices.SelectedItem as SvcRow;
            if (sel == null || sel.isProtected || !sel.isTarget || sel.state != 1) return;
            try { EnsureConnected(); if (!_engineReady) return; } catch { return; }
            RunServiceAction(() => KtEngine.ServiceControl(sel.rawName, 1), "serviço iniciado: " + sel.display);
        }

        private void btnRunBench_Click(object sender, RoutedEventArgs e)
        {
            RunBench();
        }

        private bool _benchRunning;

        private void RunBench()
        {
            if (_benchRunning) return;
            try
            {
                EnsureConnected();
                if (!_engineReady) { txtBenchStatus.Text = "motor indisponível: rode o bench quando a sessão estiver ativa"; return; }
                uint durMs = 1500;
                if (cmbBenchDur != null)
                {
                    switch (cmbBenchDur.SelectedIndex)
                    {
                        case 0: durMs = 1000; break;
                        case 1: durMs = 1500; break;
                        case 2: durMs = 3000; break;
                    }
                }
                txtBenchScore.Text = "·";
                txtBenchScoreSub.Text = "composto 0-100";
                txtBenchScore.Foreground = null;
                txtBenchTimer.Text = "·";
                txtBenchTimerSub.Text = "resolução real do timer";
                txtBenchIo.Text = "·";
                txtBenchStatus.Text = "rodando bench (" + durMs + " ms)...";
                btnRunBench.IsEnabled = false;
                _benchRunning = true;
                var worker = new System.ComponentModel.BackgroundWorker();
                worker.DoWork += (s, ev) => {
                    var v = new BenchV2();
                    try { v.eng = KtEngine.RunBench(durMs); } catch { v.eng = new KtBenchResult(); }
                    try { v.timerCur = QueryTimerCur(); v.timerOk = v.timerCur > 0; } catch { v.timerOk = false; }
                    try { v.io = RunUnbufferedIoBench(48); v.ioOk = true; } catch { v.ioOk = false; }
                    ev.Result = v;
                };
                worker.RunWorkerCompleted += (s, ev) => {
                    btnRunBench.IsEnabled = true;
                    _benchRunning = false;
                    try
                    {
                        var v = (BenchV2)ev.Result;
                        var r = v.eng;
                        if (r.valid == 0) { txtBenchStatus.Text = "falha"; return; }

                        double p95Ms = r.latencyUsP95 / 1000.0, maxMs = r.latencyUsMax / 1000.0;
                        double avgMs = r.latencyUsAvg / 1000.0, p50Ms = r.latencyUsP50 / 1000.0;
                        txtBenchP95.Text = p95Ms.ToString("0.00") + " ms";
                        txtBenchMax.Text = maxMs.ToString("0.00") + " ms";
                        if (v.timerOk)
                            txtBenchTimer.Text = (v.timerCur / 10000.0).ToString("0.000") + " ms";
                        else { txtBenchTimer.Text = "—"; txtBenchTimerSub.Text = "indisponível"; }
                        if (v.ioOk && v.io > 0)
                            txtBenchIo.Text = v.io.ToString("0") + " µs";
                        else
                            txtBenchIo.Text = "—";

                        double t = v.timerOk ? ScoreCurve(v.timerCur / 10.0, TimerCurve) : 0;
                        double sch = ScoreCurve(r.latencyUsP95, SchedCurve);
                        double sc = 0;
                        if (v.ioOk && v.io > 0) sc = ScoreCurve(v.io, IoCurve);
                        double num = (v.timerOk ? 0.35 * t : 0) + 0.35 * sch + (v.ioOk && v.io > 0 ? 0.30 * sc : 0);
                        double den = (v.timerOk ? 0.35 : 0) + 0.35 + (v.ioOk && v.io > 0 ? 0.30 : 0);
                        double score = den > 0 ? num / den : 0;
                        score = Math.Max(0, Math.Min(100, score));
                        string cls = score >= 85 ? "excelente" : score >= 70 ? "bom" : score >= 50 ? "média" : "sensível";
                        txtBenchScore.Text = ((int)Math.Round(score)).ToString();
                        txtBenchScoreSub.Text = "composto 0-100 · " + cls;
                        txtBenchScore.Foreground = score >= 70 ? BrushSuccess : score >= 50 ? BrushWarn : BrushDanger;

                        txtBenchStatus.Text = "ok · " + r.samples + " amostras · média "
                            + avgMs.ToString("0.000") + " ms · P50 " + p50Ms.ToString("0.000") + " ms";
                        AddBenchHistory(p95Ms);
                        UpdateBenchAb(p95Ms);
                    }
                    catch { txtBenchStatus.Text = "erro"; }
                };
                worker.RunWorkerAsync();
            }
            catch { btnRunBench.IsEnabled = true; _benchRunning = false; txtBenchStatus.Text = "erro"; }
        }

        private struct CurvePt { public double x; public double y; public CurvePt(double x, double y) { this.x = x; this.y = y; } }

        private static readonly CurvePt[] TimerCurve = new CurvePt[] {
            new CurvePt(500, 100), new CurvePt(1000, 88), new CurvePt(2000, 74),
            new CurvePt(5000, 58), new CurvePt(10000, 42), new CurvePt(15625, 30),
            new CurvePt(25000, 20) };
        private static readonly CurvePt[] SchedCurve = new CurvePt[] {
            new CurvePt(50, 96), new CurvePt(100, 88), new CurvePt(250, 74),
            new CurvePt(500, 60), new CurvePt(1000, 45), new CurvePt(2000, 32),
            new CurvePt(4000, 20) };
        private static readonly CurvePt[] IoCurve = new CurvePt[] {
            new CurvePt(30, 96), new CurvePt(75, 86), new CurvePt(150, 74), new CurvePt(300, 62),
            new CurvePt(600, 48), new CurvePt(1200, 35), new CurvePt(3000, 22), new CurvePt(8000, 10) };

        private static double ScoreCurve(double v, CurvePt[] curve)
        {
            if (v <= curve[0].x) return curve[0].y;
            for (int i = 1; i < curve.Length; i++)
            {
                if (v < curve[i].x)
                {
                    double t = (v - curve[i - 1].x) / (curve[i].x - curve[i - 1].x);
                    return curve[i - 1].y + t * (curve[i].y - curve[i - 1].y);
                }
            }
            var a = curve[curve.Length - 2]; var b = curve[curve.Length - 1];
            double slope = (b.y - a.y) / (b.x - a.x);
            return Math.Max(5, b.y + (v - b.x) * slope);
        }

        private sealed class BenchV2
        {
            public KtBenchResult eng;
            public uint timerCur;
            public bool timerOk;
            public double io;
            public bool ioOk;
        }

        [System.Runtime.InteropServices.DllImport("ntdll.dll")]
        private static extern int NtQueryTimerResolution(out uint MinResolution, out uint MaxResolution, out uint CurrentResolution);

        [System.Runtime.InteropServices.DllImport("kernel32.dll", SetLastError = true, CharSet = System.Runtime.InteropServices.CharSet.Unicode)]
        private static extern Microsoft.Win32.SafeHandles.SafeFileHandle CreateFileW(string name, uint access, uint share, IntPtr security, uint creation, uint flags, IntPtr template);

        [System.Runtime.InteropServices.DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool ReadFile(Microsoft.Win32.SafeHandles.SafeFileHandle file, IntPtr buffer, uint bytes, out uint read, IntPtr overlapped);

        private const uint KT_BENCH_GENERIC_READ = 0x80000000;
        private const uint KT_BENCH_SHARE_READ = 0x00000001;
        private const uint KT_BENCH_OPEN_EXISTING = 3;
        private const uint KT_BENCH_FLAG_NO_BUFFERING = 0x20000000;

        private static uint QueryTimerCur()
        {
            uint min, max, cur;
            int st = NtQueryTimerResolution(out min, out max, out cur);
            return st == 0 ? cur : 0;
        }

        private static double RunUnbufferedIoBench(int iters)
        {
            string path = System.IO.Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "ktengine.dll");
            if (!System.IO.File.Exists(path)) throw new System.IO.FileNotFoundException("ktengine.dll ausente", path);
            using (var h = CreateFileW(path, KT_BENCH_GENERIC_READ, KT_BENCH_SHARE_READ, IntPtr.Zero, KT_BENCH_OPEN_EXISTING, KT_BENCH_FLAG_NO_BUFFERING, IntPtr.Zero))
            {
                if (h.IsInvalid) throw new System.ComponentModel.Win32Exception();
                IntPtr raw = System.Runtime.InteropServices.Marshal.AllocHGlobal(4096 * 2);
                try
                {
                    long aligned = ((raw.ToInt64() + 4095) / 4096) * 4096;
                    IntPtr buf = new IntPtr(aligned);
                    double[] us = new double[iters];
                    for (int i = 0; i < iters; i++)
                    {
                        uint read;
                        long t0 = System.Diagnostics.Stopwatch.GetTimestamp();
                        if (!ReadFile(h, buf, 4096, out read, IntPtr.Zero) || read != 4096)
                            throw new System.ComponentModel.Win32Exception();
                        us[i] = (System.Diagnostics.Stopwatch.GetTimestamp() - t0) * 1000000.0 / System.Diagnostics.Stopwatch.Frequency;
                    }
                    Array.Sort(us);
                    int idx = Math.Max(0, (int)Math.Ceiling(iters * 0.95) - 1);
                    return us[idx];
                }
                finally { System.Runtime.InteropServices.Marshal.FreeHGlobal(raw); }
            }
        }

        private double? _benchIdleP95;

        private void UpdateBenchAb(double p95Ms)
        {
            if (txtBenchAB == null) return;
            bool inSession;
            try { inSession = KtEngine.GetState().gameActive != 0; }
            catch { inSession = false; }
            if (!inSession)
            {
                _benchIdleP95 = p95Ms;
                txtBenchAB.Text = "A/B de sessão: baseline " + p95Ms.ToString("0.00") +
                    " ms · rode o bench com o jogo otimizado rodando para comparar";
                txtBenchAB.Foreground = null;
                return;
            }
            if (_benchIdleP95 == null)
            {
                txtBenchAB.Text = "A/B de sessão: rode um bench fora do jogo primeiro (vira baseline)";
                txtBenchAB.Foreground = null;
                return;
            }
            double delta = p95Ms - _benchIdleP95.Value;
            if (Math.Abs(delta) < 0.005)
            {
                txtBenchAB.Text = "A/B de sessão: baseline " + _benchIdleP95.Value.ToString("0.00") +
                    " ms → sessão " + p95Ms.ToString("0.00") + " ms (igual)";
                txtBenchAB.Foreground = BrushAccent3;
                return;
            }
            txtBenchAB.Text = "A/B de sessão: baseline " + _benchIdleP95.Value.ToString("0.00") +
                " ms → sessão " + p95Ms.ToString("0.00") + " ms (" +
                (delta <= 0 ? "↓ melhora " : "↑ piora ") + Math.Abs(delta).ToString("0.00") + " ms)";
            txtBenchAB.Foreground = delta <= 0 ? BrushSuccess : BrushWarn;
        }

        private void AddBenchHistory(double p95Ms)
        {            _benchSeries.Add(p95Ms);
            if (_benchSeries.Count > 40) _benchSeries.RemoveAt(0);
            double min = _benchSeries.Min();
            double max = _benchSeries.Max();
            double range = max - min;
            sparkBench.Values = range < 0.001
                ? _benchSeries.Select(v => 0.5).ToArray()
                : _benchSeries.Select(v => 0.1 + ((v - min) / range) * 0.8).ToArray();
            txtBenchEmpty.Visibility = Visibility.Collapsed;
            txtBenchRuns.Text = _benchSeries.Count + (_benchSeries.Count == 1 ? " execução" : " execuções");
            txtBenchBest.Text = "melhor: " + _benchSeries.Min().ToString("0.00") + " ms";
            txtBenchWorst.Text = "pior: " + _benchSeries.Max().ToString("0.00") + " ms";
            if (_benchSeries.Count < 2)
            {
                txtBenchTrend.Text = "tendência: aguardando próxima execução";
                return;
            }

            double delta = _benchSeries[_benchSeries.Count - 1] - _benchSeries[_benchSeries.Count - 2];
            txtBenchTrend.Text = "tendência: " + (delta <= 0 ? "↓ " : "↑ ") +
                                 Math.Abs(delta).ToString("0.00") + " ms";
            txtBenchTrend.Foreground = delta <= 0 ? BrushSuccess : BrushDanger;
        }

        private bool _gameUiMode;
        private bool _gameUiAutoHid;
        private readonly System.Collections.Generic.Dictionary<FrameworkElement, System.Windows.Media.Effects.Effect> _savedEffects =
            new System.Collections.Generic.Dictionary<FrameworkElement, System.Windows.Media.Effects.Effect>();

        private const string UiPrefsKeyPath = @"Software\KTGameBooster";

        private void LoadHideOnGamePref()
        {
            bool on = true;
            try
            {
                using (var k = Microsoft.Win32.Registry.CurrentUser.OpenSubKey(UiPrefsKeyPath, false))
                    if (k != null) on = ("" + k.GetValue("HidePanelOnGame", "1")) == "1";
            }
            catch { }
            chkHideOnGame.IsChecked = on;
        }

        private void HideOnGame_Changed(object sender, RoutedEventArgs e)
        {
            if (!IsXamlLoaded || chkHideOnGame == null) return;
            try
            {
                using (var k = Microsoft.Win32.Registry.CurrentUser.CreateSubKey(UiPrefsKeyPath))
                    k.SetValue("HidePanelOnGame", chkHideOnGame.IsChecked == true ? "1" : "0");
            }
            catch { }
            if (chkHideOnGame.IsChecked != true && _gameUiMode && _gameUiAutoHid && !IsVisible)
                Dispatcher.BeginInvoke(new Action(Show), DispatcherPriority.ApplicationIdle);
        }

        private void ApplyGameUiMode(bool gameActive, string gameName)
        {
            if (gameActive == _gameUiMode) return;
            _gameUiMode = gameActive;
            try
            {
                if (bgFx != null) bgFx.Visibility = gameActive ? Visibility.Collapsed : Visibility.Visible;
                var root = Content as DependencyObject;
                if (root != null)
                {
                    if (gameActive)
                    {
                        _savedEffects.Clear();
                        SuspendEffects(root);
                    }
                    else RestoreEffects(root);
                }
                if (gameActive
                    && chkHideOnGame != null && chkHideOnGame.IsChecked == true
                    && IsVisible && WindowState != WindowState.Minimized)
                {
                    _gameUiAutoHid = true;
                    Hide();
                    ShowBalloon("Modo jogo · painel na bandeja",
                        "Animações e sombras desligadas; a otimização de "
                        + (string.IsNullOrEmpty(gameName) ? "jogo" : gameName) + " continua.");
                }
                else if (!gameActive && _gameUiAutoHid)
                {
                    _gameUiAutoHid = false;
                    if (!IsVisible)
                        ShowBalloon("Painel liberado", "Sessão encerrada; clique no ícone da bandeja para reabrir.");
                }
            }
            catch { }
        }

        private void SuspendEffects(DependencyObject node)
        {
            var fe = node as FrameworkElement;
            if (fe != null && fe.Effect != null) { _savedEffects[fe] = fe.Effect; fe.Effect = null; }
            int n = System.Windows.Media.VisualTreeHelper.GetChildrenCount(node);
            for (int i = 0; i < n; i++)
                SuspendEffects(System.Windows.Media.VisualTreeHelper.GetChild(node, i));
        }

        private void RestoreEffects(DependencyObject node)
        {
            var fe = node as FrameworkElement;
            if (fe != null && _savedEffects.TryGetValue(fe, out System.Windows.Media.Effects.Effect ef))
            {
                fe.Effect = ef;
                _savedEffects.Remove(fe);
            }
            int n = System.Windows.Media.VisualTreeHelper.GetChildrenCount(node);
            for (int i = 0; i < n; i++)
                RestoreEffects(System.Windows.Media.VisualTreeHelper.GetChild(node, i));
        }

        private void RefreshLog()
        {
            try
            {
                EnsureConnected();
                if (!_engineReady)
                {
                    string detail = "Motor offline: recuperação da sessão pendente necessária.";
                    lstLog.ItemsSource = new[] { detail };
                    txtLogCount.Text = "(1)";
                    return;
                }
                _lastLog = KtEngine.GetLogV2(_logMinLevel, 400);
                ApplyLogFilter();
            }
            catch { }
        }

        private void ApplyLogFilter()
        {
            if (_lastLog == null) { lstLog.ItemsSource = null; return; }
            string f = (_logFilter ?? "").Trim().ToLowerInvariant();
            IEnumerable<KtLogLine> q = _lastLog;
            if (f.Length > 0)
                q = q.Where(l => (l.text ?? "").ToLowerInvariant().Contains(f));
            var arr = q.Select(l => l.text).ToArray();
            lstLog.ItemsSource = arr;
            txtLogCount.Text = "(" + arr.Length + ")";
            if (_logAutoScroll && arr.Length > 0)
                ScrollLogToEnd();
        }

        private void cmbLogLevel_Changed(object sender, SelectionChangedEventArgs e)
        {
            if (!IsXamlLoaded) return;
            if (cmbLogLevel == null) return;
            _logMinLevel = cmbLogLevel.SelectedIndex;
            RefreshLog();
        }

        private void txtLogFilter_TextChanged(object sender, System.Windows.Controls.TextChangedEventArgs e)
        {
            if (!IsXamlLoaded) return;
            _logFilter = txtLogFilter?.Text ?? "";
            ApplyLogFilter();
        }

        private void chkLogAuto_Changed(object sender, RoutedEventArgs e)
        {
            if (!IsXamlLoaded) return;
            _logAutoScroll = chkLogAuto.IsChecked == true;
            if (_logAutoScroll && lstLog.Items.Count > 0)
                ScrollLogToEnd();
        }

        private void ScrollLogToEnd()
        {
            Dispatcher.BeginInvoke(new Action(() =>
            {
                var scroll = lstLog.GetVisualDescendant<System.Windows.Controls.ScrollViewer>();
                if (scroll != null) scroll.ScrollToEnd();
            }), DispatcherPriority.Background);
        }

        private void btnCopyLog_Click(object sender, RoutedEventArgs e)
        {
            try
            {
                var sb = new StringBuilder();
                if (lstLog.ItemsSource != null)
                    foreach (var it in lstLog.ItemsSource) sb.AppendLine(it?.ToString() ?? "");
                System.Windows.Clipboard.SetText(sb.ToString());
            }
            catch { }
        }

        private void btnExportLog_Click(object sender, RoutedEventArgs e)
        {
            try
            {
                var dlg = new Microsoft.Win32.SaveFileDialog();
                dlg.FileName = "kt-log-" + DateTime.Now.ToString("yyyyMMdd-HHmmss") + ".txt";
                dlg.Filter = "Log|*.txt|Tudo|*.*";
                if (dlg.ShowDialog() == true)
                {
                    var sb = new StringBuilder();
                    if (lstLog.ItemsSource != null)
                        foreach (var it in lstLog.ItemsSource) sb.AppendLine(it?.ToString() ?? "");
                    File.WriteAllText(dlg.FileName, sb.ToString());
                    txtStatus.Text = "log exportado: " + dlg.FileName;
                }
            }
            catch { }
        }

        private const string AutoRunKeyPath = @"Software\Microsoft\Windows\CurrentVersion\Run";
        private const string AutoRunValueName = "KTGameBooster";

        private void LoadAutoRunState()
        {
            try
            {
                using (var key = Microsoft.Win32.Registry.CurrentUser.OpenSubKey(AutoRunKeyPath, false))
                {
                    string present = key?.GetValue(AutoRunValueName, null) as string;
                    _syncingToggles = true;
                    chkAutoRun.IsChecked = !string.IsNullOrWhiteSpace(present);
                    _syncingToggles = false;
                }
            }
            catch {   }
        }

        private void AutoRun_Changed(object sender, RoutedEventArgs e)
        {
            if (!IsXamlLoaded || _syncingToggles) return;
            bool on = chkAutoRun.IsChecked == true;
            try
            {
                using (var key = Microsoft.Win32.Registry.CurrentUser.CreateSubKey(AutoRunKeyPath, true))
                {
                    if (on)
                    {
                        string exe;
                        try { exe = Process.GetCurrentProcess().MainModule?.FileName; }
                        catch { exe = null; }
                        if (string.IsNullOrEmpty(exe))
                            exe = System.Windows.Forms.Application.ExecutablePath;
                        key.SetValue(AutoRunValueName, "\"" + exe + "\" --minimized");
                    }
                    else key.DeleteValue(AutoRunValueName, throwOnMissingValue: false);
                }
                txtStatus.Text = on ? "auto-início com o Windows ligado (bandeja)" : "auto-início com o Windows desligado";
            }
            catch (Exception ex) { ReportUiError("auto-início", ex); }
        }

        private void btnPresetBalanced_Click(object sender, RoutedEventArgs e)
        {
            StagePreset(new[] {
                "trim_background_ws", "background_limiter",
                "turbo_boost", "gpu_priority", "core_parking_disable",
                "dwm_mmcss", "eco_qos_background", "io_priority_background",
                "high_performance_plan",
                "thread_power_game_unthrottle", "game_tree_boost",
                "game_dvr_disable"
            }, false);
            txtStatus.Text = "preset equilibrado pendente · aplique ou descarte as alterações";
        }

        private void btnPresetCompetitive_Click(object sender, RoutedEventArgs e)
        {
            StagePreset(new[] {
                "trim_background_ws", "background_limiter",
                "turbo_boost", "gpu_priority", "core_parking_disable",
                "dwm_mmcss", "eco_qos_background", "io_priority_background",
                "high_performance_plan",
                "thread_power_game_unthrottle", "game_tree_boost",
                "boost_input_threads", "io_priority_game_high", "mmcss_game",
                "page_priority_game", "dwm_thread_priority", "thread_power_background_throttle",
                "game_dvr_disable"
            }, true);
            txtStatus.Text = "preset FPS máximo pendente · aplique ou descarte as alterações";
        }

        private void btnPresetStreaming_Click(object sender, RoutedEventArgs e)
        {
            StagePreset(new[] {
                "background_limiter", "eco_qos_background",
                "io_priority_background", "high_performance_plan",
                "thread_power_game_unthrottle", "gpu_priority", "dwm_mmcss",
                "game_tree_boost",
                "game_dvr_disable"
            }, false);
            txtStatus.Text = "preset streaming pendente · aplique ou descarte as alterações";
        }

        private void btnPresetDefaults_Click(object sender, RoutedEventArgs e)
        {
            StagePreset(new[] {
                "trim_background_ws", "background_limiter",
                "turbo_boost", "gpu_priority", "core_parking_disable",
                "dwm_mmcss", "eco_qos_background", "io_priority_background",
                "high_performance_plan", "thread_power_game_unthrottle",
                "game_tree_boost", "dwm_thread_priority",
                "thread_power_background_throttle", "io_priority_game_high",
                "mmcss_game", "page_priority_game",
                "game_dvr_disable"
            }, false);
            txtStatus.Text = "preset padrão pendente · aplique ou descarte as alterações";
        }

        private void StagePreset(string[] on, bool tournament)
        {
            var onSet = new System.Collections.Generic.HashSet<string>(on);
            _syncingToggles = true;
            foreach (var cb in FindAllToggles())
            {
                if (cb.Tag == null) continue;
                string tag = cb.Tag.ToString();
                if (tag == "auto_profile_switching") continue;
                bool v = onSet.Contains(tag);
                cb.IsChecked = v;
                if (TryGetCurrentFeatureValue(tag, out bool current) && current == v)
                    _stagedTweaks.Remove(tag);
                else
                    _stagedTweaks[tag] = v;
            }
            _syncingToggles = false;
            if (TryGetCurrentFeatureValue("tournament_mode", out bool currentTournament) && currentTournament == tournament)
                _stagedTweaks.Remove("tournament_mode");
            else
                _stagedTweaks["tournament_mode"] = tournament;
            UpdateStagedUi();
        }

        private void LoadExclusions()
        {
            try
            {
                var list = new System.Collections.Generic.List<string>();
                var path = Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "gamemode.toml");
                if (File.Exists(path))
                {
                    foreach (var line in File.ReadAllLines(path))
                    {
                        var t = line.Trim();
                        if (t.StartsWith("exe", StringComparison.OrdinalIgnoreCase) && t.Contains("="))
                        {
                            var v = t.Substring(t.IndexOf('=') + 1).Trim().Trim('"');
                            if (v.Length > 3 && v.EndsWith(".exe", StringComparison.OrdinalIgnoreCase))
                                list.Add(v.ToLowerInvariant());
                        }
                    }
                }
                lstExclusions.ItemsSource = list.Distinct().ToList();
            }
            catch { }
        }

        private void btnAddExclusion_Click(object sender, RoutedEventArgs e)
        {
            var name = txtAddExclusion.Text?.Trim();
            if (string.IsNullOrEmpty(name)) return;
            try
            {
                EnsureConnected();
                int r = KtEngine.AddExclusion(name);
                txtAddExclusion.Text = "";
                LoadExclusions();
                txtStatus.Text = r == 1 ? "exclusão adicionada: " + Path.GetFileName(name).ToLowerInvariant()
                                         : "já excluído / inválido";
            }
            catch { }
        }

        private void btnRemoveExclusion_Click(object sender, RoutedEventArgs e)
        {
            var b = sender as System.Windows.Controls.Button;
            if (b == null || b.Tag == null) return;
            try
            {
                EnsureConnected();
                KtEngine.RemoveExclusion(b.Tag.ToString());
                LoadExclusions();
            }
            catch { }
        }

        private void LoadGameList()
        {
            try
            {
                var list = new System.Collections.Generic.List<string>();
                var path = Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "gamemode.toml");
                if (File.Exists(path))
                {
                    foreach (var line in File.ReadAllLines(path))
                    {
                        var t = line.Trim();
                        if (t.StartsWith("exe", StringComparison.OrdinalIgnoreCase) && t.Contains("="))
                        {
                            var v = t.Substring(t.IndexOf('=') + 1).Trim().Trim('"');
                            if (v.Length > 3 && v.EndsWith(".exe", StringComparison.OrdinalIgnoreCase))
                                list.Add(v.ToLowerInvariant());
                        }
                    }
                }
                lstGames.ItemsSource = list.Distinct().ToList();
            }
            catch { }
        }
    }

    public class ProcRow : System.ComponentModel.INotifyPropertyChanged
    {
        private ImageSource _icon;
        public event System.ComponentModel.PropertyChangedEventHandler PropertyChanged;
        public string pid { get; set; }
        public uint pidNum { get; set; }
        public string name { get; set; }
        public string klass { get; set; }
        public string stateStr { get; set; }
        public string cpuStr { get; set; }
        public string ramStr { get; set; }
            public double cpuPct { get; set; }
            public uint ramMB { get; set; }
        public string affinityHex { get; set; }
        public Brush stateBrush { get; set; }
        public string rawName { get; set; }
        public ulong creationTime { get; set; }
        public ImageSource icon
        {
            get { return _icon; }
            set
            {
                if (ReferenceEquals(_icon, value)) return;
                _icon = value;
                PropertyChanged?.Invoke(this, new System.ComponentModel.PropertyChangedEventArgs(nameof(icon)));
            }
        }
    }

    public class SvcRow
    {
        public string name { get; set; }
        public string display { get; set; }
        public string stateStr { get; set; }
        public System.Windows.Media.Brush stateBrush { get; set; }
        public System.Windows.Media.Brush planBrush { get; set; }
        public string planStr { get; set; }
        public string reasonStr { get; set; }
        public string pid { get; set; }
        public string rawName { get; set; }
        public uint state { get; set; }
        public bool canStop { get; set; }
        public bool canPause { get; set; }
        public bool isTarget { get; set; }
        public bool isProtected { get; set; }
    }

    public static class VisualTreeExtensions
    {
        public static System.Collections.Generic.IEnumerable<T> FindLogicalChildren<T>(this System.Windows.DependencyObject depObj) where T : System.Windows.DependencyObject
        {
            if (depObj == null) yield break;
            foreach (var child in System.Windows.LogicalTreeHelper.GetChildren(depObj))
            {
                var d = child as System.Windows.DependencyObject;
                if (d == null) continue;
                if (d is T t) yield return t;
                foreach (var c in d.FindLogicalChildren<T>()) yield return c;
            }
        }

        public static T GetAncestor<T>(this System.Windows.DependencyObject dep) where T : System.Windows.DependencyObject
        {
            var cur = dep;
            while (cur != null)
            {
                if (cur is T t) return t;
                cur = System.Windows.Media.VisualTreeHelper.GetParent(cur);
            }
            return null;
        }

        public static T GetVisualDescendant<T>(this System.Windows.DependencyObject parent) where T : System.Windows.DependencyObject
        {
            if (parent == null) return null;
            int count = System.Windows.Media.VisualTreeHelper.GetChildrenCount(parent);
            for (int i = 0; i < count; i++)
            {
                var child = System.Windows.Media.VisualTreeHelper.GetChild(parent, i);
                if (child is T match) return match;
                var nested = child.GetVisualDescendant<T>();
                if (nested != null) return nested;
            }
            return null;
        }
    }
}
