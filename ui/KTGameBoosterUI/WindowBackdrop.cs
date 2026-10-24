using System;
using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Interop;
using System.Windows.Media;

namespace KTGameBooster
{
    public static class WindowBackdrop
    {
        public enum Style
        {
            Solid = 0,
            Mica = 1,
            Acrylic = 2,
        }

        private const int DWMWA_USE_IMMERSIVE_DARK_MODE = 20;
        private const int DWMWA_SYSTEMBACKDROP_TYPE      = 38;
        private const int DWMWA_MICA_EFFECT              = 1029;
        private const int DWMWA_WINDOW_CORNER_PREFERENCE = 33;
        private const int DWMWA_BORDER_COLOR             = 34;
        private const int DWMWA_CAPTION_COLOR            = 35;
        private const int DWMWA_TEXT_COLOR               = 36;

        private const int DWMSBT_AUTO   = 0;
        private const int DWMSBT_NONE   = 1;
        private const int DWMSBT_MAIN   = 2;
        private const int DWMSBT_TAB    = 4;
        private const int DWMSBT_ACRYLIC= 3;

        [DllImport("dwmapi.dll")]
        private static extern int DwmSetWindowAttribute(IntPtr hwnd, int attr, ref int attrValue, int attrSize);

        [DllImport("dwmapi.dll")]
        private static extern int DwmGetWindowAttribute(IntPtr hwnd, int attr, out int attrValue, int attrSize);

        [DllImport("dwmapi.dll", PreserveSig = true)]
        private static extern int DwmExtendFrameIntoClientArea(IntPtr hWnd, ref MARGINS pMarInset);

        [DllImport("dwmapi.dll", PreserveSig = true)]
        private static extern int DwmEnableBlurBehindWindow(IntPtr hWnd, ref DwmBlurBehind blurBehind);

        [DllImport("user32.dll")]
        private static extern bool SetWindowCompositionAttribute(IntPtr hwnd, ref WindowCompositionAttributeData data);

        [StructLayout(LayoutKind.Sequential)]
        private struct MARGINS
        {
            public int cxLeftWidth;
            public int cxRightWidth;
            public int cyTopHeight;
            public int cyBottomHeight;
        }

        [StructLayout(LayoutKind.Sequential)]
        private struct WindowCompositionAttributeData
        {
            public int Attribute;
            public IntPtr Data;
            public int SizeOfData;
        }

        private enum AccentState
        {
            Disabled = 0,
            EnableGradient = 1,
            TransparentGradient = 2,
            EnableBlurBehind = 3,
            AcrylicBlurBehind = 6,
        }

        [StructLayout(LayoutKind.Sequential)]
        private struct AccentPolicy
        {
            public AccentState AccentState;
            public int AccentFlags;
            public int GradientColor;
            public int AnimationId;
        }

        public static string ActiveMaterial { get; private set; } = "Sólido";
        public static bool IsMaterialActive { get; private set; }

        public static void StyleChrome(Window window)
        {
            if (window == null) return;
            var helper = new WindowInteropHelper(window);
            IntPtr hwnd = helper.Handle;
            if (hwnd == IntPtr.Zero) return;

            int corner = 1;
            DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, ref corner, sizeof(int));

            int defaultColor = unchecked((int)0xFFFFFFFF);
            int border = unchecked((int)0x006E4A38);
            DwmSetWindowAttribute(hwnd, DWMWA_BORDER_COLOR, ref border, sizeof(int));
            DwmSetWindowAttribute(hwnd, DWMWA_CAPTION_COLOR, ref defaultColor, sizeof(int));
            DwmSetWindowAttribute(hwnd, DWMWA_TEXT_COLOR, ref defaultColor, sizeof(int));
        }

        [StructLayout(LayoutKind.Sequential)]
        private struct DwmBlurBehind
        {
            public uint Flags;
            public int Enable;
            public IntPtr BlurRegion;
            public int TransitionOnMaximized;
        }

