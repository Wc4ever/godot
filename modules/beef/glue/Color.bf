// Godot Color math, ported from modules/mono/glue/GodotSharp/GodotSharp/Core/Color.cs.
// Extension over the [CRepr] layout stub (float R, G, B, A) in GodotPrimitives.bf.
// TODO: the ~150-entry named-color table (and FromString / named Color(string)) plus OkHSL
// (FromOkHsl + OkHslH/S/L) are not ported — large, niche, and string/perceptual. The HTML hex
// parsing/formatting below covers Color("#rrggbb") usage.
using System;

namespace Godot
{
	extension Color
	{
		public int32 R8
		{
			get => (int32)Math.Round(R * 255.0f);
			set mut => R = value / 255.0f;
		}
		public int32 G8
		{
			get => (int32)Math.Round(G * 255.0f);
			set mut => G = value / 255.0f;
		}
		public int32 B8
		{
			get => (int32)Math.Round(B * 255.0f);
			set mut => B = value / 255.0f;
		}
		public int32 A8
		{
			get => (int32)Math.Round(A * 255.0f);
			set mut => A = value / 255.0f;
		}

		public float H
		{
			get { float h; float s; float v; ToHsv(out h, out s, out v); return h; }
			set mut { float h; float s; float v; ToHsv(out h, out s, out v); this = FromHsv(value, s, v, A); }
		}
		public float S
		{
			get { float h; float s; float v; ToHsv(out h, out s, out v); return s; }
			set mut { float h; float s; float v; ToHsv(out h, out s, out v); this = FromHsv(h, value, v, A); }
		}
		public float V
		{
			get { float h; float s; float v; ToHsv(out h, out s, out v); return v; }
			set mut { float h; float s; float v; ToHsv(out h, out s, out v); this = FromHsv(h, s, value, A); }
		}

		public float Luminance => 0.2126f * R + 0.7152f * G + 0.0722f * B;

		public this(float r, float g, float b, float a = 1.0f)
		{
			R = r;
			G = g;
			B = b;
			A = a;
		}
		public this(Color c, float a = 1.0f)
		{
			R = c.R;
			G = c.G;
			B = c.B;
			A = a;
		}
		public this(uint32 rgba)
		{
			A = (rgba & 0xFF) / 255.0f;
			uint32 v = rgba >> 8;
			B = (v & 0xFF) / 255.0f;
			v >>= 8;
			G = (v & 0xFF) / 255.0f;
			v >>= 8;
			R = (v & 0xFF) / 255.0f;
		}
		public this(uint64 rgba)
		{
			A = (rgba & 0xFFFF) / 65535.0f;
			uint64 v = rgba >> 16;
			B = (v & 0xFFFF) / 65535.0f;
			v >>= 16;
			G = (v & 0xFFFF) / 65535.0f;
			v >>= 16;
			R = (v & 0xFFFF) / 65535.0f;
		}
		public this(StringView code)
		{
			this = FromHtml(code);
		}

		public float this[int index]
		{
			get { switch (index) { case 0: return R; case 1: return G; case 2: return B; default: return A; } }
			set mut { switch (index) { case 0: R = value; case 1: G = value; case 2: B = value; case 3: A = value; } }
		}

		public static Color Color8(uint8 r8, uint8 g8, uint8 b8, uint8 a8 = 255) =>
			.(r8 / 255.0f, g8 / 255.0f, b8 / 255.0f, a8 / 255.0f);

		public Color Inverted() => .(1.0f - R, 1.0f - G, 1.0f - B, A);
		public Color Lerp(Color to, float weight) => .(Mathf.Lerp(R, to.R, weight), Mathf.Lerp(G, to.G, weight), Mathf.Lerp(B, to.B, weight), Mathf.Lerp(A, to.A, weight));

		public Color Darkened(float amount)
		{
			Color res = this;
			res.R *= 1.0f - amount;
			res.G *= 1.0f - amount;
			res.B *= 1.0f - amount;
			return res;
		}

		public Color Lightened(float amount)
		{
			Color res = this;
			res.R += (1.0f - res.R) * amount;
			res.G += (1.0f - res.G) * amount;
			res.B += (1.0f - res.B) * amount;
			return res;
		}

