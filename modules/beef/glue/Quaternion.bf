// Godot Quaternion math, ported from modules/mono/glue/GodotSharp/GodotSharp/Core/Quaternion.cs.
// Extension over the [CRepr] layout stub (float X, Y, Z, W) in GodotPrimitives.bf.
// TODO: SphericalCubicInterpolate(+InTime) not ported yet.
using System;

namespace Godot
{
	extension Quaternion
	{
		public static Quaternion Identity => .(0, 0, 0, 1);

		public this(float x, float y, float z, float w)
		{
			X = x;
			Y = y;
			Z = z;
			W = w;
		}

		public this(Basis basis)
		{
			this = basis.GetQuaternion();
		}

		public this(Vector3 axis, float angle)
		{
			float d = axis.Length();
			if (d == 0f)
			{
				X = 0; Y = 0; Z = 0; W = 0;
			}
			else
			{
				(float sin, float cos) = Mathf.SinCos(angle * 0.5f);
				float s = sin / d;
				X = axis.X * s;
				Y = axis.Y * s;
				Z = axis.Z * s;
				W = cos;
			}
		}

		public this(Vector3 arcFrom, Vector3 arcTo)
		{
			const float AlmostOne = 0.99999975f;
			Vector3 n0 = arcFrom.Normalized();
			Vector3 n1 = arcTo.Normalized();
			float d = n0.Dot(n1);
			float qx = 0; float qy = 0; float qz = 0; float qw = 1;
			if (Mathf.Abs(d) > AlmostOne)
			{
				if (d < 0.0f)
				{
					Vector3 perp = Mathf.Abs(n0.X) < 0.9f ? Vector3(1, 0, 0) : Vector3(0, 1, 0);
					Vector3 axis = n0.Cross(perp).Normalized();
					qx = axis.X; qy = axis.Y; qz = axis.Z; qw = 0;
				}
			}
			else
			{
				Vector3 c = n0.Cross(n1);
				float s = Mathf.Sqrt((1.0f + d) * 2.0f);
				float rs = 1.0f / s;
				qx = c.X * rs; qy = c.Y * rs; qz = c.Z * rs; qw = s * 0.5f;
			}
			float len = Mathf.Sqrt(qx * qx + qy * qy + qz * qz + qw * qw);
			if (len == 0)
				len = 1;
			X = qx / len; Y = qy / len; Z = qz / len; W = qw / len;
		}

		public static Quaternion FromEuler(Vector3 eulerYXZ)
		{
			float halfA1 = eulerYXZ.Y * 0.5f;
			float halfA2 = eulerYXZ.X * 0.5f;
			float halfA3 = eulerYXZ.Z * 0.5f;
			(float sinA1, float cosA1) = Mathf.SinCos(halfA1);
			(float sinA2, float cosA2) = Mathf.SinCos(halfA2);
			(float sinA3, float cosA3) = Mathf.SinCos(halfA3);
			return .(
				(sinA1 * cosA2 * sinA3) + (cosA1 * sinA2 * cosA3),
				(sinA1 * cosA2 * cosA3) - (cosA1 * sinA2 * sinA3),
				(cosA1 * cosA2 * sinA3) - (sinA1 * sinA2 * cosA3),
				(sinA1 * sinA2 * sinA3) + (cosA1 * cosA2 * cosA3));
		}

		public float this[int index]
		{
			get { switch (index) { case 0: return X; case 1: return Y; case 2: return Z; default: return W; } }
			set mut { switch (index) { case 0: X = value; case 1: Y = value; case 2: Z = value; case 3: W = value; } }
		}

		public float Dot(Quaternion b) => (X * b.X) + (Y * b.Y) + (Z * b.Z) + (W * b.W);
		public float Length() => Mathf.Sqrt(LengthSquared());
		public float LengthSquared() => Dot(this);
		public Quaternion Normalized() => this / Length();
		public bool IsNormalized() => Mathf.Abs(LengthSquared() - 1) <= 2 * Mathf.Epsilon;
		public bool IsFinite() => Mathf.IsFinite(X) && Mathf.IsFinite(Y) && Mathf.IsFinite(Z) && Mathf.IsFinite(W);
		public bool IsEqualApprox(Quaternion other) => Mathf.IsEqualApprox(X, other.X) && Mathf.IsEqualApprox(Y, other.Y) && Mathf.IsEqualApprox(Z, other.Z) && Mathf.IsEqualApprox(W, other.W);
		public bool Equals(Quaternion other) => X == other.X && Y == other.Y && Z == other.Z && W == other.W;

		public Quaternion Inverse() => .(-X, -Y, -Z, W);
		public float AngleTo(Quaternion to) { float d = Dot(to); return Mathf.Acos(Mathf.Clamp(d * d * 2 - 1, -1, 1)); }
		public float GetAngle() => 2 * Mathf.Acos(W);
		public Vector3 GetEuler(EulerOrder order = .EULER_ORDER_YXZ) => Basis(this).GetEuler(order);

		public Vector3 GetAxis()
		{
			if (Mathf.Abs(W) > 1 - Mathf.Epsilon)
				return .(X, Y, Z);
			float r = 1 / Mathf.Sqrt(1 - W * W);
			return .(X * r, Y * r, Z * r);
		}

		public Quaternion Exp()
		{
			Vector3 v = .(X, Y, Z);
			float theta = v.Length();
			v = v.Normalized();
			if (theta < Mathf.Epsilon || !v.IsNormalized())
				return .(0, 0, 0, 1);
			return .(v, theta);
		}

		public Quaternion Log()
		{
			Vector3 v = GetAxis() * GetAngle();
			return .(v.X, v.Y, v.Z, 0);
		}

