// Godot Basis math, ported from modules/mono/glue/GodotSharp/GodotSharp/Core/Basis.cs.
// Extension over the [CRepr] layout stub (Vector3 Row0, Row1, Row2 — row-major, matching Godot).
// X/Y/Z are columns. TODO: LookingAt is not ported yet.
using System;

namespace Godot
{
	extension Basis
	{
		public Vector3 Column0 { get => this[0]; set mut => this[0] = value; }
		public Vector3 Column1 { get => this[1]; set mut => this[1] = value; }
		public Vector3 Column2 { get => this[2]; set mut => this[2] = value; }
		public Vector3 X { get => this[0]; set mut => this[0] = value; }
		public Vector3 Y { get => this[1]; set mut => this[1] = value; }
		public Vector3 Z { get => this[2]; set mut => this[2] = value; }

		public Vector3 Scale
		{
			get
			{
				float detSign = Mathf.Sign(Determinant());
				return detSign * Vector3(Column0.Length(), Column1.Length(), Column2.Length());
			}
		}

		public static Basis Identity => .(1, 0, 0, 0, 1, 0, 0, 0, 1);
		public static Basis FlipX => .(-1, 0, 0, 0, 1, 0, 0, 0, 1);
		public static Basis FlipY => .(1, 0, 0, 0, -1, 0, 0, 0, 1);
		public static Basis FlipZ => .(1, 0, 0, 0, 1, 0, 0, 0, -1);

		public this(Vector3 column0, Vector3 column1, Vector3 column2)
		{
			Row0 = .(column0.X, column1.X, column2.X);
			Row1 = .(column0.Y, column1.Y, column2.Y);
			Row2 = .(column0.Z, column1.Z, column2.Z);
		}

		public this(float xx, float yx, float zx, float xy, float yy, float zy, float xz, float yz, float zz)
		{
			Row0 = .(xx, yx, zx);
			Row1 = .(xy, yy, zy);
			Row2 = .(xz, yz, zz);
		}

		public this(Quaternion q)
		{
			float s = 2.0f / q.LengthSquared();
			float xs = q.X * s; float ys = q.Y * s; float zs = q.Z * s;
			float wx = q.W * xs; float wy = q.W * ys; float wz = q.W * zs;
			float xx = q.X * xs; float xy = q.X * ys; float xz = q.X * zs;
			float yy = q.Y * ys; float yz = q.Y * zs; float zz = q.Z * zs;
			Row0 = .(1.0f - (yy + zz), xy - wz, xz + wy);
			Row1 = .(xy + wz, 1.0f - (xx + zz), yz - wx);
			Row2 = .(xz - wy, yz + wx, 1.0f - (xx + yy));
		}

		public this(Vector3 axis, float angle)
		{
			Vector3 axisSq = .(axis.X * axis.X, axis.Y * axis.Y, axis.Z * axis.Z);
			(float sin, float cos) = Mathf.SinCos(angle);
			float t = 1.0f - cos;
			float m00 = axisSq.X + cos * (1.0f - axisSq.X);
			float m11 = axisSq.Y + cos * (1.0f - axisSq.Y);
			float m22 = axisSq.Z + cos * (1.0f - axisSq.Z);
			float xyt = axis.X * axis.Y * t; float zs = axis.Z * sin;
			float m01 = xyt - zs; float m10 = xyt + zs;
			float xzt = axis.X * axis.Z * t; float ys = axis.Y * sin;
			float m02 = xzt + ys; float m20 = xzt - ys;
			float yzt = axis.Y * axis.Z * t; float xs = axis.X * sin;
			float m12 = yzt - xs; float m21 = yzt + xs;
			Row0 = .(m00, m01, m02);
			Row1 = .(m10, m11, m12);
			Row2 = .(m20, m21, m22);
		}

		public static Basis FromScale(Vector3 scale) => .(scale.X, 0, 0, 0, scale.Y, 0, 0, 0, scale.Z);

		public static Basis LookingAt(Vector3 target, Vector3 up, bool useModelFront = false)
		{
			Vector3 column2 = target.Normalized();
			if (!useModelFront)
				column2 = -column2;
			Vector3 column0 = up.Cross(column2).Normalized();
			Vector3 column1 = column2.Cross(column0);
			return .(column0, column1, column2);
		}
		public static Basis LookingAt(Vector3 target) => LookingAt(target, Vector3.Up, false);

		public Vector3 this[int column]
		{
			get
			{
				switch (column)
				{
				case 0: return .(Row0.X, Row1.X, Row2.X);
				case 1: return .(Row0.Y, Row1.Y, Row2.Y);
				default: return .(Row0.Z, Row1.Z, Row2.Z);
				}
			}
			set mut
			{
				switch (column)
				{
				case 0: Row0.X = value.X; Row1.X = value.Y; Row2.X = value.Z;
				case 1: Row0.Y = value.X; Row1.Y = value.Y; Row2.Y = value.Z;
				case 2: Row0.Z = value.X; Row1.Z = value.Y; Row2.Z = value.Z;
				}
			}
		}

