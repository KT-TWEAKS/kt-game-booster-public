using System;
using System.Globalization;
using System.Windows;
using System.Windows.Data;
using System.Windows.Media;

namespace KTGameBooster
{
    internal static class ThemeRes
    {
        public static SolidColorBrush Res(string key, SolidColorBrush fallback)
            => Application.Current?.TryFindResource(key) as SolidColorBrush ?? fallback;
    }

    public class UsageColorConverter : IValueConverter
    {
        private static readonly SolidColorBrush Green  = new SolidColorBrush(Color.FromRgb(0x22, 0xC5, 0x5E));
        private static readonly SolidColorBrush Yellow = new SolidColorBrush(Color.FromRgb(0xFB, 0xBF, 0x24));
        private static readonly SolidColorBrush Red    = new SolidColorBrush(Color.FromRgb(0xF8, 0x71, 0x71));
        public object Convert(object value, Type targetType, object parameter, CultureInfo culture)
        {
            double pct = 0;
            if (value is double d) pct = d;
            else if (value is int i) pct = i;
            else try { pct = System.Convert.ToDouble(value); } catch { }
            if (pct >= 85) return ThemeRes.Res("DangerBrush", Red);
            if (pct >= 60) return ThemeRes.Res("WarnBrush", Yellow);
            return ThemeRes.Res("SuccessBrush", Green);
        }
        public object ConvertBack(object value, Type targetType, object parameter, CultureInfo culture)
            => Binding.DoNothing;
    }

    public class ConfidenceToLabelConverter : IValueConverter
    {
        public object Convert(object value, Type targetType, object parameter, CultureInfo culture)
        {
            int conf = 0;
            if (value is int i) conf = i;
            else if (value is double d) conf = (int)d;
            else try { conf = System.Convert.ToInt32(value); } catch { }
            if (conf >= 90) return "alta";
            if (conf >= 60) return "média";
            if (conf > 0) return "baixa";
            return "";
        }
        public object ConvertBack(object value, Type targetType, object parameter, CultureInfo culture)
            => Binding.DoNothing;
    }

    public class ConfidenceToBrushConverter : IValueConverter
    {
        private static readonly SolidColorBrush Green  = new SolidColorBrush(Color.FromRgb(0x22, 0xC5, 0x5E));
        private static readonly SolidColorBrush Blue   = new SolidColorBrush(Color.FromRgb(0x22, 0xD3, 0xEE));
        private static readonly SolidColorBrush Yellow = new SolidColorBrush(Color.FromRgb(0xFB, 0xBF, 0x24));
        public object Convert(object value, Type targetType, object parameter, CultureInfo culture)
        {
            int conf = 0;
            if (value is int i) conf = i;
            else if (value is double d) conf = (int)d;
            else try { conf = System.Convert.ToInt32(value); } catch { }
            if (conf >= 90) return ThemeRes.Res("SuccessBrush", Green);
            if (conf >= 60) return ThemeRes.Res("Accent3Brush", Blue);
            if (conf > 0) return ThemeRes.Res("WarnBrush", Yellow);
            return new SolidColorBrush(Colors.Transparent);
        }
        public object ConvertBack(object value, Type targetType, object parameter, CultureInfo culture)
            => Binding.DoNothing;
    }

    public class LogColorConverter : IValueConverter
    {
        private static readonly SolidColorBrush GameBrush  = new SolidColorBrush(Color.FromRgb(0x22, 0xC5, 0x5E));
        private static readonly SolidColorBrush BgBrush    = new SolidColorBrush(Color.FromRgb(0xFB, 0xBF, 0x24));
        private static readonly SolidColorBrush EtwBrush   = new SolidColorBrush(Color.FromRgb(0x22, 0xD3, 0xEE));
        private static readonly SolidColorBrush WarnOrange = new SolidColorBrush(Color.FromRgb(0xFB, 0x92, 0x3C));
        private static readonly SolidColorBrush ErrorBrush = new SolidColorBrush(Color.FromRgb(0xF8, 0x71, 0x71));
        private static readonly SolidColorBrush ModeBrush  = new SolidColorBrush(Color.FromRgb(0xA7, 0x8B, 0xFA));
        private static readonly SolidColorBrush ShellBrush = new SolidColorBrush(Color.FromRgb(0xF8, 0x71, 0x71));
        private static readonly SolidColorBrush DefaultBrush = new SolidColorBrush(Color.FromRgb(0x9A, 0xA5, 0xBD));

