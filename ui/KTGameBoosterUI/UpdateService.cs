using System;
using System.Diagnostics;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Net;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text;
using System.Text.RegularExpressions;
using System.Web.Script.Serialization;

namespace KTGameBooster
{
    internal sealed class UpdateCheckResult
    {
        public bool UpdateAvailable;
        public bool AuthenticationRequired;
        public string Tag;
        public string ZipPath;
        public string Sha256;
        public string Message;
        public long ZipSize;
    }

    internal static class UpdateService
    {
        internal const string Repository = "KT-TWEAKS/kt-game-booster-public";
        internal const string CurrentTag = "v1.0-relaunch.5";
        internal const string ProductVersion = "1.0";
        internal static readonly string ZipName = "KTGameBooster-v" + ProductVersion + "-win-x64.zip";
        internal static readonly string ShaName = ZipName + ".sha256";
        private static readonly Regex ReleaseTag = new Regex(
            @"^v1\.0-relaunch\.(\d+)$", RegexOptions.IgnoreCase | RegexOptions.CultureInvariant);

        internal static UpdateCheckResult CheckAndDownload()
        {
            try
            {
                bool useCli = false;
                string[] tags = new string[0];
                GitHubRelease[] apiReleases = null;
                try
                {
                    var version = RunGitHub("--version");
                    var list = version.ExitCode == 0
                        ? RunGitHub("release", "list", "--repo", Repository, "--limit", "50",
                            "--exclude-drafts", "--json", "tagName", "--jq", ".[].tagName")
                        : null;
                    if (list != null && list.ExitCode == 0)
                    {
                        useCli = true;
                        tags = list.StdOut.Split(new[] { '\r', '\n' }, StringSplitOptions.RemoveEmptyEntries)
                            .Select(x => x.Trim()).ToArray();
                    }
                }
                catch { }

                if (!useCli)
                {
                    apiReleases = GetApiReleases();
                    tags = apiReleases.Where(x => !x.draft).Select(x => x.tag_name).ToArray();
                }
                int currentRevision = ParseRevision(CurrentTag);
                string newestTag = tags
                    .Where(x => ParseRevision(x) >= 0)
                    .OrderByDescending(ParseRevision)
                    .FirstOrDefault();
                int newestRevision = ParseRevision(newestTag);
                if (newestRevision <= currentRevision)
                {
                    return new UpdateCheckResult
                    {
                        Tag = CurrentTag,
                        Message = "Atualizado · verificação automática pelo GitHub ligada"
                    };
                }

                string updateDir = Path.Combine(
                    Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
                    "KTGameBooster", "Updates", newestTag);
                Directory.CreateDirectory(updateDir);
                if (useCli)
                {
                    var download = RunGitHub("release", "download", newestTag, "--repo", Repository,
                        "--pattern", ZipName, "--pattern", ShaName, "--dir", updateDir, "--clobber");
                    if (download.ExitCode != 0)
                        return Failure("Nova versão encontrada, mas o download privado falhou: " + Clean(download.StdErr));
                }
                else
                {
                    var release = apiReleases.First(x => newestTag.Equals(x.tag_name, StringComparison.OrdinalIgnoreCase));
                    DownloadApiAsset(release, ZipName, Path.Combine(updateDir, ZipName));
                    DownloadApiAsset(release, ShaName, Path.Combine(updateDir, ShaName));
                }

                string zip = Path.Combine(updateDir, ZipName);
                string shaFile = Path.Combine(updateDir, ShaName);
                if (!File.Exists(zip) || !File.Exists(shaFile))
                    return Failure("A release nova não contém o ZIP e o SHA-256 esperados.");
                if (new FileInfo(zip).Length <= 0)
                    return Failure("Atualização descartada: o ZIP baixado está vazio.");

                string expected = Regex.Match(File.ReadAllText(shaFile), @"\b[0-9a-fA-F]{64}\b").Value;
                string actual = ComputeSha256(zip);
                if (expected.Length != 64 || !actual.Equals(expected, StringComparison.OrdinalIgnoreCase))
                {
                    try { File.Delete(zip); } catch { }
                    return Failure("Atualização descartada: o SHA-256 publicado não confere.");
                }

                return new UpdateCheckResult
                {
                    UpdateAvailable = true,
                    Tag = newestTag,
                    ZipPath = zip,
                    Sha256 = actual,
                    ZipSize = new FileInfo(zip).Length,
                    Message = "Atualização " + newestTag + " baixada e verificada"
                };
            }
            catch (WebException)
            {
                return AuthResult("Auto-update ligado · privado: use gh auth login; público: funciona sem login");
            }
            catch (Exception ex)
            {
                return Failure("Auto-update temporariamente indisponível: " + ex.Message);
            }
        }

