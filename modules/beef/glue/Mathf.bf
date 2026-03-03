// Godot math helper, ported from modules/mono/glue/GodotSharp/GodotSharp/Core/Mathf*.cs.
// Hand-written (not generated); the bindings generator copies this into GodotBindings/src on editor
// start. real_t == float in standard Godot builds (double-precision builds are not handled here yet).
using System;

namespace Godot
{
	public static class Mathf
	{
		public const float Tau = 6.2831853071795864769252867666f;
		public const float Pi = 3.1415926535897932384626433833f;
		public const float E = 2.7182818284590452353602874714f;
		public const float Sqrt2 = 1.4142135623730950488016887242f;
		public const float Epsilon = 1e-06f;
		public const float Inf = float.PositiveInfinity;
		public const float NaN = float.NaN;

		const float _degToRad = Pi / 180.0f;
		const float _radToDeg = 180.0f / Pi;

		// --- Forwards to System.Math ---
		public static float Abs(float s) => Math.Abs(s);
		public static int Abs(int s) => Math.Abs(s);
		public static int32 Abs(int32 s) => Math.Abs(s);
		public static float Acos(float s) => Math.Acos(s);
		public static float Asin(float s) => Math.Asin(s);
		public static float Atan(float s) => Math.Atan(s);
		public static float Atan2(float y, float x) => Math.Atan2(y, x);
		public static float Ceil(float s) => Math.Ceiling(s);
		public static int CeilToInt(float s) => (int)Math.Ceiling(s);
		public static float Cos(float s) => Math.Cos(s);
		public static float Cosh(float s) => Math.Cosh(s);
		public static float Exp(float s) => Math.Exp(s);
		public static float Floor(float s) => Math.Floor(s);
		public static int FloorToInt(float s) => (int)Math.Floor(s);
		public static float Log(float s) => Math.Log(s);
		public static float Pow(float x, float y) => Math.Pow(x, y);
		public static float Round(float s) => Math.Round(s);
		public static int RoundToInt(float s) => (int)Math.Round(s);
		public static float Sin(float s) => Math.Sin(s);
		public static float Sinh(float s) => Math.Sinh(s);
		public static float Sqrt(float s) => Math.Sqrt(s);
		public static float Tan(float s) => Math.Tan(s);
		public static float Tanh(float s) => Math.Tanh(s);
		public static float Clamp(float v, float min, float max) => Math.Clamp(v, min, max);
		public static int Clamp(int v, int min, int max) => Math.Clamp(v, min, max);
		public static int32 Clamp(int32 v, int32 min, int32 max) => Math.Clamp(v, min, max);
		public static float Max(float a, float b) => Math.Max(a, b);
		public static int Max(int a, int b) => Math.Max(a, b);
		public static int32 Max(int32 a, int32 b) => Math.Max(a, b);
		public static float Min(float a, float b) => Math.Min(a, b);
		public static int Min(int a, int b) => Math.Min(a, b);
		public static int32 Min(int32 a, int32 b) => Math.Min(a, b);
		public static int Sign(int s) => (s == 0) ? 0 : (s < 0 ? -1 : 1);
		public static int32 Sign(int32 s) => (s == 0) ? 0 : (s < 0 ? -1 : 1);
		public static int Sign(float s) => (s == 0) ? 0 : (s < 0 ? -1 : 1);
		public static bool IsFinite(float s) => s.IsFinite;
		public static bool IsInf(float s) => s.IsInfinity;
		public static bool IsNaN(float s) => s.IsNaN;
		public static float DegToRad(float deg) => deg * _degToRad;
		public static float RadToDeg(float rad) => rad * _radToDeg;
		public static (float Sin, float Cos) SinCos(float s) => (Math.Sin(s), Math.Cos(s));

		// --- Custom logic (ported) ---
		public static float Lerp(float from, float to, float weight) => from + (to - from) * weight;
		public static float InverseLerp(float from, float to, float weight) => (weight - from) / (to - from);
		public static float Remap(float value, float inFrom, float inTo, float outFrom, float outTo) =>
			Lerp(outFrom, outTo, InverseLerp(inFrom, inTo, value));

