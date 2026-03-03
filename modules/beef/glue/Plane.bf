// Godot Plane math, ported from modules/mono/glue/GodotSharp/GodotSharp/Core/Plane.cs.
// Extension over the [CRepr] layout stub (Vector3 Normal; float D) in GodotPrimitives.bf.
using System;

namespace Godot
{
	extension Plane
	{
		public float X
		{
			get => Normal.X;
			set mut => Normal.X = value;
		}
		public float Y
		{
			get => Normal.Y;
			set mut => Normal.Y = value;
		}
		public float Z
		{
			get => Normal.Z;
			set mut => Normal.Z = value;
		}

		public static Plane PlaneYZ => .(1, 0, 0, 0);
		public static Plane PlaneXZ => .(0, 1, 0, 0);
		public static Plane PlaneXY => .(0, 0, 1, 0);

		public this(float a, float b, float c, float d)
		{
			Normal = .(a, b, c);
			D = d;
		}
		public this(Vector3 normal)
		{
			Normal = normal;
			D = 0;
		}
		public this(Vector3 normal, float d)
		{
			Normal = normal;
			D = d;
		}
		public this(Vector3 normal, Vector3 point)
		{
			Normal = normal;
			D = normal.Dot(point);
		}
		public this(Vector3 v1, Vector3 v2, Vector3 v3)
		{
			Normal = (v1 - v3).Cross(v1 - v2).Normalized();
			D = Normal.Dot(v1);
		}

		public float DistanceTo(Vector3 point) => Normal.Dot(point) - D;
		public Vector3 GetCenter() => Normal * D;
		public bool IsPointOver(Vector3 point) => Normal.Dot(point) > D;
		public bool HasPoint(Vector3 point, float tolerance = Mathf.Epsilon) => Mathf.Abs(Normal.Dot(point) - D) <= tolerance;
		public Vector3 Project(Vector3 point) => point - (Normal * DistanceTo(point));
		public bool IsFinite() => Normal.IsFinite() && Mathf.IsFinite(D);
		public bool IsEqualApprox(Plane other) => Normal.IsEqualApprox(other.Normal) && Mathf.IsEqualApprox(D, other.D);
		public bool Equals(Plane other) => Normal == other.Normal && D == other.D;

		public Plane Normalized()
		{
			float len = Normal.Length();
			if (len == 0)
				return .(0, 0, 0, 0);
			return .(Normal / len, D / len);
		}

		public Vector3? Intersect3(Plane b, Plane c)
		{
			float denom = Normal.Cross(b.Normal).Dot(c.Normal);
			if (Mathf.IsZeroApprox(denom))
				return null;
			Vector3 result = (b.Normal.Cross(c.Normal) * D) +
				(c.Normal.Cross(Normal) * b.D) +
				(Normal.Cross(b.Normal) * c.D);
			return result / denom;
		}

		public Vector3? IntersectsRay(Vector3 from, Vector3 dir)
		{
			float den = Normal.Dot(dir);
			if (Mathf.IsZeroApprox(den))
				return null;
			float dist = (Normal.Dot(from) - D) / den;
			if (dist > Mathf.Epsilon)
				return null;
			return from - (dir * dist);
		}

		public Vector3? IntersectsSegment(Vector3 begin, Vector3 end)
		{
			Vector3 segment = begin - end;
			float den = Normal.Dot(segment);
			if (Mathf.IsZeroApprox(den))
				return null;
			float dist = (Normal.Dot(begin) - D) / den;
			if (dist < -Mathf.Epsilon || dist > 1.0f + Mathf.Epsilon)
				return null;
			return begin - (segment * dist);
		}

		public static Plane operator -(Plane plane) => .(-plane.Normal, -plane.D);
		public static bool operator ==(Plane a, Plane b) => a.Normal == b.Normal && a.D == b.D;
	}
}