        public static bool Apply(Window window, Style style)
        {
            if (window == null) return false;
            var helper = new WindowInteropHelper(window);
            IntPtr hwnd = helper.Handle;
            if (hwnd == IntPtr.Zero) return false;

            var source = HwndSource.FromHwnd(hwnd);
            if (source != null) source.CompositionTarget.BackgroundColor = Colors.Transparent;
            window.Background = Brushes.Transparent;

            if (window.AllowsTransparency)
            {
                bool blurred = style != Style.Solid &&
                    TryAccent(hwnd, AccentState.AcrylicBlurBehind, unchecked((int)0x68181412));
                ActiveMaterial = blurred ? "Acrylic + alpha" : "Transparente";
                IsMaterialActive = blurred;
                return blurred;
            }

            var fullFrame = new MARGINS { cxLeftWidth = -1, cxRightWidth = -1, cyTopHeight = -1, cyBottomHeight = -1 };
            DwmExtendFrameIntoClientArea(hwnd, ref fullFrame);

            int useDark = 1;
            DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, ref useDark, sizeof(int));

            bool isWin11 = Environment.OSVersion.Version >= new Version(10, 0, 22000);
            bool applied = false;
            string appliedName = "Sólido";
            if (isWin11)
            {
                int backdrop = style switch
                {
                    Style.Mica    => DWMSBT_MAIN,
                    Style.Acrylic => DWMSBT_ACRYLIC,
                    _             => DWMSBT_NONE,
                };
                int hr = DwmSetWindowAttribute(hwnd, DWMWA_SYSTEMBACKDROP_TYPE, ref backdrop, sizeof(int));
                if (style == Style.Mica)
                {
                    int mica = 1;
                    if (hr != 0) hr = DwmSetWindowAttribute(hwnd, DWMWA_MICA_EFFECT, ref mica, sizeof(int));
                }
                int verified;
                applied = hr == 0 && (DwmGetWindowAttribute(hwnd, DWMWA_SYSTEMBACKDROP_TYPE, out verified, sizeof(int)) != 0 || verified == backdrop);
                if (applied) appliedName = style == Style.Mica ? "Mica" : "Acrylic";
            }

            if (!applied && style != Style.Solid)
            {
                applied = TryAccent(hwnd, AccentState.AcrylicBlurBehind, unchecked((int)0xA8181412));
                if (applied) appliedName = "Acrylic";
            }
            if (!applied && style != Style.Solid)
            {
                applied = TryAccent(hwnd, AccentState.EnableBlurBehind, unchecked((int)0x78181412));
                if (applied) appliedName = "Blur";
            }
            if (!applied && style != Style.Solid)
            {
                var blur = new DwmBlurBehind
                {
                    Flags = 0x1 | 0x4,
                    Enable = 1,
                    BlurRegion = IntPtr.Zero,
                    TransitionOnMaximized = 1,
                };
                applied = DwmEnableBlurBehindWindow(hwnd, ref blur) == 0;
                if (applied) appliedName = "Blur";
            }

            ActiveMaterial = appliedName;
            IsMaterialActive = applied;
            return applied;
        }

        private static bool TryAccent(IntPtr hwnd, AccentState state, int gradientColor)
        {
            var accent = new AccentPolicy
            {
                AccentState = state,
                AccentFlags = 2,
                GradientColor = gradientColor,
            };
            int dataSize = Marshal.SizeOf<AccentPolicy>();
            var data = new WindowCompositionAttributeData
            {
                Attribute = 19,
                SizeOfData = dataSize,
                Data = Marshal.AllocHGlobal(dataSize),
            };
            try
            {
                Marshal.StructureToPtr(accent, data.Data, false);
                return SetWindowCompositionAttribute(hwnd, ref data);
            }
            finally
            {
                Marshal.FreeHGlobal(data.Data);
            }
        }
    }
}