        private sealed class GitHubRelease
        {
            public string tag_name { get; set; }
            public bool draft { get; set; }
            public GitHubAsset[] assets { get; set; }
        }

        private sealed class GitHubAsset
        {
            public string name { get; set; }
            public string browser_download_url { get; set; }
            public long size { get; set; }
        }

        private static GitHubRelease[] GetApiReleases()
        {
            using (var client = CreateGitHubClient())
            {
                string json = client.DownloadString("https://api.github.com/repos/" + Repository + "/releases?per_page=50");
                return new JavaScriptSerializer().Deserialize<GitHubRelease[]>(json) ?? new GitHubRelease[0];
            }
        }

        private static void DownloadApiAsset(GitHubRelease release, string name, string destination)
        {
            var asset = (release.assets ?? new GitHubAsset[0])
                .FirstOrDefault(x => name.Equals(x.name, StringComparison.OrdinalIgnoreCase));
            if (asset == null || string.IsNullOrWhiteSpace(asset.browser_download_url))
                throw new InvalidDataException("Asset ausente na release: " + name);
            using (var client = CreateGitHubClient()) client.DownloadFile(asset.browser_download_url, destination);
            if (!File.Exists(destination) || new FileInfo(destination).Length <= 0 ||
                (asset.size > 0 && new FileInfo(destination).Length != asset.size))
                throw new InvalidDataException("Tamanho inválido para o asset: " + name);
        }

        private static WebClient CreateGitHubClient()
        {
            var client = new WebClient();
            client.Headers[HttpRequestHeader.UserAgent] = "KTGameBooster/1.0";
            client.Headers[HttpRequestHeader.Accept] = "application/vnd.github+json";
            string token = Environment.GetEnvironmentVariable("GH_TOKEN");
            if (string.IsNullOrWhiteSpace(token)) token = Environment.GetEnvironmentVariable("GITHUB_TOKEN");
            if (!string.IsNullOrWhiteSpace(token)) client.Headers[HttpRequestHeader.Authorization] = "Bearer " + token.Trim();
            return client;
        }

        internal static bool StartInstaller(UpdateCheckResult update, int oldPid, string targetDirectory)
        {
            if (update == null || !update.UpdateAvailable || !File.Exists(update.ZipPath)) return false;
            string currentExe = Process.GetCurrentProcess().MainModule.FileName;
            string updaterExe = Path.Combine(Path.GetTempPath(), "KTGameBooster-Updater-" + Guid.NewGuid().ToString("N") + ".exe");
            File.Copy(currentExe, updaterExe, true);
            var psi = new ProcessStartInfo
            {
                FileName = updaterExe,
                UseShellExecute = false,
                WorkingDirectory = Path.GetTempPath(),
                Arguments = JoinArguments("--apply-update", oldPid.ToString(), update.ZipPath,
                    targetDirectory.TrimEnd('\\', '/'), Path.GetFileName(currentExe), update.Sha256, update.Tag)
            };
            Process.Start(psi);
            return true;
        }

        internal static bool TryRunApplyMode(string[] args)
        {
            if (args == null || args.Length < 7 || !args[0].Equals("--apply-update", StringComparison.OrdinalIgnoreCase))
                return false;

            string target = args[3];
            string log = Path.Combine(target, "kt-update.log");
            try
            {
                int oldPid = int.Parse(args[1]);
                string zip = args[2];
                string exeName = args[4];
                string expectedSha = args[5];
                string tag = args[6];
                try
                {
                    var old = Process.GetProcessById(oldPid);
                    if (!old.WaitForExit(30000)) throw new InvalidOperationException("O aplicativo antigo não encerrou a tempo.");
                }
                catch (ArgumentException) { }

                if (!File.Exists(zip) || new FileInfo(zip).Length <= 0 ||
                    !ComputeSha256(zip).Equals(expectedSha, StringComparison.OrdinalIgnoreCase))
                    throw new InvalidDataException("Pacote de atualização ausente ou com SHA-256 inválido.");

                string stage = Path.Combine(Path.GetTempPath(), "KTGameBooster-Stage-" + Guid.NewGuid().ToString("N"));
                Directory.CreateDirectory(stage);
                ExtractSafe(zip, stage);
                CopyTreeAtomically(stage, target);
                try { Directory.Delete(stage, true); } catch { }

                Process.Start(new ProcessStartInfo
                {
                    FileName = Path.Combine(target, exeName),
                    WorkingDirectory = target,
                    UseShellExecute = true,
                    Arguments = Quote("--updated=" + tag)
                });
                try { File.AppendAllText(log, DateTime.Now.ToString("s") + " atualizado para " + tag + Environment.NewLine); } catch { }
                MoveFileEx(Process.GetCurrentProcess().MainModule.FileName, null, 4);
            }
            catch (Exception ex)
            {
                try { File.AppendAllText(log, DateTime.Now.ToString("s") + " falha: " + ex + Environment.NewLine); } catch { }
            }
            return true;
        }

