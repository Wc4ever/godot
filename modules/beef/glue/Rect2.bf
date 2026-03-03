// Godot Rect2 math, ported from modules/mono/glue/GodotSharp/GodotSharp/Core/Rect2.cs.
// Extension over the [CRepr] layout stub (Vector2 Position, Size) in GodotPrimitives.bf.
using System;

namespace Godot
{
	extension Rect2
	{
		public Vector2 End
		{
			get => Position + Size;
			set mut => Size = value - Position;
		}
		public float Area => Size.X * Size.Y;

		public this(Vector2 position, Vector2 size)
		{
			Position = position;
			Size = size;
		}
		public this(Vector2 position, float width, float height)
		{
			Position = position;
			Size = .(width, height);
		}
		public this(float x, float y, Vector2 size)
		{
			Position = .(x, y);
			Size = size;
		}
		public this(float x, float y, float width, float height)
		{
			Position = .(x, y);
			Size = .(width, height);
		}

		public Vector2 GetCenter() => Position + (Size * 0.5f);
		public bool HasArea() => Size.X > 0.0f && Size.Y > 0.0f;
		public bool IsFinite() => Position.IsFinite() && Size.IsFinite();

		public Rect2 Abs()
		{
			Vector2 end = End;
			Vector2 topLeft = .(Mathf.Min(Position.X, end.X), Mathf.Min(Position.Y, end.Y));
			return .(topLeft, Size.Abs());
		}

		public bool HasPoint(Vector2 point)
		{
			if (point.X < Position.X || point.Y < Position.Y)
				return false;
			if (point.X >= Position.X + Size.X || point.Y >= Position.Y + Size.Y)
				return false;
			return true;
		}

		public bool Encloses(Rect2 b) =>
			b.Position.X >= Position.X && b.Position.Y >= Position.Y &&
			b.Position.X + b.Size.X <= Position.X + Size.X &&
			b.Position.Y + b.Size.Y <= Position.Y + Size.Y;

		public bool Intersects(Rect2 b, bool includeBorders = false)
		{
			if (includeBorders)
			{
				if (Position.X > b.Position.X + b.Size.X) return false;
				if (Position.X + Size.X < b.Position.X) return false;
				if (Position.Y > b.Position.Y + b.Size.Y) return false;
				if (Position.Y + Size.Y < b.Position.Y) return false;
			}
			else
			{
				if (Position.X >= b.Position.X + b.Size.X) return false;
				if (Position.X + Size.X <= b.Position.X) return false;
				if (Position.Y >= b.Position.Y + b.Size.Y) return false;
				if (Position.Y + Size.Y <= b.Position.Y) return false;
			}
			return true;
		}

		public Rect2 Intersection(Rect2 b)
		{
			if (!Intersects(b))
				return .();
			Vector2 pos = b.Position.Max(Position);
			Vector2 end = (b.Position + b.Size).Min(Position + Size);
			return .(pos, end - pos);
		}

		public Rect2 Merge(Rect2 b)
		{
			Vector2 pos = Position.Min(b.Position);
			Vector2 end = (Position + Size).Max(b.Position + b.Size);
			return .(pos, end - pos);
		}

		public Rect2 Expand(Vector2 to) => Merge(.(to, Vector2.Zero));

		public Rect2 GrowIndividual(float left, float top, float right, float bottom) =>
			.(Position.X - left, Position.Y - top, Size.X + left + right, Size.Y + top + bottom);

		public Rect2 Grow(float by) => GrowIndividual(by, by, by, by);

		public Rect2 GrowSide(Side side, float by) =>
			GrowIndividual(side == .SIDE_LEFT ? by : 0, side == .SIDE_TOP ? by : 0,
				side == .SIDE_RIGHT ? by : 0, side == .SIDE_BOTTOM ? by : 0);

		public Vector2 GetSupport(Vector2 direction)
		{
			Vector2 support = Position;
			if (direction.X > 0.0f)
				support.X += Size.X;
			if (direction.Y > 0.0f)
				support.Y += Size.Y;
			return support;
		}

		public bool IsEqualApprox(Rect2 other) => Position.IsEqualApprox(other.Position) && Size.IsEqualApprox(other.Size);
		public bool Equals(Rect2 other) => Position == other.Position && Size == other.Size;

		public static bool operator ==(Rect2 a, Rect2 b) => a.Position == b.Position && a.Size == b.Size;
	}
}
