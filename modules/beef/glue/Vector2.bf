// Godot Vector2 math, ported from modules/mono/glue/GodotSharp/GodotSharp/Core/Vector2.cs.
// Hand-written extension over the [CRepr] layout stub (X, Y) generated in GodotPrimitives.bf, so the
// engine marshalling layout is unchanged. The bindings generator copies this into GodotBindings/src.
using System;

namespace Godot
{
	extension Vector2
	{
		public this(float x, float y)
		{
			X = x;
			Y = y;
		}

		public static Vector2 Zero => .(0, 0);
		public static Vector2 One => .(1, 1);
		public static Vector2 Inf => .(Mathf.Inf, Mathf.Inf);
		public static Vector2 Up => .(0, -1);
		public static Vector2 Down => .(0, 1);
		public static Vector2 Right => .(1, 0);
		public static Vector2 Left => .(-1, 0);

		public float this[int index]
		{
			get
			{
				switch (index)
				{
				case 0: return X;
				case 1: return Y;
				default: return 0;
				}
			}
			set mut
			{
				switch (index)
				{
				case 0: X = value;
				case 1: Y = value;
				}
			}
		}

		public Vector2 Abs() => .(Mathf.Abs(X), Mathf.Abs(Y));
		public float Angle() => Mathf.Atan2(Y, X);
		public float AngleTo(Vector2 to) => Mathf.Atan2(Cross(to), Dot(to));
		public float AngleToPoint(Vector2 to) => Mathf.Atan2(to.Y - Y, to.X - X);
		public float Aspect() => X / Y;
		public Vector2 Ceil() => .(Mathf.Ceil(X), Mathf.Ceil(Y));
		public Vector2 Floor() => .(Mathf.Floor(X), Mathf.Floor(Y));
		public Vector2 Round() => .(Mathf.Round(X), Mathf.Round(Y));
		public Vector2 Sign() => .(Mathf.Sign(X), Mathf.Sign(Y));
		public Vector2 Clamp(Vector2 min, Vector2 max) => .(Mathf.Clamp(X, min.X, max.X), Mathf.Clamp(Y, min.Y, max.Y));
		public Vector2 Clamp(float min, float max) => .(Mathf.Clamp(X, min, max), Mathf.Clamp(Y, min, max));
		public float Cross(Vector2 with) => (X * with.Y) - (Y * with.X);
		public float Dot(Vector2 with) => (X * with.X) + (Y * with.Y);
		public float DistanceTo(Vector2 to) => Mathf.Sqrt((X - to.X) * (X - to.X) + (Y - to.Y) * (Y - to.Y));
		public float DistanceSquaredTo(Vector2 to) => (X - to.X) * (X - to.X) + (Y - to.Y) * (Y - to.Y);
		public Vector2 DirectionTo(Vector2 to) => Vector2(to.X - X, to.Y - Y).Normalized();
		public float Length() => Mathf.Sqrt((X * X) + (Y * Y));
		public float LengthSquared() => (X * X) + (Y * Y);
		public bool IsFinite() => Mathf.IsFinite(X) && Mathf.IsFinite(Y);
		public bool IsNormalized() => Mathf.Abs(LengthSquared() - 1.0f) < Mathf.Epsilon;
		public bool IsZeroApprox() => Mathf.IsZeroApprox(X) && Mathf.IsZeroApprox(Y);
		public bool IsEqualApprox(Vector2 other) => Mathf.IsEqualApprox(X, other.X) && Mathf.IsEqualApprox(Y, other.Y);

		public Vector2 Normalized()
		{
			float len = Length();
			if (len == 0)
				return .(0, 0);
			return .(X / len, Y / len);
		}

		public Vector2 Lerp(Vector2 to, float weight) => .(Mathf.Lerp(X, to.X, weight), Mathf.Lerp(Y, to.Y, weight));
		public Vector2 Max(Vector2 with) => .(Mathf.Max(X, with.X), Mathf.Max(Y, with.Y));
		public Vector2 Max(float with) => .(Mathf.Max(X, with), Mathf.Max(Y, with));
		public Vector2 Min(Vector2 with) => .(Mathf.Min(X, with.X), Mathf.Min(Y, with.Y));
		public Vector2 Min(float with) => .(Mathf.Min(X, with), Mathf.Min(Y, with));
		public Axis MaxAxisIndex() => X < Y ? .AXIS_Y : .AXIS_X;
		public Axis MinAxisIndex() => X < Y ? .AXIS_X : .AXIS_Y;
		public Vector2 Orthogonal() => .(Y, -X);
		public Vector2 Snapped(Vector2 step) => .(Mathf.Snapped(X, step.X), Mathf.Snapped(Y, step.Y));
		public Vector2 Snapped(float step) => .(Mathf.Snapped(X, step), Mathf.Snapped(Y, step));

		public Vector2 MoveToward(Vector2 to, float delta)
		{
			Vector2 v = this;
			Vector2 vd = .(to.X - v.X, to.Y - v.Y);
			float len = vd.Length();
			if (len <= delta || len < Mathf.Epsilon)
				return to;
			return .(v.X + vd.X / len * delta, v.Y + vd.Y / len * delta);
		}

		public Vector2 Reflect(Vector2 normal) => .(2.0f * Dot(normal) * normal.X - X, 2.0f * Dot(normal) * normal.Y - Y) * -1.0f;
		public Vector2 Project(Vector2 onNormal) => onNormal * (Dot(onNormal) / onNormal.LengthSquared());
		public Vector2 Slide(Vector2 normal) => .(X - normal.X * Dot(normal), Y - normal.Y * Dot(normal));
		public Vector2 Bounce(Vector2 normal) => -Reflect(normal);

		public Vector2 Rotated(float angle)
		{
			(float sin, float cos) = Mathf.SinCos(angle);
			return .(X * cos - Y * sin, X * sin + Y * cos);
		}

		public Vector2 LimitLength(float length = 1.0f)
		{
			float l = Length();
			Vector2 v = this;
			if (l > 0 && length < l)
				v = .(X / l * length, Y / l * length);
			return v;
		}

		public bool Equals(Vector2 other) => X == other.X && Y == other.Y;

		public static Vector2 FromAngle(float angle)
		{
			(float sin, float cos) = Mathf.SinCos(angle);
			return .(cos, sin);
		}

		public static Vector2 operator +(Vector2 a, Vector2 b) => .(a.X + b.X, a.Y + b.Y);
		public static Vector2 operator -(Vector2 a, Vector2 b) => .(a.X - b.X, a.Y - b.Y);
		public static Vector2 operator -(Vector2 v) => .(-v.X, -v.Y);
		public static Vector2 operator *(Vector2 v, float s) => .(v.X * s, v.Y * s);
		public static Vector2 operator *(float s, Vector2 v) => .(v.X * s, v.Y * s);
		public static Vector2 operator *(Vector2 a, Vector2 b) => .(a.X * b.X, a.Y * b.Y);
		public static Vector2 operator /(Vector2 v, float s) => .(v.X / s, v.Y / s);
		public static Vector2 operator /(Vector2 a, Vector2 b) => .(a.X / b.X, a.Y / b.Y);
		public static Vector2 operator %(Vector2 v, float s) => .(v.X % s, v.Y % s);
		public static Vector2 operator %(Vector2 a, Vector2 b) => .(a.X % b.X, a.Y % b.Y);
		public static bool operator ==(Vector2 a, Vector2 b) => a.X == b.X && a.Y == b.Y;
	}
}
