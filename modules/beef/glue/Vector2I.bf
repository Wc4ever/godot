// Godot Vector2I math, ported from modules/mono/glue/GodotSharp/GodotSharp/Core/Vector2I.cs.
// Extension over the [CRepr] layout stub (int32 X, Y) in GodotPrimitives.bf.
using System;

namespace Godot
{
	extension Vector2I
	{
		public enum Axis : int32 { AXIS_X = 0, AXIS_Y = 1 }

		public this(int32 x, int32 y)
		{
			X = x;
			Y = y;
		}

		public static Vector2I Zero => .(0, 0);
		public static Vector2I One => .(1, 1);
		public static Vector2I MinValue => .(int32.MinValue, int32.MinValue);
		public static Vector2I MaxValue => .(int32.MaxValue, int32.MaxValue);
		public static Vector2I Up => .(0, -1);
		public static Vector2I Down => .(0, 1);
		public static Vector2I Right => .(1, 0);
		public static Vector2I Left => .(-1, 0);

		public int32 this[int index]
		{
			get { return index == 0 ? X : Y; }
			set mut { if (index == 0) X = value; else Y = value; }
		}

		public Vector2I Abs() => .(Mathf.Abs(X), Mathf.Abs(Y));
		public float Aspect() => (float)X / (float)Y;
		public Vector2I Clamp(Vector2I min, Vector2I max) => .(Mathf.Clamp(X, min.X, max.X), Mathf.Clamp(Y, min.Y, max.Y));
		public Vector2I Clamp(int32 min, int32 max) => .(Mathf.Clamp(X, min, max), Mathf.Clamp(Y, min, max));
		public int32 DistanceSquaredTo(Vector2I to) => (to.X - X) * (to.X - X) + (to.Y - Y) * (to.Y - Y);
		public float DistanceTo(Vector2I to) => Mathf.Sqrt(DistanceSquaredTo(to));
		public int32 LengthSquared() => (X * X) + (Y * Y);
		public float Length() => Mathf.Sqrt(LengthSquared());
		public Vector2I Max(Vector2I with) => .(Mathf.Max(X, with.X), Mathf.Max(Y, with.Y));
		public Vector2I Max(int32 with) => .(Mathf.Max(X, with), Mathf.Max(Y, with));
		public Vector2I Min(Vector2I with) => .(Mathf.Min(X, with.X), Mathf.Min(Y, with.Y));
		public Vector2I Min(int32 with) => .(Mathf.Min(X, with), Mathf.Min(Y, with));
		public Axis MaxAxisIndex() => X < Y ? .AXIS_Y : .AXIS_X;
		public Axis MinAxisIndex() => X < Y ? .AXIS_X : .AXIS_Y;
		public Vector2I Sign() => .(Mathf.Sign(X), Mathf.Sign(Y));
		public bool Equals(Vector2I other) => X == other.X && Y == other.Y;

		public static Vector2I operator +(Vector2I a, Vector2I b) => .(a.X + b.X, a.Y + b.Y);
		public static Vector2I operator -(Vector2I a, Vector2I b) => .(a.X - b.X, a.Y - b.Y);
		public static Vector2I operator -(Vector2I v) => .(-v.X, -v.Y);
		public static Vector2I operator *(Vector2I v, int32 s) => .(v.X * s, v.Y * s);
		public static Vector2I operator *(int32 s, Vector2I v) => .(v.X * s, v.Y * s);
		public static Vector2I operator *(Vector2I a, Vector2I b) => .(a.X * b.X, a.Y * b.Y);
		public static Vector2I operator /(Vector2I v, int32 s) => .(v.X / s, v.Y / s);
		public static Vector2I operator /(Vector2I a, Vector2I b) => .(a.X / b.X, a.Y / b.Y);
		public static Vector2I operator %(Vector2I v, int32 s) => .(v.X % s, v.Y % s);
		public static Vector2I operator %(Vector2I a, Vector2I b) => .(a.X % b.X, a.Y % b.Y);
		public static bool operator ==(Vector2I a, Vector2I b) => a.X == b.X && a.Y == b.Y;

		public static implicit operator Vector2(Vector2I v) => .(v.X, v.Y);
		public static explicit operator Vector2I(Vector2 v) => .((int32)v.X, (int32)v.Y);
	}
}
