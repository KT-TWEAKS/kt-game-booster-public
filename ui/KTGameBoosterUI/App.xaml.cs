using System;
using System.IO;
using System.Reflection;
using System.Windows;

namespace KTGameBooster
{
    public partial class App : Application
    {
        internal static bool SafeMode { get; private set; }

        internal static void SetSafeMode(bool enabled)
        {
            SafeMode = enabled;
        }

        protected override void OnStartup(StartupEventArgs e)
        {
            if (UpdateService.TryRunApplyMode(e.Args))
            {
                ShutdownMode = ShutdownMode.OnExplicitShutdown;
                base.OnStartup(e);
                Shutdown(0);
                return;
            }
            base.OnStartup(e);
            SafeMode = e.Args != null && Array.Exists(e.Args,
                a => string.Equals(a, "--safe-mode", StringComparison.OrdinalIgnoreCase));

            Directory.SetCurrentDirectory(AppDomain.CurrentDomain.BaseDirectory);

            DispatcherUnhandledException += (s, args) =>
            {
                try
                {
                    File.AppendAllText(
                        Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "kt-ui.log"),
                        $"[{DateTime.Now:yyyy-MM-dd HH:mm:ss}] DISPATCHER\n" +
                        args.Exception + "\n\n");
                }
                catch { }
                args.Handled = true;
            };

            ExtractEngine();
            ExtractBundledData();

            var window = new MainWindow();
            MainWindow = window;
            window.Show();
        }

        private static void ExtractEngine()
        {
            try
            {
                string dst = Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "ktengine.dll");
                var asm = Assembly.GetExecutingAssembly();
                using (var s = asm.GetManifestResourceStream("KTGameBooster.Engine.ktengine.dll"))
                {
                    if (s == null) throw new InvalidOperationException("Motor incorporado não encontrado.");
                    byte[] embedded;
                    using (var ms = new MemoryStream())
                    {
                        s.CopyTo(ms);
                        embedded = ms.ToArray();
                    }

                    bool current = false;
                    if (File.Exists(dst))
                    {
                        var disk = File.ReadAllBytes(dst);
                        if (disk.Length == embedded.Length)
                        {
                            current = true;
                            for (int i = 0; i < disk.Length; i++)
                            {
                                if (disk[i] != embedded[i]) { current = false; break; }
                            }
                        }
                    }

                    if (!current)
                    {
                        string temp = dst + ".update";
                        File.WriteAllBytes(temp, embedded);
                        File.Copy(temp, dst, true);
                        File.Delete(temp);
                    }
                }
            }
            catch (Exception ex)
            {
                try
                {
                    File.AppendAllText(
                        Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "kt-ui.log"),
                        $"[{DateTime.Now:yyyy-MM-dd HH:mm:ss}] ENGINE EXTRACTION\n{ex}\n\n");
                }
                catch { }
            }
        }

        private static void ExtractBundledData()
        {
            try
            {
                string baseDir = AppDomain.CurrentDomain.BaseDirectory;
                var asm = Assembly.GetExecutingAssembly();
                foreach (string name in asm.GetManifestResourceNames())
                {
                    string fileName;
                    if (name.StartsWith("KTGameBooster.Config.", StringComparison.Ordinal))
                        fileName = name.Substring("KTGameBooster.Config.".Length);
                    else if (name.StartsWith("KTGameBooster.Profiles.", StringComparison.Ordinal))
                        fileName = "profiles\\" + name.Substring("KTGameBooster.Profiles.".Length);
                    else
                        continue;

                    string dst = Path.Combine(baseDir, fileName);
                    if (File.Exists(dst)) continue;
                    Directory.CreateDirectory(Path.GetDirectoryName(dst));
                    using (var s = asm.GetManifestResourceStream(name))
                    using (var fs = File.Create(dst))
                        s.CopyTo(fs);
                }
            }
            catch (Exception ex)
            {
                try
                {
                    File.AppendAllText(
                        Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "kt-ui.log"),
                        $"[{DateTime.Now:yyyy-MM-dd HH:mm:ss}] BUNDLED DATA EXTRACTION\n{ex}\n\n");
                }
                catch { }
            }
        }
    }
}
