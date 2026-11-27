using System;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Text;

namespace KTGameBooster
{
    internal static class TelemetryExportService
    {
        internal static string Export(KtMetricsSample[] samples)
        {
            samples = samples ?? new KtMetricsSample[0];
            string dir = Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "exports");
            Directory.CreateDirectory(dir);
            string stamp = DateTime.Now.ToString("yyyyMMdd-HHmmss", CultureInfo.InvariantCulture);
            string csv = Path.Combine(dir, "telemetry-" + stamp + ".csv");
            string json = Path.Combine(dir, "telemetry-" + stamp + ".json");
            var csvText = new StringBuilder("tick_ms,cpu_pct,ram_used_mb,ram_total_mb,fps,ping_ms,gpu_pct\r\n");
            foreach (var s in samples)
                csvText.AppendFormat(CultureInfo.InvariantCulture, "{0},{1:0.##},{2},{3},{4},{5},{6:0.##}\r\n",
                    s.tickMs, s.cpuPct, s.ramUsedMB, s.ramTotalMB, s.fps, s.pingMs, s.gpuPct);
            File.WriteAllText(csv, csvText.ToString(), Encoding.UTF8);

            var jsonText = new StringBuilder();
            bool fpsAvailable = samples.Any(s => s.fps > 0);
            jsonText.Append("{\"exportedAt\":\"").Append(DateTime.UtcNow.ToString("o", CultureInfo.InvariantCulture))
                .Append(fpsAvailable
                    ? "\",\"fps\":{\"available\":true,\"source\":\"ETW Present (DxgKrnl)\"}"
                    : "\",\"fps\":{\"available\":false,\"reason\":\"Nenhum evento Present capturado nesta amostra (o jogo estava fechado ou o monitor não capturou o PID).\"}")
                .Append(",\"samples\":[");
            for (int i = 0; i < samples.Length; i++)
            {
                if (i > 0) jsonText.Append(',');
                var s = samples[i];
                jsonText.AppendFormat(CultureInfo.InvariantCulture,
                    "{{\"tickMs\":{0},\"cpuPct\":{1:0.##},\"ramUsedMB\":{2},\"ramTotalMB\":{3},\"fps\":{4},\"pingMs\":{5},\"gpuPct\":{6:0.##}}}",
                    s.tickMs, s.cpuPct, s.ramUsedMB, s.ramTotalMB, s.fps, s.pingMs, s.gpuPct);
            }
            jsonText.Append("]}");
            File.WriteAllText(json, jsonText.ToString(), Encoding.UTF8);
            return dir;
        }
    }
}
