using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.Drawing.Text;
using System.Globalization;
using System.IO;
using System.Text;

internal static class GenUiFont
{
    private const int FontPx = 16;
    private const int Canvas = 48;
    private const int CursorX = 8;

    private static void Main()
    {
        string mainDir = Directory.GetCurrentDirectory();
        if (!File.Exists(Path.Combine(mainDir, "ui_usb_mode.c")))
        {
            mainDir = @"e:\ESP32Code\ESP32_EBadge\main";
        }
        string outPath = Path.Combine(mainDir, "ui_font_zh_16.c");
        var cps = CollectCodepoints(File.ReadAllText(Path.Combine(mainDir, "ui_usb_mode.c"), Encoding.UTF8));
        cps.Sort();

        using (Font font = new Font("Microsoft YaHei", FontPx, FontStyle.Regular, GraphicsUnit.Pixel))
        {
            FontFamily ff = font.FontFamily;
            float em = ff.GetEmHeight(FontStyle.Regular);
            float ascent = ff.GetCellAscent(FontStyle.Regular) * FontPx / em;
            float descent = ff.GetCellDescent(FontStyle.Regular) * FontPx / em;
            int lineHeight = (int)Math.Ceiling(ascent + descent);
            int baseLine = (int)Math.Ceiling(descent);
            int baselineY = (int)Math.Ceiling(ascent) + 4;

            var glyphs = new List<Glyph>();
            using (Bitmap canvas = new Bitmap(Canvas, Canvas, PixelFormat.Format32bppArgb))
            using (Graphics g = Graphics.FromImage(canvas))
            using (StringFormat sf = (StringFormat)StringFormat.GenericTypographic.Clone())
            {
                sf.FormatFlags |= StringFormatFlags.MeasureTrailingSpaces | StringFormatFlags.NoWrap;
                g.TextRenderingHint = TextRenderingHint.AntiAlias;
                g.SmoothingMode = SmoothingMode.HighQuality;
                g.PixelOffsetMode = PixelOffsetMode.HighQuality;
                g.InterpolationMode = InterpolationMode.HighQualityBicubic;

                foreach (int cp in cps)
                {
                    glyphs.Add(RenderGlyph(canvas, g, sf, font, cp, baselineY, ascent));
                }
            }

            WriteC(outPath, glyphs, lineHeight, baseLine);
            Console.WriteLine("wrote {0} glyphs, line_height={1} base_line={2} -> {3}", glyphs.Count, lineHeight, baseLine, outPath);
        }
    }

    private static List<int> CollectCodepoints(string src)
    {
        var cps = new List<int>();
        for (int i = 0x20; i <= 0x7E; i++) cps.Add(i);
        foreach (char extra in "。，、？：；·")
        {
            if (!cps.Contains(extra)) cps.Add(extra);
        }
        for (int i = 0; i < src.Length; i++)
        {
            if (src[i] != '"') continue;
            i++;
            while (i < src.Length && src[i] != '"')
            {
                if (src[i] == '\\' && i + 1 < src.Length)
                {
                    i += 2;
                    continue;
                }
                int cp = src[i];
                if (cp > 0x7F && !cps.Contains(cp)) cps.Add(cp);
                i++;
            }
        }
        return cps;
    }

    private struct Glyph
    {
        public int Cp;
        public int AdvW;
        public int BoxW;
        public int BoxH;
        public int OfsX;
        public int OfsY;
        public byte[] Bitmap;
    }