		public float this[int column, int row]
		{
			get => this[column][row];
			set mut { Vector3 c = this[column]; c[row] = value; this[column] = c; }
		}

		public float Determinant()
		{
			float cofac00 = Row1[1] * Row2[2] - Row1[2] * Row2[1];
			float cofac10 = Row1[2] * Row2[0] - Row1[0] * Row2[2];
			float cofac20 = Row1[0] * Row2[1] - Row1[1] * Row2[0];
			return Row0[0] * cofac00 + Row0[1] * cofac10 + Row0[2] * cofac20;
		}

		public Basis Inverse()
		{
			float cofac00 = Row1[1] * Row2[2] - Row1[2] * Row2[1];
			float cofac10 = Row1[2] * Row2[0] - Row1[0] * Row2[2];
			float cofac20 = Row1[0] * Row2[1] - Row1[1] * Row2[0];
			float det = Row0[0] * cofac00 + Row0[1] * cofac10 + Row0[2] * cofac20;
			if (det == 0)
				return Identity;
			float detInv = 1.0f / det;
			float cofac01 = Row0[2] * Row2[1] - Row0[1] * Row2[2];
			float cofac02 = Row0[1] * Row1[2] - Row0[2] * Row1[1];
			float cofac11 = Row0[0] * Row2[2] - Row0[2] * Row2[0];
			float cofac12 = Row0[2] * Row1[0] - Row0[0] * Row1[2];
			float cofac21 = Row0[1] * Row2[0] - Row0[0] * Row2[1];
			float cofac22 = Row0[0] * Row1[1] - Row0[1] * Row1[0];
			return .(
				cofac00 * detInv, cofac01 * detInv, cofac02 * detInv,
				cofac10 * detInv, cofac11 * detInv, cofac12 * detInv,
				cofac20 * detInv, cofac21 * detInv, cofac22 * detInv);
		}

		public Basis Transposed() => .(Row0.X, Row1.X, Row2.X, Row0.Y, Row1.Y, Row2.Y, Row0.Z, Row1.Z, Row2.Z);
		public float Tdotx(Vector3 with) => Row0[0] * with[0] + Row1[0] * with[1] + Row2[0] * with[2];
		public float Tdoty(Vector3 with) => Row0[1] * with[0] + Row1[1] * with[1] + Row2[1] * with[2];
		public float Tdotz(Vector3 with) => Row0[2] * with[0] + Row1[2] * with[1] + Row2[2] * with[2];

		public Basis Scaled(Vector3 scale) => .(
			Row0.X * scale.X, Row0.Y * scale.X, Row0.Z * scale.X,
			Row1.X * scale.Y, Row1.Y * scale.Y, Row1.Z * scale.Y,
			Row2.X * scale.Z, Row2.Y * scale.Z, Row2.Z * scale.Z);

		public Basis Rotated(Vector3 axis, float angle) => Basis(axis, angle) * this;

		public Basis Orthonormalized()
		{
			Vector3 column0 = this[0];
			Vector3 column1 = this[1];
			Vector3 column2 = this[2];
			column0 = column0.Normalized();
			column1 = (column1 - column0 * column0.Dot(column1)).Normalized();
			column2 = (column2 - column0 * column0.Dot(column2) - column1 * column1.Dot(column2)).Normalized();
			return .(column0, column1, column2);
		}

		public Quaternion GetQuaternion()
		{
			float trace = Row0[0] + Row1[1] + Row2[2];
			if (trace > 0.0f)
			{
				float s = Mathf.Sqrt(trace + 1.0f) * 2f;
				float invS = 1f / s;
				return .((Row2[1] - Row1[2]) * invS, (Row0[2] - Row2[0]) * invS, (Row1[0] - Row0[1]) * invS, s * 0.25f);
			}
			if (Row0[0] > Row1[1] && Row0[0] > Row2[2])
			{
				float s = Mathf.Sqrt(Row0[0] - Row1[1] - Row2[2] + 1.0f) * 2f;
				float invS = 1f / s;
				return .(s * 0.25f, (Row0[1] + Row1[0]) * invS, (Row0[2] + Row2[0]) * invS, (Row2[1] - Row1[2]) * invS);
			}
			if (Row1[1] > Row2[2])
			{
				float s = Mathf.Sqrt(-Row0[0] + Row1[1] - Row2[2] + 1.0f) * 2f;
				float invS = 1f / s;
				return .((Row0[1] + Row1[0]) * invS, s * 0.25f, (Row1[2] + Row2[1]) * invS, (Row0[2] - Row2[0]) * invS);
			}
			float s = Mathf.Sqrt(-Row0[0] - Row1[1] + Row2[2] + 1.0f) * 2f;
			float invS = 1f / s;
			return .((Row0[2] + Row2[0]) * invS, (Row1[2] + Row2[1]) * invS, s * 0.25f, (Row1[0] - Row0[1]) * invS);
		}

