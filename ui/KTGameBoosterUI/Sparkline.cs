using System;
using System.Collections.Generic;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using System.Windows.Shapes;

namespace KTGameBooster
{
    public class Sparkline : FrameworkElement
    {
        public static readonly DependencyProperty ValuesProperty =
            DependencyProperty.Register("Values", typeof(double[]), typeof(Sparkline),
                new FrameworkPropertyMetadata(new double[0],
                    FrameworkPropertyMetadataOptions.AffectsRender,
                    (d, e) => ((Sparkline)d).OnChanged()));
        public static readonly DependencyProperty StrokeProperty =
            DependencyProperty.Register("Stroke", typeof(Brush), typeof(Sparkline),
                new FrameworkPropertyMetadata(Brushes.SteelBlue,
                    FrameworkPropertyMetadataOptions.AffectsRender));
        public static readonly DependencyProperty FillProperty =
            DependencyProperty.Register("Fill", typeof(Brush), typeof(Sparkline),
                new FrameworkPropertyMetadata(new SolidColorBrush(Color.FromArgb(0x40, 0x5B, 0x8C, 0xFF)),
                    FrameworkPropertyMetadataOptions.AffectsRender));

        public double[] Values
        {
            get { return (double[])GetValue(ValuesProperty); }
            set { SetValue(ValuesProperty, value ?? new double[0]); }
        }
        public Brush Stroke { get { return (Brush)GetValue(StrokeProperty); } set { SetValue(StrokeProperty, value); } }
        public Brush Fill   { get { return (Brush)GetValue(FillProperty);   } set { SetValue(FillProperty, value); } }

        private void OnChanged() { InvalidateVisual(); }

        protected override void OnRender(DrawingContext dc)
        {
            double w = ActualWidth, h = ActualHeight;
            if (w <= 0 || h <= 0) return;
            var vals = Values;
            if (vals == null || vals.Length == 0)
            {
                var gridBrush = Application.Current?.TryFindResource("GridLineBrush") as Brush
                    ?? (Brush)new SolidColorBrush(Color.FromArgb(0x40, 0xFF, 0xFF, 0xFF));
                var pen = new Pen(gridBrush, 1);
                if (pen.CanFreeze) pen.Freeze();
                dc.DrawLine(pen, new Point(0, h - 1), new Point(w, h - 1));
                return;
            }

            double max = 1.0;
            for (int i = 0; i < vals.Length; i++) if (vals[i] > max) max = vals[i];
            if (max <= 0) max = 1.0;

            int n = vals.Length;
            double step = w / Math.Max(1, n - 1);
            var pts = new List<Point>(n);
            for (int i = 0; i < n; i++)
            {
                double x = i * step;
                double y = h - (Math.Max(0, Math.Min(1.0, vals[i] / max)) * h);
                pts.Add(new Point(x, y));
            }

            var area = new PathGeometry();
            var fig = new PathFigure { StartPoint = new Point(pts[0].X, h), IsClosed = true };
            fig.Segments.Add(new LineSegment(pts[0], false));
            for (int i = 1; i < pts.Count; i++) fig.Segments.Add(new LineSegment(pts[i], true));
            fig.Segments.Add(new LineSegment(new Point(pts[pts.Count - 1].X, h), false));
            area.Figures.Add(fig);
            area.Freeze();

            var stroke = Stroke;
            if (stroke == null || stroke == Brushes.SteelBlue)
                stroke = Application.Current?.TryFindResource("AccentBrush") as Brush ?? Brushes.SteelBlue;

            var strokeBrush = stroke as SolidColorBrush;
            Color strokeColor = strokeBrush != null ? strokeBrush.Color : Color.FromRgb(0x5B, 0x8C, 0xFF);
            var fillBrush = new LinearGradientBrush
            {
                StartPoint = new Point(0, 0),
                EndPoint = new Point(0, 1),
                GradientStops =
                {
                    new GradientStop(Color.FromArgb(0x60, strokeColor.R, strokeColor.G, strokeColor.B), 0),
                    new GradientStop(Color.FromArgb(0x10, strokeColor.R, strokeColor.G, strokeColor.B), 1),
                }
            };
            fillBrush.Freeze();
            dc.DrawGeometry(fillBrush, null, area);

            var strokeGeom = new StreamGeometry();
            using (var ctx = strokeGeom.Open())
            {
                ctx.BeginFigure(pts[0], false, false);
                for (int i = 1; i < pts.Count; i++) ctx.LineTo(pts[i], true, true);
            }
            strokeGeom.Freeze();
            var linePen = new Pen(stroke, 1.6);
            linePen.LineJoin = PenLineJoin.Round;
            linePen.Freeze();
            dc.DrawGeometry(null, linePen, strokeGeom);

            if (pts.Count > 0)
            {
                var last = pts[pts.Count - 1];
                var dot = new EllipseGeometry(last, 2.5, 2.5);
                dot.Freeze();
                dc.DrawGeometry(stroke, null, dot);
            }
        }
    }
}