		public static float LerpAngle(float from, float to, float weight)
		{
			float difference = (to - from) % Tau;
			float distance = ((2 * difference) % Tau) - difference;
			return from + distance * weight;
		}

		public static bool IsEqualApprox(float a, float b)
		{
			if (a == b)
				return true;
			float tolerance = Epsilon * Abs(a);
			if (tolerance < Epsilon)
				tolerance = Epsilon;
			return Abs(a - b) < tolerance;
		}

		public static bool IsEqualApprox(float a, float b, float tolerance) => (a == b) || (Abs(a - b) < tolerance);
		public static bool IsZeroApprox(float s) => Abs(s) < Epsilon;

		public static int PosMod(int a, int b)
		{
			int value = a % b;
			if ((value < 0 && b > 0) || (value > 0 && b < 0))
				value += b;
			return value;
		}

		public static float PosMod(float a, float b)
		{
			float value = a % b;
			if ((value < 0 && b > 0) || (value > 0 && b < 0))
				value += b;
			return value;
		}

		public static int Wrap(int value, int min, int max)
		{
			int range = max - min;
			return range == 0 ? min : min + ((((value - min) % range) + range) % range);
		}

		public static float Wrap(float value, float min, float max)
		{
			float range = max - min;
			return IsZeroApprox(range) ? min : value - (range * Floor((value - min) / range));
		}

		public static float Snapped(float s, float step)
		{
			if (step != 0f)
				return Floor((s / step) + 0.5f) * step;
			return s;
		}

		public static float MoveToward(float from, float to, float delta)
		{
			if (Abs(to - from) <= delta)
				return to;
			return from + Sign(to - from) * delta;
		}

		public static float SmoothStep(float from, float to, float weight)
		{
			if (IsEqualApprox(from, to))
				return from;
			float x = Clamp((weight - from) / (to - from), 0.0f, 1.0f);
			return x * x * (3 - 2 * x);
		}

		public static float CubicInterpolate(float from, float to, float pre, float post, float weight) =>
			0.5f * ((from * 2.0f) +
				(-pre + to) * weight +
				(2.0f * pre - 5.0f * from + 4.0f * to - post) * (weight * weight) +
				(-pre + 3.0f * from - 3.0f * to + post) * (weight * weight * weight));

		public static float CubicInterpolateInTime(float from, float to, float pre, float post, float weight, float toT, float preT, float postT)
		{
			float t = Lerp(0.0f, toT, weight);
			float a1 = (preT == 0f) ? from : Lerp(pre, from, (t - preT) / -preT);
			float a2 = (toT == 0f) ? from : Lerp(from, to, t / toT);
			float a3 = (postT - toT == 0f) ? to : Lerp(to, post, (t - toT) / (postT - toT));
			float b1 = (postT == 0f) ? a1 : Lerp(a1, a2, (t - preT) / (toT - preT));
			float b2 = (postT - preT == 0f) ? a2 : Lerp(a2, a3, t / (postT - preT));
			return (toT == 0f) ? a1 : Lerp(b1, b2, (t) / (toT));
		}

		public static float BezierInterpolate(float start, float control1, float control2, float end, float t)
		{
			float omt = 1 - t;
			float omt2 = omt * omt;
			float omt3 = omt2 * omt;
			float t2 = t * t;
			float t3 = t2 * t;
			return start * omt3 + control1 * omt2 * t * 3 + control2 * omt * t2 * 3 + end * t3;
		}

		public static float BezierDerivative(float start, float control1, float control2, float end, float t)
		{
			float omt = 1 - t;
			float omt2 = omt * omt;
			float t2 = t * t;
			return (control1 - start) * 3 * omt2 + (control2 - control1) * 6 * omt * t + (end - control2) * 3 * t2;
		}
	}
}
