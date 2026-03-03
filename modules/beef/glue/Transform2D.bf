// Godot Transform2D math, ported from modules/mono/glue/GodotSharp/GodotSharp/Core/Transform2D.cs.
// Extension over the [CRepr] layout stub (Vector2 X, Y, Origin) in GodotPrimitives.bf.
// X/Y are the basis columns. TODO: Skew, the *Local variants, InterpolateWith, Orthonormalized.
using System;

namespace Godot
{
	extension Transform2D
	{
		public float Rotation => Mathf.Atan2(X.Y, X.X);
		public Vector2 Scale
		{
			get
			{
				float detSign = Mathf.Sign(Determinant());
				return .(X.Length(), detSign * Y.Length());
			}
		}
		public float Skew
		{
			get
			{
				float detSign = Mathf.Sign(Determinant());
				return Mathf.Acos(X.Normalized().Dot(detSign * Y.Normalized())) - Mathf.Pi * 0.5f;
			}
		}

		public static Transform2D Identity => .(Vector2(1, 0), Vector2(0, 1), Vector2(0, 0));
		public static Transform2D FlipX => .(Vector2(-1, 0), Vector2(0, 1), Vector2(0, 0));
		public static Transform2D FlipY => .(Vector2(1, 0), Vector2(0, -1), Vector2(0, 0));

		public this(Vector2 xAxis, Vector2 yAxis, Vector2 originPos)
		{
			X = xAxis;
			Y = yAxis;
			Origin = originPos;
		}
		public this(float xx, float xy, float yx, float yy, float ox, float oy)
		{
			X = .(xx, xy);
			Y = .(yx, yy);
			Origin = .(ox, oy);
		}
		public this(float rotation, Vector2 origin)
		{
			(float sin, float cos) = Mathf.SinCos(rotation);
			X = .(cos, sin);
			Y = .(-sin, cos);
			Origin = origin;
		}
		public this(float rotation, Vector2 scale, float skew, Vector2 origin)
		{
			(float rSin, float rCos) = Mathf.SinCos(rotation);
			(float rsSin, float rsCos) = Mathf.SinCos(rotation + skew);
			X = .(rCos * scale.X, rSin * scale.X);
			Y = .(-rsSin * scale.Y, rsCos * scale.Y);
			Origin = origin;
		}

		public Vector2 this[int column]
		{
			get { switch (column) { case 0: return X; case 1: return Y; default: return Origin; } }
			set mut { switch (column) { case 0: X = value; case 1: Y = value; case 2: Origin = value; } }
		}

		public float this[int column, int row]
		{
			get => this[column][row];
			set mut { Vector2 c = this[column]; c[row] = value; this[column] = c; }
		}

		public float Determinant() => (X.X * Y.Y) - (X.Y * Y.X);
		public float Tdotx(Vector2 w) => (X.X * w.X) + (Y.X * w.Y);
		public float Tdoty(Vector2 w) => (X.Y * w.X) + (Y.Y * w.Y);
		public Vector2 BasisXform(Vector2 v) => .(Tdotx(v), Tdoty(v));
		public Vector2 BasisXformInv(Vector2 v) => .(X.Dot(v), Y.Dot(v));
		public bool IsFinite() => X.IsFinite() && Y.IsFinite() && Origin.IsFinite();
		public bool IsEqualApprox(Transform2D other) => X.IsEqualApprox(other.X) && Y.IsEqualApprox(other.Y) && Origin.IsEqualApprox(other.Origin);
		public bool Equals(Transform2D other) => X == other.X && Y == other.Y && Origin == other.Origin;

		public Transform2D AffineInverse()
		{
			float det = Determinant();
			if (det == 0)
				return Identity;
			Transform2D inv = this;
			inv.X.X = Y.Y;
			inv.Y.Y = X.X;
			float detInv = 1.0f / det;
			inv.X = inv.X * Vector2(detInv, -detInv);
			inv.Y = inv.Y * Vector2(-detInv, detInv);
			inv.Origin = inv.BasisXform(-inv.Origin);
			return inv;
		}

		public Transform2D Inverse()
		{
			Transform2D inv = this;
			inv.X.Y = Y.X;
			inv.Y.X = X.Y;
			inv.Origin = inv.BasisXform(-inv.Origin);
			return inv;
		}

		public Transform2D Rotated(float angle) => Transform2D(angle, Vector2(0, 0)) * this;
		public Transform2D RotatedLocal(float angle) => this * Transform2D(angle, Vector2(0, 0));

		public Transform2D Scaled(Vector2 scale)
		{
			Transform2D copy = this;
			copy.X = copy.X * scale;
			copy.Y = copy.Y * scale;
			copy.Origin = copy.Origin * scale;
			return copy;
		}

		public Transform2D Translated(Vector2 offset)
		{
			Transform2D copy = this;
			copy.Origin = copy.Origin + offset;
			return copy;
		}

		public Transform2D ScaledLocal(Vector2 scale)
		{
			Transform2D copy = this;
			copy.X = copy.X * scale;
			copy.Y = copy.Y * scale;
			return copy;
		}

		public Transform2D TranslatedLocal(Vector2 offset)
		{
			Transform2D copy = this;
			copy.Origin = copy.Origin + copy.BasisXform(offset);
			return copy;
		}

		public Transform2D Orthonormalized()
		{
			Transform2D ortho = this;
			Vector2 orthoX = ortho.X.Normalized();
			Vector2 orthoY = ortho.Y - orthoX * orthoX.Dot(ortho.Y);
			orthoY = orthoY.Normalized();
			ortho.X = orthoX;
			ortho.Y = orthoY;
			return ortho;
		}

		public Transform2D InterpolateWith(Transform2D transform, float weight) => .(
			Mathf.LerpAngle(Rotation, transform.Rotation, weight),
			Scale.Lerp(transform.Scale, weight),
			Mathf.LerpAngle(Skew, transform.Skew, weight),
			Origin.Lerp(transform.Origin, weight));

		public static Transform2D operator *(Transform2D left, Transform2D right)
		{
			Transform2D r = left;
			r.Origin = left * right.Origin;
			float x0 = left.Tdotx(right.X);
			float x1 = left.Tdoty(right.X);
			float y0 = left.Tdotx(right.Y);
			float y1 = left.Tdoty(right.Y);
			r.X = .(x0, x1);
			r.Y = .(y0, y1);
			return r;
		}

		public static Vector2 operator *(Transform2D transform, Vector2 vector) => Vector2(transform.Tdotx(vector), transform.Tdoty(vector)) + transform.Origin;

		public static bool operator ==(Transform2D a, Transform2D b) => a.X == b.X && a.Y == b.Y && a.Origin == b.Origin;
	}
}
