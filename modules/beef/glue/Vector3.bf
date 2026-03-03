// Godot Vector3 math, ported from modules/mono/glue/GodotSharp/GodotSharp/Core/Vector3.cs.
// Extension over the [CRepr] layout stub (X, Y, Z) in GodotPrimitives.bf.
using System;

namespace Godot
{
	extension Vector3
	{
		public this(float x, float y, float z)
		{
			X = x;
			Y = y;
			Z = z;
		}

		public static Vector3 Zero => .(0, 0, 0);
		public static Vector3 One => .(1, 1, 1);
		public static Vector3 Inf => .(Mathf.Inf, Mathf.Inf, Mathf.Inf);
		public static Vector3 Up => .(0, 1, 0);
		public static Vector3 Down => .(0, -1, 0);
		public static Vector3 Right => .(1, 0, 0);
		public static Vector3 Left => .(-1, 0, 0);
		public static Vector3 Forward => .(0, 0, -1);
		public static Vector3 Back => .(0, 0, 1);

		public float this[int index]
		{
			get
			{
				switch (index)
				{
				case 0: return X;
				case 1: return Y;
				case 2: return Z;
				default: return 0;
				}
			}
			set mut
			{
				switch (index)
				{
				case 0: X = value;
				case 1: Y = value;
				case 2: Z = value;
				}
			}
		}

		public Vector3 Abs() => .(Mathf.Abs(X), Mathf.Abs(Y), Mathf.Abs(Z));
		public Vector3 Ceil() => .(Mathf.Ceil(X), Mathf.Ceil(Y), Mathf.Ceil(Z));
		public Vector3 Floor() => .(Mathf.Floor(X), Mathf.Floor(Y), Mathf.Floor(Z));
		public Vector3 Round() => .(Mathf.Round(X), Mathf.Round(Y), Mathf.Round(Z));
		public Vector3 Sign() => .(Mathf.Sign(X), Mathf.Sign(Y), Mathf.Sign(Z));
		public Vector3 Clamp(Vector3 min, Vector3 max) => .(Mathf.Clamp(X, min.X, max.X), Mathf.Clamp(Y, min.Y, max.Y), Mathf.Clamp(Z, min.Z, max.Z));
		public Vector3 Clamp(float min, float max) => .(Mathf.Clamp(X, min, max), Mathf.Clamp(Y, min, max), Mathf.Clamp(Z, min, max));
		public Vector3 Cross(Vector3 with) => .((Y * with.Z) - (Z * with.Y), (Z * with.X) - (X * with.Z), (X * with.Y) - (Y * with.X));
		public float Dot(Vector3 with) => (X * with.X) + (Y * with.Y) + (Z * with.Z);
		public float DistanceTo(Vector3 to) => (to - this).Length();
		public float DistanceSquaredTo(Vector3 to) => (to - this).LengthSquared();
		public Vector3 DirectionTo(Vector3 to) => (to - this).Normalized();
		public float Length() => Mathf.Sqrt((X * X) + (Y * Y) + (Z * Z));
		public float LengthSquared() => (X * X) + (Y * Y) + (Z * Z);
		public bool IsFinite() => Mathf.IsFinite(X) && Mathf.IsFinite(Y) && Mathf.IsFinite(Z);
		public bool IsNormalized() => Mathf.Abs(LengthSquared() - 1.0f) < Mathf.Epsilon;
		public bool IsZeroApprox() => Mathf.IsZeroApprox(X) && Mathf.IsZeroApprox(Y) && Mathf.IsZeroApprox(Z);
		public bool IsEqualApprox(Vector3 other) => Mathf.IsEqualApprox(X, other.X) && Mathf.IsEqualApprox(Y, other.Y) && Mathf.IsEqualApprox(Z, other.Z);

		public Vector3 Normalized()
		{
			float len = Length();
			if (len == 0)
				return .(0, 0, 0);
			return .(X / len, Y / len, Z / len);
		}

