// Godot Vector4 math, ported from modules/mono/glue/GodotSharp/GodotSharp/Core/Vector4.cs.
// Extension over the [CRepr] layout stub (X, Y, Z, W) in GodotPrimitives.bf.
using System;

namespace Godot
{
	extension Vector4
	{
		public this(float x, float y, float z, float w)
		{
			X = x;
			Y = y;
			Z = z;
			W = w;
		}

		public static Vector4 Zero => .(0, 0, 0, 0);
		public static Vector4 One => .(1, 1, 1, 1);
		public static Vector4 Inf => .(Mathf.Inf, Mathf.Inf, Mathf.Inf, Mathf.Inf);

		public float this[int index]
		{
			get { switch (index) { case 0: return X; case 1: return Y; case 2: return Z; default: return W; } }
			set mut { switch (index) { case 0: X = value; case 1: Y = value; case 2: Z = value; case 3: W = value; } }
		}

		public Vector4 Abs() => .(Mathf.Abs(X), Mathf.Abs(Y), Mathf.Abs(Z), Mathf.Abs(W));
		public Vector4 Ceil() => .(Mathf.Ceil(X), Mathf.Ceil(Y), Mathf.Ceil(Z), Mathf.Ceil(W));
		public Vector4 Floor() => .(Mathf.Floor(X), Mathf.Floor(Y), Mathf.Floor(Z), Mathf.Floor(W));
		public Vector4 Round() => .(Mathf.Round(X), Mathf.Round(Y), Mathf.Round(Z), Mathf.Round(W));
		public Vector4 Sign() => .(Mathf.Sign(X), Mathf.Sign(Y), Mathf.Sign(Z), Mathf.Sign(W));
		public Vector4 Clamp(Vector4 min, Vector4 max) => .(Mathf.Clamp(X, min.X, max.X), Mathf.Clamp(Y, min.Y, max.Y), Mathf.Clamp(Z, min.Z, max.Z), Mathf.Clamp(W, min.W, max.W));
		public Vector4 Clamp(float min, float max) => .(Mathf.Clamp(X, min, max), Mathf.Clamp(Y, min, max), Mathf.Clamp(Z, min, max), Mathf.Clamp(W, min, max));
		public float Dot(Vector4 with) => (X * with.X) + (Y * with.Y) + (Z * with.Z) + (W * with.W);
		public float DistanceTo(Vector4 to) => (to - this).Length();
		public float DistanceSquaredTo(Vector4 to) => (to - this).LengthSquared();
		public Vector4 DirectionTo(Vector4 to) => (to - this).Normalized();
		public float Length() => Mathf.Sqrt((X * X) + (Y * Y) + (Z * Z) + (W * W));
		public float LengthSquared() => (X * X) + (Y * Y) + (Z * Z) + (W * W);
		public bool IsFinite() => Mathf.IsFinite(X) && Mathf.IsFinite(Y) && Mathf.IsFinite(Z) && Mathf.IsFinite(W);
		public bool IsNormalized() => Mathf.Abs(LengthSquared() - 1.0f) < Mathf.Epsilon;
		public bool IsZeroApprox() => Mathf.IsZeroApprox(X) && Mathf.IsZeroApprox(Y) && Mathf.IsZeroApprox(Z) && Mathf.IsZeroApprox(W);
		public bool IsEqualApprox(Vector4 other) => Mathf.IsEqualApprox(X, other.X) && Mathf.IsEqualApprox(Y, other.Y) && Mathf.IsEqualApprox(Z, other.Z) && Mathf.IsEqualApprox(W, other.W);

		public Vector4 Normalized()
		{
			float len = Length();
			if (len == 0)
				return .(0, 0, 0, 0);
			return .(X / len, Y / len, Z / len, W / len);
		}

		public Vector4 Lerp(Vector4 to, float weight) => .(Mathf.Lerp(X, to.X, weight), Mathf.Lerp(Y, to.Y, weight), Mathf.Lerp(Z, to.Z, weight), Mathf.Lerp(W, to.W, weight));
		public Vector4 Max(Vector4 with) => .(Mathf.Max(X, with.X), Mathf.Max(Y, with.Y), Mathf.Max(Z, with.Z), Mathf.Max(W, with.W));
		public Vector4 Min(Vector4 with) => .(Mathf.Min(X, with.X), Mathf.Min(Y, with.Y), Mathf.Min(Z, with.Z), Mathf.Min(W, with.W));
		public Vector4 Snapped(Vector4 step) => .(Mathf.Snapped(X, step.X), Mathf.Snapped(Y, step.Y), Mathf.Snapped(Z, step.Z), Mathf.Snapped(W, step.W));
		public bool Equals(Vector4 other) => X == other.X && Y == other.Y && Z == other.Z && W == other.W;

		public static Vector4 operator +(Vector4 a, Vector4 b) => .(a.X + b.X, a.Y + b.Y, a.Z + b.Z, a.W + b.W);
		public static Vector4 operator -(Vector4 a, Vector4 b) => .(a.X - b.X, a.Y - b.Y, a.Z - b.Z, a.W - b.W);
		public static Vector4 operator -(Vector4 v) => .(-v.X, -v.Y, -v.Z, -v.W);
		public static Vector4 operator *(Vector4 v, float s) => .(v.X * s, v.Y * s, v.Z * s, v.W * s);
		public static Vector4 operator *(float s, Vector4 v) => .(v.X * s, v.Y * s, v.Z * s, v.W * s);
		public static Vector4 operator *(Vector4 a, Vector4 b) => .(a.X * b.X, a.Y * b.Y, a.Z * b.Z, a.W * b.W);
		public static Vector4 operator /(Vector4 v, float s) => .(v.X / s, v.Y / s, v.Z / s, v.W / s);
		public static Vector4 operator /(Vector4 a, Vector4 b) => .(a.X / b.X, a.Y / b.Y, a.Z / b.Z, a.W / b.W);
		public static Vector4 operator %(Vector4 v, float s) => .(v.X % s, v.Y % s, v.Z % s, v.W % s);
		public static Vector4 operator %(Vector4 a, Vector4 b) => .(a.X % b.X, a.Y % b.Y, a.Z % b.Z, a.W % b.W);
		public static bool operator ==(Vector4 a, Vector4 b) => a.X == b.X && a.Y == b.Y && a.Z == b.Z && a.W == b.W;
	}
}
