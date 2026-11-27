using System;
using System.Windows;
using Microsoft.Win32;

namespace KTGameBooster
{
    public enum AppTheme
    {
        Black,
        Dark,
        Light
    }

    public static class ThemeManager
    {
        private const string RegPath = @"Software\KTGameBooster";
        private const string RegValue = "ui_theme";

        public static AppTheme Current { get; private set; } = AppTheme.Dark;

        public static event Action<AppTheme> ThemeChanged;

        public static void Apply(AppTheme theme)
        {
            var app = Application.Current;
            if (app != null)
            {
                var dicts = app.Resources.MergedDictionaries;
                var newDict = new ResourceDictionary
                {
                    Source = new Uri("Themes/" + FileName(theme), UriKind.Relative)
                };

                var replaced = false;
                for (int i = 0; i < dicts.Count; i++)
                {
                    if (IsThemeDictionary(dicts[i]))
                    {
                        dicts[i] = newDict;
                        replaced = true;
                        break;
                    }
                }

                if (!replaced)
                    dicts.Insert(0, newDict);
            }

            Current = theme;
            Save(theme);
            ThemeChanged?.Invoke(theme);
        }

        public static AppTheme Load()
        {
            try
            {
                using (var key = Registry.CurrentUser.OpenSubKey(RegPath))
                {
                    var raw = key?.GetValue(RegValue) as string;
                    if (!string.IsNullOrEmpty(raw) &&
                        Enum.TryParse(raw, true, out AppTheme theme))
                        return theme;
                }
            }
            catch
            {
            }

            return AppTheme.Dark;
        }

        public static string NextLabel(AppTheme t)
        {
            switch (Next(t))
            {
                case AppTheme.Black:
                    return "Preto";
                case AppTheme.Light:
                    return "Claro";
                default:
                    return "Escuro";
            }
        }

        public static AppTheme Next(AppTheme t)
        {
            switch (t)
            {
                case AppTheme.Dark:
                    return AppTheme.Black;
                case AppTheme.Black:
                    return AppTheme.Light;
                default:
                    return AppTheme.Dark;
            }
        }

        private static string FileName(AppTheme theme)
        {
            switch (theme)
            {
                case AppTheme.Black:
                    return "Black.xaml";
                case AppTheme.Light:
                    return "Light.xaml";
                default:
                    return "Dark.xaml";
            }
        }

        private static bool IsThemeDictionary(ResourceDictionary dict)
        {
            var src = dict.Source?.OriginalString;
            if (string.IsNullOrEmpty(src))
                return false;

            return EndsWithTheme(src, "Dark.xaml")
                || EndsWithTheme(src, "Black.xaml")
                || EndsWithTheme(src, "Light.xaml");
        }

        private static bool EndsWithTheme(string source, string fileName)
        {
            return source.EndsWith("Themes\\" + fileName, StringComparison.OrdinalIgnoreCase)
                || source.EndsWith("Themes/" + fileName, StringComparison.OrdinalIgnoreCase);
        }

        private static void Save(AppTheme theme)
        {
            try
            {
                using (var key = Registry.CurrentUser.CreateSubKey(RegPath))
                {
                    key?.SetValue(RegValue, theme.ToString(), RegistryValueKind.String);
                }
            }
            catch
            {
            }
        }
    }
}
