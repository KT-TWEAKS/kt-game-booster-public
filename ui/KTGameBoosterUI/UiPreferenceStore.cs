using System;
using System.Collections.Generic;
using System.Linq;
using Microsoft.Win32;

namespace KTGameBooster
{
    internal static class UiPreferenceStore
    {
        private const string RootPath = @"Software\KTGameBooster";
        private const string ActiveProfileValue = "ui_active_profile";

        internal static string LoadActiveProfile()
        {
            try
            {
                using (var key = Registry.CurrentUser.OpenSubKey(RootPath))
                    return key?.GetValue(ActiveProfileValue) as string ?? "normal";
            }
            catch
            {
                return "normal";
            }
        }

        internal static void SaveActiveProfile(string profile)
        {
            try
            {
                using (var key = Registry.CurrentUser.CreateSubKey(RootPath))
                    key?.SetValue(ActiveProfileValue, NormalizeProfile(profile), RegistryValueKind.String);
            }
            catch
            {
            }
        }

        internal static IDictionary<string, bool> LoadFeatureOverrides(
            string profile, IEnumerable<string> featureNames)
        {
            var result = new Dictionary<string, bool>(StringComparer.OrdinalIgnoreCase);
            if (featureNames == null) return result;

            try
            {
                using (var key = Registry.CurrentUser.OpenSubKey(ProfilePath(profile)))
                {
                    if (key == null) return result;
                    foreach (string name in featureNames.Distinct(StringComparer.OrdinalIgnoreCase))
                    {
                        if (string.IsNullOrWhiteSpace(name) || key.GetValue(name) == null) continue;
                        if (key.GetValueKind(name) != RegistryValueKind.DWord) continue;
                        result[name] = Convert.ToInt32(key.GetValue(name)) != 0;
                    }
                }
            }
            catch
            {
            }

            return result;
        }

        internal static void SaveFeatureOverride(string profile, string feature, bool enabled)
        {
            if (string.IsNullOrWhiteSpace(feature)) return;
            try
            {
                using (var key = Registry.CurrentUser.CreateSubKey(ProfilePath(profile)))
                    key?.SetValue(feature, enabled ? 1 : 0, RegistryValueKind.DWord);
            }
            catch
            {
            }
        }

        private static string ProfilePath(string profile)
        {
            return RootPath + @"\Profiles\" + NormalizeProfile(profile);
        }

        private static string NormalizeProfile(string profile)
        {
            if (string.IsNullOrWhiteSpace(profile)) return "normal";
            var chars = profile.Trim().Select(c =>
                char.IsLetterOrDigit(c) || c == '_' || c == '-' ? c : '_').ToArray();
            return chars.Length == 0 ? "normal" : new string(chars);
        }
    }
}