        public object Convert(object value, Type targetType, object parameter, CultureInfo culture)
        {
            string line = value as string ?? "";
            string lower = line.ToLowerInvariant();

            if (lower.Contains("[error]") || lower.Contains("[fail")) return ThemeRes.Res("DangerBrush", ErrorBrush);
            if (lower.Contains("[warning]") || lower.Contains("[warn")) return ThemeRes.Res("WarnBrush", WarnOrange);
            if (lower.Contains("[game]") || lower.Contains("game,")) return ThemeRes.Res("SuccessBrush", GameBrush);
            if (lower.Contains("[bg]") || lower.Contains("bg,")) return ThemeRes.Res("WarnBrush", BgBrush);
            if (lower.Contains("[etw]") || lower.Contains("etw,")) return ThemeRes.Res("Accent3Brush", EtwBrush);
            if (lower.Contains("[shell]") || lower.Contains("extreme")) return ThemeRes.Res("DangerBrush", ShellBrush);
            if (lower.Contains("[mode]") || lower.Contains("mode,") || lower.Contains("[profile]")) return ThemeRes.Res("Accent2Brush", ModeBrush);
            return ThemeRes.Res("Fg1Brush", DefaultBrush);
        }
        public object ConvertBack(object value, Type targetType, object parameter, CultureInfo culture)
            => Binding.DoNothing;
    }

    public class ProcessRowConverter : IMultiValueConverter
    {
        private static readonly SolidColorBrush GameRow      = new SolidColorBrush(Color.FromArgb(0x18, 0x22, 0xC5, 0x5E));
        private static readonly SolidColorBrush ChildRow     = new SolidColorBrush(Color.FromArgb(0x18, 0x22, 0xD3, 0xEE));
        private static readonly SolidColorBrush ThrottledRow = new SolidColorBrush(Color.FromArgb(0x18, 0xFB, 0xBF, 0x24));
        private static readonly SolidColorBrush NormalRow    = new SolidColorBrush(Colors.Transparent);

        public object Convert(object[] values, Type targetType, object parameter, CultureInfo culture)
        {
            if (values == null || values.Length < 3) return NormalRow;

            bool isGame = false, throttled = false, child = false;
            if (values[0] is int ig) isGame = ig != 0; else if (values[0] is bool ib) isGame = ib;
            if (values[1] is int it) throttled = it != 0; else if (values[1] is bool tb) throttled = tb;
            if (values.Length > 2 && values[2] is int ic) child = ic != 0;

            if (isGame) return ThemeRes.Res("RowGameBrush", GameRow);
            if (child) return ThemeRes.Res("RowChildBrush", ChildRow);
            if (throttled) return ThemeRes.Res("RowThrottledBrush", ThrottledRow);
            return NormalRow;
        }
        public object[] ConvertBack(object value, Type[] targetTypes, object parameter, CultureInfo culture)
            => new object[] { Binding.DoNothing, Binding.DoNothing, Binding.DoNothing };
    }

    public class BoolToVisibilityConverter : IValueConverter
    {
        public object Convert(object value, Type targetType, object parameter, CultureInfo culture)
            => (value is bool b && b) ? Visibility.Visible : Visibility.Collapsed;
        public object ConvertBack(object value, Type targetType, object parameter, CultureInfo culture)
            => value is Visibility v && v == Visibility.Visible;
    }

    public class IntToVisibilityConverter : IValueConverter
    {
        public object Convert(object value, Type targetType, object parameter, CultureInfo culture)
        {
            int n = 0;
            if (value is int i) n = i;
            else try { n = System.Convert.ToInt32(value); } catch { }
            return n != 0 ? Visibility.Visible : Visibility.Collapsed;
        }
        public object ConvertBack(object value, Type targetType, object parameter, CultureInfo culture)
            => Binding.DoNothing;
    }

