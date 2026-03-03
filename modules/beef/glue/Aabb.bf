// Godot Aabb math, ported from modules/mono/glue/GodotSharp/GodotSharp/Core/Aabb.cs.
// Extension over the [CRepr] layout stub (Vector3 Position, Size) in GodotPrimitives.bf.
using System;

namespace Godot
{
	extension Aabb
	{
		public Vector3 End
		{
			get => Position + Size;
			set mut => Size = value - Position;
		}
		public float Volume => Size.X * Size.Y * Size.Z;

		public this(Vector3 position, Vector3 size)
		{
			Position = position;
			Size = size;
		}
		public this(Vector3 position, float width, float height, float depth)
		{
			Position = position;
			Size = .(width, height, depth);
		}
		public this(float x, float y, float z, Vector3 size)
		{
			Position = .(x, y, z);
			Size = size;
		}
		public this(float x, float y, float z, float width, float height, float depth)
		{
			Position = .(x, y, z);
			Size = .(width, height, depth);
		}

		public Vector3 GetCenter() => Position + (Size * 0.5f);
		public bool HasVolume() => Size.X > 0.0f && Size.Y > 0.0f && Size.Z > 0.0f;
		public bool HasSurface() => Size.X > 0.0f || Size.Y > 0.0f || Size.Z > 0.0f;
		public bool IsFinite() => Position.IsFinite() && Size.IsFinite();

		public Aabb Abs()
		{
			Vector3 end = End;
			Vector3 topLeft = .(Mathf.Min(Position.X, end.X), Mathf.Min(Position.Y, end.Y), Mathf.Min(Position.Z, end.Z));
			return .(topLeft, Size.Abs());
		}

		public bool HasPoint(Vector3 point)
		{
			if (point.X < Position.X || point.Y < Position.Y || point.Z < Position.Z)
				return false;
			if (point.X > Position.X + Size.X || point.Y > Position.Y + Size.Y || point.Z > Position.Z + Size.Z)
				return false;
			return true;
		}

		public bool Encloses(Aabb with)
		{
			Vector3 srcMin = Position;
			Vector3 srcMax = Position + Size;
			Vector3 dstMin = with.Position;
			Vector3 dstMax = with.Position + with.Size;
			return srcMin.X <= dstMin.X && srcMax.X >= dstMax.X &&
				srcMin.Y <= dstMin.Y && srcMax.Y >= dstMax.Y &&
				srcMin.Z <= dstMin.Z && srcMax.Z >= dstMax.Z;
		}

		public Aabb Expand(Vector3 point)
		{
			Vector3 begin = Position;
			Vector3 end = Position + Size;
			if (point.X < begin.X) begin.X = point.X;
			if (point.Y < begin.Y) begin.Y = point.Y;
			if (point.Z < begin.Z) begin.Z = point.Z;
			if (point.X > end.X) end.X = point.X;
			if (point.Y > end.Y) end.Y = point.Y;
			if (point.Z > end.Z) end.Z = point.Z;
			return .(begin, end - begin);
		}

		public Aabb Merge(Aabb with)
		{
			Vector3 beg1 = Position;
			Vector3 beg2 = with.Position;
			Vector3 end1 = Size + beg1;
			Vector3 end2 = with.Size + beg2;
			Vector3 min = .(Mathf.Min(beg1.X, beg2.X), Mathf.Min(beg1.Y, beg2.Y), Mathf.Min(beg1.Z, beg2.Z));
			Vector3 max = .(Mathf.Max(end1.X, end2.X), Mathf.Max(end1.Y, end2.Y), Mathf.Max(end1.Z, end2.Z));
			return .(min, max - min);
		}

		public Aabb Grow(float by) => .(Position.X - by, Position.Y - by, Position.Z - by, Size.X + by * 2, Size.Y + by * 2, Size.Z + by * 2);

		public bool Intersects(Aabb with)
		{
			if (Position.X >= with.Position.X + with.Size.X) return false;
			if (Position.X + Size.X <= with.Position.X) return false;
			if (Position.Y >= with.Position.Y + with.Size.Y) return false;
			if (Position.Y + Size.Y <= with.Position.Y) return false;
			if (Position.Z >= with.Position.Z + with.Size.Z) return false;
			if (Position.Z + Size.Z <= with.Position.Z) return false;
			return true;
		}

		public Aabb Intersection(Aabb with)
		{
			if (!Intersects(with))
				return .();
			Vector3 pos = with.Position.Max(Position);
			Vector3 end = (with.Position + with.Size).Min(Position + Size);
			return .(pos, end - pos);
		}