		public Quaternion GetRotationQuaternion()
		{
			Basis ortho = Orthonormalized();
			if (ortho.Determinant() < 0)
				ortho = ortho.Scaled(-Vector3.One);
			return ortho.GetQuaternion();
		}

		public Basis Slerp(Basis target, float weight)
		{
			Quaternion from = Quaternion(this);
			Quaternion to = Quaternion(target);
			Basis b = Basis(from.Slerp(to, weight));
			b.Row0 = b.Row0 * Mathf.Lerp(Row0.Length(), target.Row0.Length(), weight);
			b.Row1 = b.Row1 * Mathf.Lerp(Row1.Length(), target.Row1.Length(), weight);
			b.Row2 = b.Row2 * Mathf.Lerp(Row2.Length(), target.Row2.Length(), weight);
			return b;
		}

		public bool IsFinite() => Row0.IsFinite() && Row1.IsFinite() && Row2.IsFinite();
		public bool IsEqualApprox(Basis other) => Row0.IsEqualApprox(other.Row0) && Row1.IsEqualApprox(other.Row1) && Row2.IsEqualApprox(other.Row2);
		public bool Equals(Basis other) => Row0 == other.Row0 && Row1 == other.Row1 && Row2 == other.Row2;

		public Vector3 GetEuler(EulerOrder order = .EULER_ORDER_YXZ)
		{
			Vector3 euler = .();
			switch (order)
			{
			case .EULER_ORDER_XYZ:
				float sy = Row0[2];
				if (sy < (1.0f - Mathf.Epsilon))
				{
					if (sy > -(1.0f - Mathf.Epsilon))
					{
						if (Row1[0] == 0 && Row0[1] == 0 && Row1[2] == 0 && Row2[1] == 0 && Row1[1] == 1)
						{
							euler.X = 0; euler.Y = Mathf.Atan2(Row0[2], Row0[0]); euler.Z = 0;
						}
						else
						{
							euler.X = Mathf.Atan2(-Row1[2], Row2[2]); euler.Y = Mathf.Asin(sy); euler.Z = Mathf.Atan2(-Row0[1], Row0[0]);
						}
					}
					else { euler.X = Mathf.Atan2(Row2[1], Row1[1]); euler.Y = -Mathf.Tau / 4.0f; euler.Z = 0; }
				}
				else { euler.X = Mathf.Atan2(Row2[1], Row1[1]); euler.Y = Mathf.Tau / 4.0f; euler.Z = 0; }
			case .EULER_ORDER_XZY:
				float sz = Row0[1];
				if (sz < (1.0f - Mathf.Epsilon))
				{
					if (sz > -(1.0f - Mathf.Epsilon))
					{
						euler.X = Mathf.Atan2(Row2[1], Row1[1]); euler.Y = Mathf.Atan2(Row0[2], Row0[0]); euler.Z = Mathf.Asin(-sz);
					}
					else { euler.X = -Mathf.Atan2(Row1[2], Row2[2]); euler.Y = 0; euler.Z = Mathf.Tau / 4.0f; }
				}
				else { euler.X = -Mathf.Atan2(Row1[2], Row2[2]); euler.Y = 0; euler.Z = -Mathf.Tau / 4.0f; }
			case .EULER_ORDER_YXZ:
				float m12 = Row1[2];
				if (m12 < (1 - Mathf.Epsilon))
				{
					if (m12 > -(1 - Mathf.Epsilon))
					{
						if (Row1[0] == 0 && Row0[1] == 0 && Row0[2] == 0 && Row2[0] == 0 && Row0[0] == 1)
						{
							euler.X = Mathf.Atan2(-m12, Row1[1]); euler.Y = 0; euler.Z = 0;
						}
						else
						{
							euler.X = Mathf.Asin(-m12); euler.Y = Mathf.Atan2(Row0[2], Row2[2]); euler.Z = Mathf.Atan2(Row1[0], Row1[1]);
						}
					}
					else { euler.X = Mathf.Tau / 4.0f; euler.Y = Mathf.Atan2(Row0[1], Row0[0]); euler.Z = 0; }
				}
				else { euler.X = -Mathf.Tau / 4.0f; euler.Y = -Mathf.Atan2(Row0[1], Row0[0]); euler.Z = 0; }
			case .EULER_ORDER_YZX:
				float sz2 = Row1[0];
				if (sz2 < (1.0f - Mathf.Epsilon))
				{
					if (sz2 > -(1.0f - Mathf.Epsilon))
					{
						euler.X = Mathf.Atan2(-Row1[2], Row1[1]); euler.Y = Mathf.Atan2(-Row2[0], Row0[0]); euler.Z = Mathf.Asin(sz2);
					}
					else { euler.X = Mathf.Atan2(Row2[1], Row2[2]); euler.Y = 0; euler.Z = -Mathf.Tau / 4.0f; }
				}
				else { euler.X = Mathf.Atan2(Row2[1], Row2[2]); euler.Y = 0; euler.Z = Mathf.Tau / 4.0f; }
			case .EULER_ORDER_ZXY:
				float sx = Row2[1];
				if (sx < (1.0f - Mathf.Epsilon))
				{
					if (sx > -(1.0f - Mathf.Epsilon))
					{
						euler.X = Mathf.Asin(sx); euler.Y = Mathf.Atan2(-Row2[0], Row2[2]); euler.Z = Mathf.Atan2(-Row0[1], Row1[1]);
					}
					else { euler.X = -Mathf.Tau / 4.0f; euler.Y = Mathf.Atan2(Row0[2], Row0[0]); euler.Z = 0; }
				}
				else { euler.X = Mathf.Tau / 4.0f; euler.Y = Mathf.Atan2(Row0[2], Row0[0]); euler.Z = 0; }
			case .EULER_ORDER_ZYX:
				float sy2 = Row2[0];
				if (sy2 < (1.0f - Mathf.Epsilon))
				{
					if (sy2 > -(1.0f - Mathf.Epsilon))
					{
						euler.X = Mathf.Atan2(Row2[1], Row2[2]); euler.Y = Mathf.Asin(-sy2); euler.Z = Mathf.Atan2(Row1[0], Row0[0]);
					}
					else { euler.X = 0; euler.Y = Mathf.Tau / 4.0f; euler.Z = -Mathf.Atan2(Row0[1], Row1[1]); }
				}
				else { euler.X = 0; euler.Y = -Mathf.Tau / 4.0f; euler.Z = -Mathf.Atan2(Row0[1], Row1[1]); }
			}
			return euler;
		}