    private static Glyph RenderGlyph(Bitmap canvas, Graphics g, StringFormat sf, Font font, int cp, int baselineY, float ascent)
    {
        string text = char.ConvertFromUtf32(cp);
        g.Clear(Color.Black);
        float drawY = baselineY - ascent;
        g.DrawString(text, font, Brushes.White, CursorX, drawY, sf);
        SizeF sz = g.MeasureString(text, font, PointF.Empty, sf);
        int advPx = Math.Max(1, (int)Math.Round(sz.Width));

        int minX = Canvas, minY = Canvas, maxX = -1, maxY = -1;
        for (int y = 0; y < Canvas; y++)
        {
            for (int x = 0; x < Canvas; x++)
            {
                Color c = canvas.GetPixel(x, y);
                int lum = Math.Max(c.R, Math.Max(c.G, c.B));
                if (lum <= 8) continue;
                if (x < minX) minX = x;
                if (y < minY) minY = y;
                if (x > maxX) maxX = x;
                if (y > maxY) maxY = y;
            }
        }

        Glyph gl = new Glyph();
        gl.Cp = cp;
        gl.AdvW = advPx * 16;
        if (gl.AdvW > 4095) gl.AdvW = 4095;

        if (maxX < 0)
        {
            gl.BoxW = 0;
            gl.BoxH = 0;
            gl.OfsX = 0;
            gl.OfsY = 0;
            gl.Bitmap = new byte[0];
            return gl;
        }

        int boxW = maxX - minX + 1;
        int boxH = maxY - minY + 1;
        if (boxW > 255) boxW = 255;
        if (boxH > 255) boxH = 255;
        gl.BoxW = boxW;
        gl.BoxH = boxH;
        gl.OfsX = ClampS8(minX - CursorX);
        gl.OfsY = ClampS8(baselineY - (maxY + 1));

        int pix = boxW * boxH;
        int nbytes = (pix + 1) / 2;
        byte[] bmp = new byte[nbytes];
        int pi = 0;
        for (int y = 0; y < boxH; y++)
        {
            for (int x = 0; x < boxW; x++)
            {
                Color c = canvas.GetPixel(minX + x, minY + y);
                int lum = Math.Max(c.R, Math.Max(c.G, c.B));
                int nibble = lum >> 4;
                if ((pi & 1) == 0) bmp[pi / 2] = (byte)(nibble << 4);
                else bmp[pi / 2] |= (byte)nibble;
                pi++;
            }
        }
        gl.Bitmap = bmp;
        return gl;
    }

    private static int ClampS8(int v)
    {
        if (v < -128) return -128;
        if (v > 127) return 127;
        return v;
    }

