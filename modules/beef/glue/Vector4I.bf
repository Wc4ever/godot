// Godot Vector4I math, ported from modules/mono/glue/GodotSharp/GodotSharp/Core/Vector4I.cs.
// Extension over the [CRepr] layout stub (int32 X, Y, Z, W) in GodotPrimitives.bf.
using System;

namespace Godot
{
	extension Vector4I
	{
		public enum Axis : int32 { AXIS_X = 0, AXIS_Y = 1, AXIS_Z = 2, AXIS_W = 3 }

		public this(int32 x, int32 y, int32 z, int32 w)
		{
			X = x;
			Y = y;
			Z = z;
			W = w;
		}

		public static Vector4I Zero => .(0, 0, 0, 0);
		public static Vector4I One => .(1, 1, 1, 1);
		public static Vector4I MinValue => .(int32.MinValue, int32.MinValue, int32.MinValue, int32.MinValue);
		public static Vector4I MaxValue => .(int32.MaxValue, int32.MaxValue, int32.MaxValue, int32.MaxValue);

		public int32 this[int index]
		{
			get { switch (index) { case 0: return X; case 1: return Y; case 2: return Z; default: return W; } }
			set mut { switch (index) { case 0: X = value; case 1: Y = value; case 2: Z = value; case 3: W = value; } }
		}

		public Vector4I Abs() => .(Mathf.Abs(X), Mathf.Abs(Y), Mathf.Abs(Z), Mathf.Abs(W));
		public Vector4I Clamp(Vector4I min, Vector4I max) => .(Mathf.Clamp(X, min.X, max.X), Mathf.Clamp(Y, min.Y, max.Y), Mathf.Clamp(Z, min.Z, max.Z), Mathf.Clamp(W, min.W, max.W));
		public Vector4I Clamp(int32 min, int32 max) => .(Mathf.Clamp(X, min, max), Mathf.Clamp(Y, min, max), Mathf.Clamp(Z, min, max), Mathf.Clamp(W, min, max));
		public int32 DistanceSquaredTo(Vector4I to) => (to.X - X) * (to.X - X) + (to.Y - Y) * (to.Y - Y) + (to.Z - Z) * (to.Z - Z) + (to.W - W) * (to.W - W);
		public float DistanceTo(Vector4I to) => Mathf.Sqrt(DistanceSquaredTo(to));
		public int32 LengthSquared() => (X * X) + (Y * Y) + (Z * Z) + (W * W);
		public float Length() => Mathf.Sqrt(LengthSquared());
		public Vector4I Max(Vector4I with) => .(Mathf.Max(X, with.X), Mathf.Max(Y, with.Y), Mathf.Max(Z, with.Z), Mathf.Max(W, with.W));
		public Vector4I Min(Vector4I with) => .(Mathf.Min(X, with.X), Mathf.Min(Y, with.Y), Mathf.Min(Z, with.Z), Mathf.Min(W, with.W));
		public Vector4I Sign() => .(Mathf.Sign(X), Mathf.Sign(Y), Mathf.Sign(Z), Mathf.Sign(W));
		public bool Equals(Vector4I other) => X == other.X && Y == other.Y && Z == other.Z && W == other.W;

		public static Vector4I operator +(Vector4I a, Vector4I b) => .(a.X + b.X, a.Y + b.Y, a.Z + b.Z, a.W + b.W);
		public static Vector4I operator -(Vector4I a, Vector4I b) => .(a.X - b.X, a.Y - b.Y, a.Z - b.Z, a.W - b.W);
		public static Vector4I operator -(Vector4I v) => .(-v.X, -v.Y, -v.Z, -v.W);
		public static Vector4I operator *(Vector4I v, int32 s) => .(v.X * s, v.Y * s, v.Z * s, v.W * s);
		public static Vector4I operator *(int32 s, Vector4I v) => .(v.X * s, v.Y * s, v.Z * s, v.W * s);
		public static Vector4I operator *(Vector4I a, Vector4I b) => .(a.X * b.X, a.Y * b.Y, a.Z * b.Z, a.W * b.W);
		public static Vector4I operator /(Vector4I v, int32 s) => .(v.X / s, v.Y / s, v.Z / s, v.W / s);
		public static Vector4I operator /(Vector4I a, Vector4I b) => .(a.X / b.X, a.Y / b.Y, a.Z / b.Z, a.W / b.W);
		public static Vector4I operator %(Vector4I v, int32 s) => .(v.X % s, v.Y % s, v.Z % s, v.W % s);
		public static Vector4I operator %(Vector4I a, Vector4I b) => .(a.X % b.X, a.Y % b.Y, a.Z % b.Z, a.W % b.W);
		public static bool operator ==(Vector4I a, Vector4I b) => a.X == b.X && a.Y == b.Y && a.Z == b.Z && a.W == b.W;

		public static implicit operator Vector4(Vector4I v) => .(v.X, v.Y, v.Z, v.W);
		public static explicit operator Vector4I(Vector4 v) => .((int32)v.X, (int32)v.Y, (int32)v.Z, (int32)v.W);
	}
}