		public Quaternion Slerp(Quaternion to, float weight)
		{
			float cosom = Dot(to);
			Quaternion to1;
			if (cosom < 0.0f)
			{
				cosom = -cosom;
				to1 = -to;
			}
			else
			{
				to1 = to;
			}
			float scale0;
			float scale1;
			if (1.0f - cosom > Mathf.Epsilon)
			{
				float omega = Mathf.Acos(cosom);
				float sinom = Mathf.Sin(omega);
				scale0 = Mathf.Sin((1.0f - weight) * omega) / sinom;
				scale1 = Mathf.Sin(weight * omega) / sinom;
			}
			else
			{
				scale0 = 1.0f - weight;
				scale1 = weight;
			}
			return .(
				(scale0 * X) + (scale1 * to1.X),
				(scale0 * Y) + (scale1 * to1.Y),
				(scale0 * Z) + (scale1 * to1.Z),
				(scale0 * W) + (scale1 * to1.W));
		}

		public Quaternion Slerpni(Quaternion to, float weight)
		{
			float dot = Dot(to);
			if (Mathf.Abs(dot) > 0.9999f)
				return this;
			float theta = Mathf.Acos(dot);
			float sinT = 1.0f / Mathf.Sin(theta);
			float newFactor = Mathf.Sin(weight * theta) * sinT;
			float invFactor = Mathf.Sin((1.0f - weight) * theta) * sinT;
			return .(
				(invFactor * X) + (newFactor * to.X),
				(invFactor * Y) + (newFactor * to.Y),
				(invFactor * Z) + (newFactor * to.Z),
				(invFactor * W) + (newFactor * to.W));
		}

		public Quaternion SphericalCubicInterpolate(Quaternion b, Quaternion preA, Quaternion postB, float weight)
		{
			Quaternion fromQ = Basis(this).GetRotationQuaternion();
			Quaternion preQ = Basis(preA).GetRotationQuaternion();
			Quaternion toQ = Basis(b).GetRotationQuaternion();
			Quaternion postQ = Basis(postB).GetRotationQuaternion();

			bool flip1 = Mathf.Sign(fromQ.Dot(preQ)) < 0;
			preQ = flip1 ? -preQ : preQ;
			bool flip2 = Mathf.Sign(fromQ.Dot(toQ)) < 0;
			toQ = flip2 ? -toQ : toQ;
			bool flip3 = flip2 ? toQ.Dot(postQ) <= 0 : Mathf.Sign(toQ.Dot(postQ)) < 0;
			postQ = flip3 ? -postQ : postQ;

			Quaternion lnFrom = .(0, 0, 0, 0);
			Quaternion lnTo = (fromQ.Inverse() * toQ).Log();
			Quaternion lnPre = (fromQ.Inverse() * preQ).Log();
			Quaternion lnPost = (fromQ.Inverse() * postQ).Log();
			Quaternion ln = .(
				Mathf.CubicInterpolate(lnFrom.X, lnTo.X, lnPre.X, lnPost.X, weight),
				Mathf.CubicInterpolate(lnFrom.Y, lnTo.Y, lnPre.Y, lnPost.Y, weight),
				Mathf.CubicInterpolate(lnFrom.Z, lnTo.Z, lnPre.Z, lnPost.Z, weight), 0);
			Quaternion q1 = fromQ * ln.Exp();

			lnFrom = (toQ.Inverse() * fromQ).Log();
			lnTo = .(0, 0, 0, 0);
			lnPre = (toQ.Inverse() * preQ).Log();
			lnPost = (toQ.Inverse() * postQ).Log();
			ln = .(
				Mathf.CubicInterpolate(lnFrom.X, lnTo.X, lnPre.X, lnPost.X, weight),
				Mathf.CubicInterpolate(lnFrom.Y, lnTo.Y, lnPre.Y, lnPost.Y, weight),
				Mathf.CubicInterpolate(lnFrom.Z, lnTo.Z, lnPre.Z, lnPost.Z, weight), 0);
			Quaternion q2 = toQ * ln.Exp();

			return q1.Slerp(q2, weight);
		}

		public static Quaternion operator *(Quaternion l, Quaternion r) => .(
			(l.W * r.X) + (l.X * r.W) + (l.Y * r.Z) - (l.Z * r.Y),
			(l.W * r.Y) + (l.Y * r.W) + (l.Z * r.X) - (l.X * r.Z),
			(l.W * r.Z) + (l.Z * r.W) + (l.X * r.Y) - (l.Y * r.X),
			(l.W * r.W) - (l.X * r.X) - (l.Y * r.Y) - (l.Z * r.Z));

		public static Vector3 operator *(Quaternion q, Vector3 vector)
		{
			Vector3 u = .(q.X, q.Y, q.Z);
			Vector3 uv = u.Cross(vector);
			return vector + (((uv * q.W) + u.Cross(uv)) * 2);
		}

		public static Quaternion operator +(Quaternion a, Quaternion b) => .(a.X + b.X, a.Y + b.Y, a.Z + b.Z, a.W + b.W);
		public static Quaternion operator -(Quaternion a, Quaternion b) => .(a.X - b.X, a.Y - b.Y, a.Z - b.Z, a.W - b.W);
		public static Quaternion operator -(Quaternion q) => .(-q.X, -q.Y, -q.Z, -q.W);
		public static Quaternion operator *(Quaternion q, float s) => .(q.X * s, q.Y * s, q.Z * s, q.W * s);
		public static Quaternion operator *(float s, Quaternion q) => .(q.X * s, q.Y * s, q.Z * s, q.W * s);
		public static Quaternion operator /(Quaternion q, float s) => .(q.X / s, q.Y / s, q.Z / s, q.W / s);
		public static bool operator ==(Quaternion a, Quaternion b) => a.X == b.X && a.Y == b.Y && a.Z == b.Z && a.W == b.W;
	}
}