    private static void WriteC(string path, List<Glyph> glyphs, int lineHeight, int baseLine)
    {
        var sb = new StringBuilder();
        sb.AppendLine("#include \"lvgl.h\"");
        sb.AppendLine();
        sb.AppendLine("#ifndef UI_FONT_ZH_16");
        sb.AppendLine("#define UI_FONT_ZH_16 1");
        sb.AppendLine("#endif");
        sb.AppendLine();
        sb.AppendLine("#if UI_FONT_ZH_16");
        sb.AppendLine();
        sb.AppendLine("static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {");

        var dsc = new List<string>();
        dsc.Add("    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0} /* id = 0 reserved */");
        int bitmapIndex = 0;
        foreach (Glyph gl in glyphs)
        {
            sb.AppendFormat("    /* U+{0:X4} */\n", gl.Cp);
            if (gl.Bitmap.Length == 0)
            {
                sb.AppendLine("    /* empty */");
            }
            else
            {
                sb.Append("    ");
                for (int i = 0; i < gl.Bitmap.Length; i++)
                {
                    sb.AppendFormat("0x{0:x2}, ", gl.Bitmap[i]);
                    if ((i + 1) % 16 == 0 && i + 1 < gl.Bitmap.Length)
                    {
                        sb.Append("\n    ");
                    }
                }
                sb.AppendLine();
            }
            dsc.Add(string.Format(
                CultureInfo.InvariantCulture,
                "    {{.bitmap_index = {0}, .adv_w = {1}, .box_w = {2}, .box_h = {3}, .ofs_x = {4}, .ofs_y = {5}}}",
                bitmapIndex, gl.AdvW, gl.BoxW, gl.BoxH, gl.OfsX, gl.OfsY));
            bitmapIndex += gl.Bitmap.Length;
        }
        sb.AppendLine("};");
        sb.AppendLine();
        sb.AppendLine("static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {");
        sb.AppendLine(string.Join(",\n", dsc.ToArray()));
        sb.AppendLine("};");
        sb.AppendLine();

        var ascii = new List<Glyph>();
        var extra = new List<Glyph>();
        foreach (Glyph gl in glyphs)
        {
            if (gl.Cp >= 0x20 && gl.Cp <= 0x7F) ascii.Add(gl);
            else extra.Add(gl);
        }

        int glyphId = 1;
        var cmapBlocks = new List<string>();
        if (ascii.Count == 95 && ascii[0].Cp == 0x20 && ascii[94].Cp == 0x7E)
        {
            cmapBlocks.Add(
                "    {\n" +
                "        .range_start = 32, .range_length = 95, .glyph_id_start = 1,\n" +
                "        .unicode_list = NULL, .glyph_id_ofs_list = NULL, .list_length = 0,\n" +
                "        .type = LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY\n" +
                "    }");
            glyphId += 95;
        }
        else
        {
            throw new Exception("ASCII 0x20-0x7F must be complete for FORMAT0_TINY");
        }

        if (extra.Count > 0)
        {
            int rangeStart = extra[0].Cp;
            int rangeEnd = extra[extra.Count - 1].Cp;
            sb.AppendLine("static const uint16_t unicode_list_1[] = {");
            for (int i = 0; i < extra.Count; i++)
            {
                if (i % 8 == 0) sb.Append("    ");
                sb.AppendFormat("{0}, ", extra[i].Cp - rangeStart);
                if (i % 8 == 7) sb.AppendLine();
            }
            if (extra.Count % 8 != 0) sb.AppendLine();
            sb.AppendLine("};");
            sb.AppendLine();
            cmapBlocks.Add(string.Format(
                CultureInfo.InvariantCulture,
                "    {{\n" +
                "        .range_start = {0}, .range_length = {1}, .glyph_id_start = {2},\n" +
                "        .unicode_list = unicode_list_1, .glyph_id_ofs_list = NULL, .list_length = {3},\n" +
                "        .type = LV_FONT_FMT_TXT_CMAP_SPARSE_TINY\n" +
                "    }}",
                rangeStart, rangeEnd - rangeStart + 1, glyphId, extra.Count));
        }

        sb.AppendLine("static const lv_font_fmt_txt_cmap_t cmaps[] = {");
        sb.AppendLine(string.Join(",\n", cmapBlocks.ToArray()));
        sb.AppendLine("};");
        sb.AppendLine();
        sb.AppendLine("static const lv_font_fmt_txt_dsc_t font_dsc = {");
        sb.AppendLine("    .glyph_bitmap = glyph_bitmap,");
        sb.AppendLine("    .glyph_dsc = glyph_dsc,");
        sb.AppendLine("    .cmaps = cmaps,");
        sb.AppendLine("    .kern_dsc = NULL,");
        sb.AppendLine("    .kern_scale = 0,");
        sb.AppendFormat("    .cmap_num = {0},\n", cmapBlocks.Count);
        sb.AppendLine("    .bpp = 4,");
        sb.AppendLine("    .kern_classes = 0,");
        sb.AppendLine("    .bitmap_format = 0,");
        sb.AppendLine("    .stride = 0");
        sb.AppendLine("};");
        sb.AppendLine();
        sb.AppendLine("const lv_font_t ui_font_zh_16 = {");
        sb.AppendLine("    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,");
        sb.AppendLine("    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,");
        sb.AppendFormat("    .line_height = {0},\n", lineHeight);
        sb.AppendFormat("    .base_line = {0},\n", baseLine);
        sb.AppendLine("    .subpx = LV_FONT_SUBPX_NONE,");
        sb.AppendLine("    .underline_position = -1,");
        sb.AppendLine("    .underline_thickness = 1,");
        sb.AppendLine("    .dsc = &font_dsc,");
        sb.AppendLine("    .fallback = &lv_font_source_han_sans_sc_16_cjk,");
        sb.AppendLine("    .user_data = NULL");
        sb.AppendLine("};");
        sb.AppendLine();
        sb.AppendLine("#endif");

        File.WriteAllText(path, sb.ToString(), new UTF8Encoding(false));
    }
}