    public class OnOffLabelConverter : IValueConverter
    {
        public object Convert(object value, Type targetType, object parameter, CultureInfo culture)
        {
            int n = 0;
            if (value is int i) n = i;
            else try { n = System.Convert.ToInt32(value); } catch { }
            return n != 0 ? "ON" : "OFF";
        }
        public object ConvertBack(object value, Type targetType, object parameter, CultureInfo culture)
            => Binding.DoNothing;
    }

    public class OnOffBrushConverter : IValueConverter
    {
        private static readonly SolidColorBrush On  = new SolidColorBrush(Color.FromRgb(0x22, 0xC5, 0x5E));
        private static readonly SolidColorBrush Off = new SolidColorBrush(Color.FromRgb(0xF8, 0x71, 0x71));
        public object Convert(object value, Type targetType, object parameter, CultureInfo culture)
        {
            int n = 0;
            if (value is int i) n = i;
            else try { n = System.Convert.ToInt32(value); } catch { }
            return n != 0 ? (object)ThemeRes.Res("SuccessBrush", On) : ThemeRes.Res("DangerBrush", Off);
        }
        public object ConvertBack(object value, Type targetType, object parameter, CultureInfo culture)
            => Binding.DoNothing;
    }

    public class BytesHumanConverter : IValueConverter
    {
        public object Convert(object value, Type targetType, object parameter, CultureInfo culture)
        {
            ulong b = 0;
            if (value is ulong u) b = u;
            else if (value is long l) b = (ulong)Math.Max(0, l);
            else try { b = (ulong)System.Convert.ToInt64(value); } catch { }
            if (b >= (ulong)1024 * 1024 * 1024) return (b / 1073741824.0).ToString("0.00") + " GB";
            if (b >= 1024 * 1024) return (b / 1048576.0).ToString("0.0") + " MB";
            if (b >= 1024) return (b / 1024.0).ToString("0") + " KB";
            return b + " B";
        }
        public object ConvertBack(object value, Type targetType, object parameter, CultureInfo culture)
            => Binding.DoNothing;
    }

    public class DurationConverter : IValueConverter
    {
        public object Convert(object value, Type targetType, object parameter, CultureInfo culture)
        {
            uint sec = 0;
            if (value is uint u) sec = u;
            else if (value is int i) sec = (uint)Math.Max(0, i);
            else try { sec = (uint)System.Convert.ToUInt32(value); } catch { }
            if (sec >= 3600) return (sec / 3600) + "h " + ((sec % 3600) / 60) + "m";
            if (sec >= 60)   return (sec / 60) + "m " + (sec % 60) + "s";
            return sec + "s";
        }
        public object ConvertBack(object value, Type targetType, object parameter, CultureInfo culture)
            => Binding.DoNothing;
    }

    public class InverseBoolConverter : IValueConverter
    {
        public object Convert(object value, Type targetType, object parameter, CultureInfo culture)
            => !(value is bool b && b);
        public object ConvertBack(object value, Type targetType, object parameter, CultureInfo culture)
            => value is bool b && !b;
    }

    public class ServiceCriticalBrushConverter : IValueConverter
    {
        private static readonly SolidColorBrush Crit = new SolidColorBrush(Color.FromArgb(0x20, 0xF8, 0x71, 0x71));
        private static readonly SolidColorBrush Norm = new SolidColorBrush(Colors.Transparent);
        private static readonly System.Collections.Generic.HashSet<string> kCrit = new System.Collections.Generic.HashSet<string>(StringComparer.OrdinalIgnoreCase)
        {
            "RpcSs", "DCOMLaunch", "LSM", "Winmgmt", "BrokerInfrastructure",
            "SystemEventsBroker", "themes", "explorer", "schedule"
        };
        public object Convert(object value, Type targetType, object parameter, CultureInfo culture)
            => value is string s && kCrit.Contains(s) ? (object)ThemeRes.Res("RowCriticalBrush", Crit) : Norm;
        public object ConvertBack(object value, Type targetType, object parameter, CultureInfo culture)
            => Binding.DoNothing;
    }
}