		public Color Blend(Color over)
		{
			Color res = ?;
			float sa = 1.0f - over.A;
			res.A = (A * sa) + over.A;
			if (res.A == 0)
				return .(0, 0, 0, 0);
			res.R = ((R * A * sa) + (over.R * over.A)) / res.A;
			res.G = ((G * A * sa) + (over.G * over.A)) / res.A;
			res.B = ((B * A * sa) + (over.B * over.A)) / res.A;
			return res;
		}

		public Color Clamp(Color? min = null, Color? max = null)
		{
			Color cMin = min ?? .(0, 0, 0, 0);
			Color cMax = max ?? .(1, 1, 1, 1);
			return .(
				Mathf.Clamp(R, cMin.R, cMax.R),
				Mathf.Clamp(G, cMin.G, cMax.G),
				Mathf.Clamp(B, cMin.B, cMax.B),
				Mathf.Clamp(A, cMin.A, cMax.A));
		}

		public Color LinearToSrgb() => .(
			R < 0.0031308f ? 12.92f * R : (1.0f + 0.055f) * Mathf.Pow(R, 1.0f / 2.4f) - 0.055f,
			G < 0.0031308f ? 12.92f * G : (1.0f + 0.055f) * Mathf.Pow(G, 1.0f / 2.4f) - 0.055f,
			B < 0.0031308f ? 12.92f * B : (1.0f + 0.055f) * Mathf.Pow(B, 1.0f / 2.4f) - 0.055f, A);

		public Color SrgbToLinear() => .(
			R < 0.04045f ? R * (1.0f / 12.92f) : Mathf.Pow((R + 0.055f) * (1.0f / 1.055f), 2.4f),
			G < 0.04045f ? G * (1.0f / 12.92f) : Mathf.Pow((G + 0.055f) * (1.0f / 1.055f), 2.4f),
			B < 0.04045f ? B * (1.0f / 12.92f) : Mathf.Pow((B + 0.055f) * (1.0f / 1.055f), 2.4f), A);

		public void ToHsv(out float hue, out float saturation, out float value)
		{
			float max = Mathf.Max(R, Mathf.Max(G, B));
			float min = Mathf.Min(R, Mathf.Min(G, B));
			float delta = max - min;
			if (delta == 0)
			{
				hue = 0;
			}
			else
			{
				if (R == max)
					hue = (G - B) / delta;
				else if (G == max)
					hue = 2 + ((B - R) / delta);
				else
					hue = 4 + ((R - G) / delta);
				hue /= 6.0f;
				if (hue < 0)
					hue += 1.0f;
			}
			saturation = (max == 0) ? 0 : 1 - (min / max);
			value = max;
		}

		public static Color FromHsv(float hue, float saturation, float value, float alpha = 1.0f)
		{
			if (saturation == 0)
				return .(value, value, value, alpha);
			float h = (hue * 6.0f) % 6f;
			int i = (int)h;
			float f = h - i;
			float p = value * (1 - saturation);
			float q = value * (1 - (saturation * f));
			float t = value * (1 - (saturation * (1 - f)));
			switch (i)
			{
			case 0: return .(value, t, p, alpha);
			case 1: return .(q, value, p, alpha);
			case 2: return .(p, value, t, alpha);
			case 3: return .(p, q, value, alpha);
			case 4: return .(t, p, value, alpha);
			default: return .(value, p, q, alpha);
			}
		}

		public uint32 ToRgba32()
		{
			uint32 c = (uint8)Math.Round(R * 255);
			c <<= 8; c |= (uint8)Math.Round(G * 255);
			c <<= 8; c |= (uint8)Math.Round(B * 255);
			c <<= 8; c |= (uint8)Math.Round(A * 255);
			return c;
		}

		public uint32 ToArgb32()
		{
			uint32 c = (uint8)Math.Round(A * 255);
			c <<= 8; c |= (uint8)Math.Round(R * 255);
			c <<= 8; c |= (uint8)Math.Round(G * 255);
			c <<= 8; c |= (uint8)Math.Round(B * 255);
			return c;
		}

		public uint32 ToAbgr32()
		{
			uint32 c = (uint8)Math.Round(A * 255);
			c <<= 8; c |= (uint8)Math.Round(B * 255);
			c <<= 8; c |= (uint8)Math.Round(G * 255);
			c <<= 8; c |= (uint8)Math.Round(R * 255);
			return c;
		}

