// Godot Vector3I math, ported from modules/mono/glue/GodotSharp/GodotSharp/Core/Vector3I.cs.
// Extension over the [CRepr] layout stub (int32 X, Y, Z) in GodotPrimitives.bf.
using System;

namespace Godot
{
	extension Vector3I
	{
		public enum Axis : int32 { AXIS_X = 0, AXIS_Y = 1, AXIS_Z = 2 }

		public this(int32 x, int32 y, int32 z)
		{
			X = x;
			Y = y;
			Z = z;
		}

		public static Vector3I Zero => .(0, 0, 0);
		public static Vector3I One => .(1, 1, 1);
		public static Vector3I MinValue => .(int32.MinValue, int32.MinValue, int32.MinValue);
		public static Vector3I MaxValue => .(int32.MaxValue, int32.MaxValue, int32.MaxValue);
		public static Vector3I Up => .(0, 1, 0);
		public static Vector3I Down => .(0, -1, 0);
		public static Vector3I Right => .(1, 0, 0);
		public static Vector3I Left => .(-1, 0, 0);
		public static Vector3I Forward => .(0, 0, -1);
		public static Vector3I Back => .(0, 0, 1);

		public int32 this[int index]
		{
			get { switch (index) { case 0: return X; case 1: return Y; default: return Z; } }
			set mut { switch (index) { case 0: X = value; case 1: Y = value; case 2: Z = value; } }
		}

		public Vector3I Abs() => .(Mathf.Abs(X), Mathf.Abs(Y), Mathf.Abs(Z));
		public Vector3I Clamp(Vector3I min, Vector3I max) => .(Mathf.Clamp(X, min.X, max.X), Mathf.Clamp(Y, min.Y, max.Y), Mathf.Clamp(Z, min.Z, max.Z));
		public Vector3I Clamp(int32 min, int32 max) => .(Mathf.Clamp(X, min, max), Mathf.Clamp(Y, min, max), Mathf.Clamp(Z, min, max));
		public int32 DistanceSquaredTo(Vector3I to) => (to.X - X) * (to.X - X) + (to.Y - Y) * (to.Y - Y) + (to.Z - Z) * (to.Z - Z);
		public float DistanceTo(Vector3I to) => Mathf.Sqrt(DistanceSquaredTo(to));
		public int32 LengthSquared() => (X * X) + (Y * Y) + (Z * Z);
		public float Length() => Mathf.Sqrt(LengthSquared());
		public Vector3I Max(Vector3I with) => .(Mathf.Max(X, with.X), Mathf.Max(Y, with.Y), Mathf.Max(Z, with.Z));
		public Vector3I Max(int32 with) => .(Mathf.Max(X, with), Mathf.Max(Y, with), Mathf.Max(Z, with));
		public Vector3I Min(Vector3I with) => .(Mathf.Min(X, with.X), Mathf.Min(Y, with.Y), Mathf.Min(Z, with.Z));
		public Vector3I Min(int32 with) => .(Mathf.Min(X, with), Mathf.Min(Y, with), Mathf.Min(Z, with));
		public Vector3I Sign() => .(Mathf.Sign(X), Mathf.Sign(Y), Mathf.Sign(Z));
		public bool Equals(Vector3I other) => X == other.X && Y == other.Y && Z == other.Z;

		public Axis MaxAxisIndex()
		{
			if (X < Y)
				return Y < Z ? .AXIS_Z : .AXIS_Y;
			return X < Z ? .AXIS_Z : .AXIS_X;
		}

		public Axis MinAxisIndex()
		{
			if (X < Y)
				return X < Z ? .AXIS_X : .AXIS_Z;
			return Y < Z ? .AXIS_Y : .AXIS_Z;
		}

		public static Vector3I operator +(Vector3I a, Vector3I b) => .(a.X + b.X, a.Y + b.Y, a.Z + b.Z);
		public static Vector3I operator -(Vector3I a, Vector3I b) => .(a.X - b.X, a.Y - b.Y, a.Z - b.Z);
		public static Vector3I operator -(Vector3I v) => .(-v.X, -v.Y, -v.Z);
		public static Vector3I operator *(Vector3I v, int32 s) => .(v.X * s, v.Y * s, v.Z * s);
		public static Vector3I operator *(int32 s, Vector3I v) => .(v.X * s, v.Y * s, v.Z * s);
		public static Vector3I operator *(Vector3I a, Vector3I b) => .(a.X * b.X, a.Y * b.Y, a.Z * b.Z);
		public static Vector3I operator /(Vector3I v, int32 s) => .(v.X / s, v.Y / s, v.Z / s);
		public static Vector3I operator /(Vector3I a, Vector3I b) => .(a.X / b.X, a.Y / b.Y, a.Z / b.Z);
		public static Vector3I operator %(Vector3I v, int32 s) => .(v.X % s, v.Y % s, v.Z % s);
		public static Vector3I operator %(Vector3I a, Vector3I b) => .(a.X % b.X, a.Y % b.Y, a.Z % b.Z);
		public static bool operator ==(Vector3I a, Vector3I b) => a.X == b.X && a.Y == b.Y && a.Z == b.Z;

		public static implicit operator Vector3(Vector3I v) => .(v.X, v.Y, v.Z);
		public static explicit operator Vector3I(Vector3 v) => .((int32)v.X, (int32)v.Y, (int32)v.Z);
	}
}