		public Vector3 GetEndpoint(int idx)
		{
			switch (idx)
			{
			case 0: return .(Position.X, Position.Y, Position.Z);
			case 1: return .(Position.X, Position.Y, Position.Z + Size.Z);
			case 2: return .(Position.X, Position.Y + Size.Y, Position.Z);
			case 3: return .(Position.X, Position.Y + Size.Y, Position.Z + Size.Z);
			case 4: return .(Position.X + Size.X, Position.Y, Position.Z);
			case 5: return .(Position.X + Size.X, Position.Y, Position.Z + Size.Z);
			case 6: return .(Position.X + Size.X, Position.Y + Size.Y, Position.Z);
			case 7: return .(Position.X + Size.X, Position.Y + Size.Y, Position.Z + Size.Z);
			default: return .();
			}
		}

		public Vector3 GetSupport(Vector3 dir)
		{
			Vector3 support = Position;
			if (dir.X > 0.0f) support.X += Size.X;
			if (dir.Y > 0.0f) support.Y += Size.Y;
			if (dir.Z > 0.0f) support.Z += Size.Z;
			return support;
		}

		public Vector3.Axis GetLongestAxisIndex()
		{
			Vector3.Axis axis = .AXIS_X;
			float maxSize = Size.X;
			if (Size.Y > maxSize) { axis = .AXIS_Y; maxSize = Size.Y; }
			if (Size.Z > maxSize) { axis = .AXIS_Z; }
			return axis;
		}

		public float GetLongestAxisSize() => Mathf.Max(Size.X, Mathf.Max(Size.Y, Size.Z));

		public Vector3 GetLongestAxis()
		{
			switch (GetLongestAxisIndex())
			{
			case .AXIS_Y: return .(0, 1, 0);
			case .AXIS_Z: return .(0, 0, 1);
			default: return .(1, 0, 0);
			}
		}

		public Vector3.Axis GetShortestAxisIndex()
		{
			Vector3.Axis axis = .AXIS_X;
			float minSize = Size.X;
			if (Size.Y < minSize) { axis = .AXIS_Y; minSize = Size.Y; }
			if (Size.Z < minSize) { axis = .AXIS_Z; }
			return axis;
		}

		public float GetShortestAxisSize() => Mathf.Min(Size.X, Mathf.Min(Size.Y, Size.Z));

		public Vector3 GetShortestAxis()
		{
			switch (GetShortestAxisIndex())
			{
			case .AXIS_Y: return .(0, 1, 0);
			case .AXIS_Z: return .(0, 0, 1);
			default: return .(1, 0, 0);
			}
		}

		public bool IntersectsPlane(Plane plane)
		{
			bool over = false;
			bool under = false;
			for (int i = 0; i < 8; i++)
			{
				if (plane.DistanceTo(GetEndpoint(i)) > 0)
					over = true;
				else
					under = true;
			}
			return under && over;
		}

		public bool IntersectsSegment(Vector3 from, Vector3 to)
		{
			float min = 0f;
			float max = 1f;
			for (int i = 0; i < 3; i++)
			{
				float segFrom = from[i];
				float segTo = to[i];
				float boxBegin = Position[i];
				float boxEnd = boxBegin + Size[i];
				float cmin;
				float cmax;
				if (segFrom < segTo)
				{
					if (segFrom > boxEnd || segTo < boxBegin)
						return false;
					float length = segTo - segFrom;
					cmin = segFrom < boxBegin ? (boxBegin - segFrom) / length : 0f;
					cmax = segTo > boxEnd ? (boxEnd - segFrom) / length : 1f;
				}
				else
				{
					if (segTo > boxEnd || segFrom < boxBegin)
						return false;
					float length = segTo - segFrom;
					cmin = segFrom > boxEnd ? (boxEnd - segFrom) / length : 0f;
					cmax = segTo < boxBegin ? (boxBegin - segFrom) / length : 1f;
				}
				if (cmin > min) min = cmin;
				if (cmax < max) max = cmax;
				if (max < min) return false;
			}
			return true;
		}

		public bool IsEqualApprox(Aabb other) => Position.IsEqualApprox(other.Position) && Size.IsEqualApprox(other.Size);
		public bool Equals(Aabb other) => Position == other.Position && Size == other.Size;

		public static bool operator ==(Aabb a, Aabb b) => a.Position == b.Position && a.Size == b.Size;
	}
}
