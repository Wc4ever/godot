// Godot Rect2I math, ported from modules/mono/glue/GodotSharp/GodotSharp/Core/Rect2I.cs.
// Extension over the [CRepr] layout stub (Vector2I Position, Size) in GodotPrimitives.bf.
using System;

namespace Godot
{
	extension Rect2I
	{
		public Vector2I End
		{
			get => Position + Size;
			set mut => Size = value - Position;
		}
		public int32 Area => Size.X * Size.Y;

		public this(Vector2I position, Vector2I size)
		{
			Position = position;
			Size = size;
		}
		public this(Vector2I position, int32 width, int32 height)
		{
			Position = position;
			Size = .(width, height);
		}
		public this(int32 x, int32 y, Vector2I size)
		{
			Position = .(x, y);
			Size = size;
		}
		public this(int32 x, int32 y, int32 width, int32 height)
		{
			Position = .(x, y);
			Size = .(width, height);
		}

		public Vector2I GetCenter() => Position + (Size / 2);
		public bool HasArea() => Size.X > 0 && Size.Y > 0;

		public Rect2I Abs()
		{
			Vector2I end = End;
			Vector2I topLeft = .(Mathf.Min(Position.X, end.X), Mathf.Min(Position.Y, end.Y));
			return .(topLeft, Size.Abs());
		}

		public bool HasPoint(Vector2I point)
		{
			if (point.X < Position.X || point.Y < Position.Y)
				return false;
			if (point.X >= Position.X + Size.X || point.Y >= Position.Y + Size.Y)
				return false;
			return true;
		}

		public bool Encloses(Rect2I b) =>
			b.Position.X >= Position.X && b.Position.Y >= Position.Y &&
			b.Position.X + b.Size.X <= Position.X + Size.X &&
			b.Position.Y + b.Size.Y <= Position.Y + Size.Y;

		public bool Intersects(Rect2I b)
		{
			if (Position.X >= b.Position.X + b.Size.X) return false;
			if (Position.X + Size.X <= b.Position.X) return false;
			if (Position.Y >= b.Position.Y + b.Size.Y) return false;
			if (Position.Y + Size.Y <= b.Position.Y) return false;
			return true;
		}

		public Rect2I Intersection(Rect2I b)
		{
			if (!Intersects(b))
				return .();
			Vector2I pos = b.Position.Max(Position);
			Vector2I end = (b.Position + b.Size).Min(Position + Size);
			return .(pos, end - pos);
		}

		public Rect2I Merge(Rect2I b)
		{
			Vector2I pos = Position.Min(b.Position);
			Vector2I end = (Position + Size).Max(b.Position + b.Size);
			return .(pos, end - pos);
		}

		public Rect2I Expand(Vector2I to) => Merge(.(to, Vector2I.Zero));

		public Rect2I GrowIndividual(int32 left, int32 top, int32 right, int32 bottom) =>
			.(Position.X - left, Position.Y - top, Size.X + left + right, Size.Y + top + bottom);

		public Rect2I Grow(int32 by) => GrowIndividual(by, by, by, by);

		public Rect2I GrowSide(Side side, int32 by) =>
			GrowIndividual(side == .SIDE_LEFT ? by : 0, side == .SIDE_TOP ? by : 0,
				side == .SIDE_RIGHT ? by : 0, side == .SIDE_BOTTOM ? by : 0);

		public bool Equals(Rect2I other) => Position == other.Position && Size == other.Size;

		public static bool operator ==(Rect2I a, Rect2I b) => a.Position == b.Position && a.Size == b.Size;

		public static implicit operator Rect2(Rect2I v) => .(v.Position, v.Size);
		public static explicit operator Rect2I(Rect2 v) => .((Vector2I)v.Position, (Vector2I)v.Size);
	}
}