        private static void ExtractSafe(string zipPath, string stage)
        {
            string root = Path.GetFullPath(stage).TrimEnd(Path.DirectorySeparatorChar) + Path.DirectorySeparatorChar;
            using (var archive = ZipFile.OpenRead(zipPath))
            {
                foreach (var entry in archive.Entries)
                {
                    string destination = Path.GetFullPath(Path.Combine(stage, entry.FullName));
                    if (!destination.StartsWith(root, StringComparison.OrdinalIgnoreCase))
                        throw new InvalidDataException("Entrada insegura no ZIP de atualização.");
                    if (string.IsNullOrEmpty(entry.Name))
                    {
                        Directory.CreateDirectory(destination);
                        continue;
                    }
                    Directory.CreateDirectory(Path.GetDirectoryName(destination));
                    entry.ExtractToFile(destination, true);
                }
            }
        }

        private static void CopyTreeAtomically(string source, string target)
        {
            Directory.CreateDirectory(target);
            foreach (string directory in Directory.GetDirectories(source, "*", SearchOption.AllDirectories))
                Directory.CreateDirectory(Path.Combine(target, directory.Substring(source.Length).TrimStart('\\', '/')));
            foreach (string file in Directory.GetFiles(source, "*", SearchOption.AllDirectories))
            {
                string relative = file.Substring(source.Length).TrimStart('\\', '/');
                string destination = Path.Combine(target, relative);
                bool userConfig = relative.Equals("gamemode.toml", StringComparison.OrdinalIgnoreCase) ||
                    relative.StartsWith("profiles\\", StringComparison.OrdinalIgnoreCase) ||
                    relative.StartsWith("profiles/", StringComparison.OrdinalIgnoreCase);
                if (userConfig && File.Exists(destination)) continue;
                Directory.CreateDirectory(Path.GetDirectoryName(destination));
                string incoming = destination + ".new";
                string previous = destination + ".previous";
                File.Copy(file, incoming, true);
                if (new FileInfo(incoming).Length != new FileInfo(file).Length)
                    throw new InvalidDataException("Arquivo temporário da atualização foi truncado: " + relative);
                if (File.Exists(destination))
                {
                    File.Replace(incoming, destination, previous, true);
                }
                else File.Move(incoming, destination);
            }
        }

        private static int ParseRevision(string tag)
        {
            if (string.IsNullOrWhiteSpace(tag)) return -1;
            var match = ReleaseTag.Match(tag.Trim());
            return match.Success && int.TryParse(match.Groups[1].Value, out int value) ? value : -1;
        }

        private static string ComputeSha256(string path)
        {
            using (var stream = File.OpenRead(path))
            using (var sha = SHA256.Create())
                return BitConverter.ToString(sha.ComputeHash(stream)).Replace("-", "");
        }

        private sealed class CommandResult
        {
            public int ExitCode;
            public string StdOut;
            public string StdErr;
        }

        private static CommandResult RunGitHub(params string[] args)
        {
            var psi = new ProcessStartInfo
            {
                FileName = "gh",
                Arguments = JoinArguments(args),
                UseShellExecute = false,
                CreateNoWindow = true,
                RedirectStandardOutput = true,
                RedirectStandardError = true,
                StandardOutputEncoding = Encoding.UTF8,
                StandardErrorEncoding = Encoding.UTF8
            };
            using (var process = Process.Start(psi))
            {
                string stdout = process.StandardOutput.ReadToEnd();
                string stderr = process.StandardError.ReadToEnd();
                process.WaitForExit(30000);
                return new CommandResult { ExitCode = process.ExitCode, StdOut = stdout, StdErr = stderr };
            }
        }

        private static string JoinArguments(params string[] args) => string.Join(" ", args.Select(Quote));

        private static string Quote(string value)
        {
            value = value ?? "";
            var result = new StringBuilder("\"");
            int slashes = 0;
            foreach (char c in value)
            {
                if (c == '\\') { slashes++; continue; }
                if (c == '"')
                {
                    result.Append('\\', slashes * 2 + 1).Append('"');
                    slashes = 0;
                    continue;
                }
                result.Append('\\', slashes).Append(c);
                slashes = 0;
            }
            result.Append('\\', slashes * 2).Append('"');
            return result.ToString();
        }
        private static string Clean(string value) => string.IsNullOrWhiteSpace(value) ? "erro desconhecido" : value.Trim().Replace(Environment.NewLine, " ");
        private static UpdateCheckResult Failure(string message) => new UpdateCheckResult { Message = message };
        private static UpdateCheckResult AuthResult(string message) => new UpdateCheckResult { AuthenticationRequired = true, Message = message };

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern bool MoveFileEx(string existing, string destination, int flags);
    }
}