		public float AngleTo(Vector3 to) => Mathf.Atan2(Cross(to).Length(), Dot(to));
		public Vector3 Lerp(Vector3 to, float weight) => .(Mathf.Lerp(X, to.X, weight), Mathf.Lerp(Y, to.Y, weight), Mathf.Lerp(Z, to.Z, weight));
		public Vector3 Max(Vector3 with) => .(Mathf.Max(X, with.X), Mathf.Max(Y, with.Y), Mathf.Max(Z, with.Z));
		public Vector3 Max(float with) => .(Mathf.Max(X, with), Mathf.Max(Y, with), Mathf.Max(Z, with));
		public Vector3 Min(Vector3 with) => .(Mathf.Min(X, with.X), Mathf.Min(Y, with.Y), Mathf.Min(Z, with.Z));
		public Vector3 Min(float with) => .(Mathf.Min(X, with), Mathf.Min(Y, with), Mathf.Min(Z, with));
		public Vector3 Snapped(Vector3 step) => .(Mathf.Snapped(X, step.X), Mathf.Snapped(Y, step.Y), Mathf.Snapped(Z, step.Z));
		public Vector3 Snapped(float step) => .(Mathf.Snapped(X, step), Mathf.Snapped(Y, step), Mathf.Snapped(Z, step));

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

		public Vector3 Reflect(Vector3 normal) => (2.0f * normal * Dot(normal)) - this;
		public Vector3 Project(Vector3 onNormal) => onNormal * (Dot(onNormal) / onNormal.LengthSquared());
		public Vector3 Slide(Vector3 normal) => this - (normal * Dot(normal));
		public Vector3 Bounce(Vector3 normal) => -Reflect(normal);

		public Vector3 MoveToward(Vector3 to, float delta)
		{
			Vector3 vd = to - this;
			float len = vd.Length();
			if (len <= delta || len < Mathf.Epsilon)
				return to;
			return this + (vd / len * delta);
		}

		// Rotate around a (normalized) axis using Rodrigues' formula (avoids a Basis dependency).
		public Vector3 Rotated(Vector3 axis, float angle)
		{
			(float sin, float cos) = Mathf.SinCos(angle);
			return (this * cos) + (axis.Cross(this) * sin) + (axis * (axis.Dot(this) * (1.0f - cos)));
		}

		public float SignedAngleTo(Vector3 to, Vector3 axis)
		{
			Vector3 crossTo = Cross(to);
			float unsignedAngle = Mathf.Atan2(crossTo.Length(), Dot(to));
			float sign = crossTo.Dot(axis);
			return (sign < 0) ? -unsignedAngle : unsignedAngle;
		}

		public Vector3 LimitLength(float length = 1.0f)
		{
			float l = Length();
			Vector3 v = this;
			if (l > 0 && length < l)
				v = v / l * length;
			return v;
		}

		public bool Equals(Vector3 other) => X == other.X && Y == other.Y && Z == other.Z;

		public static Vector3 operator +(Vector3 a, Vector3 b) => .(a.X + b.X, a.Y + b.Y, a.Z + b.Z);
		public static Vector3 operator -(Vector3 a, Vector3 b) => .(a.X - b.X, a.Y - b.Y, a.Z - b.Z);
		public static Vector3 operator -(Vector3 v) => .(-v.X, -v.Y, -v.Z);
		public static Vector3 operator *(Vector3 v, float s) => .(v.X * s, v.Y * s, v.Z * s);
		public static Vector3 operator *(float s, Vector3 v) => .(v.X * s, v.Y * s, v.Z * s);
		public static Vector3 operator *(Vector3 a, Vector3 b) => .(a.X * b.X, a.Y * b.Y, a.Z * b.Z);
		public static Vector3 operator /(Vector3 v, float s) => .(v.X / s, v.Y / s, v.Z / s);
		public static Vector3 operator /(Vector3 a, Vector3 b) => .(a.X / b.X, a.Y / b.Y, a.Z / b.Z);
		public static Vector3 operator %(Vector3 v, float s) => .(v.X % s, v.Y % s, v.Z % s);
		public static Vector3 operator %(Vector3 a, Vector3 b) => .(a.X % b.X, a.Y % b.Y, a.Z % b.Z);
		public static bool operator ==(Vector3 a, Vector3 b) => a.X == b.X && a.Y == b.Y && a.Z == b.Z;
	}
}