		public static Color FromRgbe9995(uint32 rgbe)
		{
			float r = rgbe & 0x1ff;
			float g = (rgbe >> 9) & 0x1ff;
			float b = (rgbe >> 18) & 0x1ff;
			float e = rgbe >> 27;
			float m = Mathf.Pow(2.0f, e - 15.0f - 9.0f);
			return .(r * m, g * m, b * m, 1.0f);
		}

		static int ParseCol4(StringView s, int index)
		{
			char8 c = s[index];
			if (c >= '0' && c <= '9') return (int)c - (int)'0';
			if (c >= 'a' && c <= 'f') return (int)c + (10 - (int)'a');
			if (c >= 'A' && c <= 'F') return (int)c + (10 - (int)'A');
			return -1;
		}
		static int ParseCol8(StringView s, int index) => ParseCol4(s, index) * 16 + ParseCol4(s, index + 1);

		public static bool HtmlIsValid(StringView color)
		{
			StringView c = color;
			if (c.IsEmpty)
				return false;
			if (c[0] == '#')
				c = c.Substring(1);
			int len = c.Length;
			if (!(len == 3 || len == 4 || len == 6 || len == 8))
				return false;
			for (int i = 0; i < len; i++)
				if (ParseCol4(c, i) == -1)
					return false;
			return true;
		}

		public static Color FromHtml(StringView rgba)
		{
			StringView s = rgba;
			if (s.IsEmpty)
				return .(0, 0, 0, 1);
			if (s[0] == '#')
				s = s.Substring(1);
			bool isShorthand = s.Length < 5;
			bool alpha;
			switch (s.Length)
			{
			case 8: alpha = true;
			case 6: alpha = false;
			case 4: alpha = true;
			case 3: alpha = false;
			default: return .(0, 0, 0, 1);
			}
			Color c = .(0, 0, 0, 1);
			if (isShorthand)
			{
				c.R = ParseCol4(s, 0) / 15f;
				c.G = ParseCol4(s, 1) / 15f;
				c.B = ParseCol4(s, 2) / 15f;
				if (alpha)
					c.A = ParseCol4(s, 3) / 15f;
			}
			else
			{
				c.R = ParseCol8(s, 0) / 255f;
				c.G = ParseCol8(s, 2) / 255f;
				c.B = ParseCol8(s, 4) / 255f;
				if (alpha)
					c.A = ParseCol8(s, 6) / 255f;
			}
			return c;
		}

		public void ToHtml(String buffer, bool includeAlpha = true)
		{
			const String hexDigits = "0123456789abcdef";
			void AppendHex(float val)
			{
				int b = Mathf.RoundToInt(Mathf.Clamp(val * 255, 0, 255));
				buffer.Append(hexDigits[(b >> 4) & 0xF]);
				buffer.Append(hexDigits[b & 0xF]);
			}
			AppendHex(R);
			AppendHex(G);
			AppendHex(B);
			if (includeAlpha)
				AppendHex(A);
		}

		public bool IsEqualApprox(Color other) => Mathf.IsEqualApprox(R, other.R) && Mathf.IsEqualApprox(G, other.G) && Mathf.IsEqualApprox(B, other.B) && Mathf.IsEqualApprox(A, other.A);
		public bool Equals(Color other) => R == other.R && G == other.G && B == other.B && A == other.A;

		public static Color operator +(Color a, Color b) => .(a.R + b.R, a.G + b.G, a.B + b.B, a.A + b.A);
		public static Color operator -(Color a, Color b) => .(a.R - b.R, a.G - b.G, a.B - b.B, a.A - b.A);
		public static Color operator -(Color c) => .(1.0f - c.R, 1.0f - c.G, 1.0f - c.B, 1.0f - c.A);
		public static Color operator *(Color c, float s) => .(c.R * s, c.G * s, c.B * s, c.A * s);
		public static Color operator *(float s, Color c) => .(c.R * s, c.G * s, c.B * s, c.A * s);
		public static Color operator *(Color a, Color b) => .(a.R * b.R, a.G * b.G, a.B * b.B, a.A * b.A);
		public static Color operator /(Color c, float s) => .(c.R / s, c.G / s, c.B / s, c.A / s);
		public static Color operator /(Color a, Color b) => .(a.R / b.R, a.G / b.G, a.B / b.B, a.A / b.A);
		public static bool operator ==(Color a, Color b) => a.R == b.R && a.G == b.G && a.B == b.B && a.A == b.A;
	}
}