		public static Basis FromEuler(Vector3 euler, EulerOrder order = .EULER_ORDER_YXZ)
		{
			(float sx, float cx) = Mathf.SinCos(euler.X);
			Basis xmat = .(Vector3(1, 0, 0), Vector3(0, cx, sx), Vector3(0, -sx, cx));
			(float sy, float cy) = Mathf.SinCos(euler.Y);
			Basis ymat = .(Vector3(cy, 0, -sy), Vector3(0, 1, 0), Vector3(sy, 0, cy));
			(float sz, float cz) = Mathf.SinCos(euler.Z);
			Basis zmat = .(Vector3(cz, sz, 0), Vector3(-sz, cz, 0), Vector3(0, 0, 1));
			switch (order)
			{
			case .EULER_ORDER_XYZ: return xmat * ymat * zmat;
			case .EULER_ORDER_XZY: return xmat * zmat * ymat;
			case .EULER_ORDER_YXZ: return ymat * xmat * zmat;
			case .EULER_ORDER_YZX: return ymat * zmat * xmat;
			case .EULER_ORDER_ZXY: return zmat * xmat * ymat;
			case .EULER_ORDER_ZYX: return zmat * ymat * xmat;
			}
		}

		public static Basis operator *(Basis l, Basis r) => .(
			r.Tdotx(l.Row0), r.Tdoty(l.Row0), r.Tdotz(l.Row0),
			r.Tdotx(l.Row1), r.Tdoty(l.Row1), r.Tdotz(l.Row1),
			r.Tdotx(l.Row2), r.Tdoty(l.Row2), r.Tdotz(l.Row2));

		public static Vector3 operator *(Basis basis, Vector3 v) => .(basis.Row0.Dot(v), basis.Row1.Dot(v), basis.Row2.Dot(v));

		public static Vector3 operator *(Vector3 v, Basis basis) => .(
			basis.Row0[0] * v.X + basis.Row1[0] * v.Y + basis.Row2[0] * v.Z,
			basis.Row0[1] * v.X + basis.Row1[1] * v.Y + basis.Row2[1] * v.Z,
			basis.Row0[2] * v.X + basis.Row1[2] * v.Y + basis.Row2[2] * v.Z);

		public static bool operator ==(Basis a, Basis b) => a.Row0 == b.Row0 && a.Row1 == b.Row1 && a.Row2 == b.Row2;
	}
}
