// Godot Projection math, ported from modules/mono/glue/GodotSharp/GodotSharp/Core/Projection.cs.
// Extension over the [CRepr] layout stub (Vector4 X, Y, Z, W — matrix columns) in GodotPrimitives.bf.
using System;

namespace Godot
{
	extension Projection
	{
		public enum Planes { Near, Far, Left, Top, Right, Bottom }

		public static Projection Zero => .(Vector4.Zero, Vector4.Zero, Vector4.Zero, Vector4.Zero);
		public static Projection Identity => .(Vector4(1, 0, 0, 0), Vector4(0, 1, 0, 0), Vector4(0, 0, 1, 0), Vector4(0, 0, 0, 1));

		public this(Vector4 x, Vector4 y, Vector4 z, Vector4 w)
		{
			X = x;
			Y = y;
			Z = z;
			W = w;
		}
		public this(float xx, float xy, float xz, float xw, float yx, float yy, float yz, float yw,
			float zx, float zy, float zz, float zw, float wx, float wy, float wz, float ww)
		{
			X = .(xx, xy, xz, xw);
			Y = .(yx, yy, yz, yw);
			Z = .(zx, zy, zz, zw);
			W = .(wx, wy, wz, ww);
		}
		public this(Transform3D transform)
		{
			X = .(transform.Basis.Row0.X, transform.Basis.Row1.X, transform.Basis.Row2.X, 0);
			Y = .(transform.Basis.Row0.Y, transform.Basis.Row1.Y, transform.Basis.Row2.Y, 0);
			Z = .(transform.Basis.Row0.Z, transform.Basis.Row1.Z, transform.Basis.Row2.Z, 0);
			W = .(transform.Origin.X, transform.Origin.Y, transform.Origin.Z, 1);
		}

		public Vector4 this[int column]
		{
			get { switch (column) { case 0: return X; case 1: return Y; case 2: return Z; default: return W; } }
			set mut { switch (column) { case 0: X = value; case 1: Y = value; case 2: Z = value; case 3: W = value; } }
		}

		public float this[int column, int row]
		{
			get => this[column][row];
			set mut { Vector4 c = this[column]; c[row] = value; this[column] = c; }
		}

		public static float GetFovy(float fovx, float aspect) => Mathf.RadToDeg(Mathf.Atan(aspect * Mathf.Tan(Mathf.DegToRad(fovx) * 0.5f)) * 2.0f);

		public static Projection CreateDepthCorrection(bool flipY) => .(
			Vector4(1, 0, 0, 0), Vector4(0, flipY ? -1 : 1, 0, 0), Vector4(0, 0, 0.5f, 0), Vector4(0, 0, 0.5f, 1));

		public static Projection CreateFitAabb(Aabb aabb)
		{
			Vector3 min = aabb.Position;
			Vector3 max = aabb.Position + aabb.Size;
			return .(
				Vector4(2 / (max.X - min.X), 0, 0, 0),
				Vector4(0, 2 / (max.Y - min.Y), 0, 0),
				Vector4(0, 0, 2 / (max.Z - min.Z), 0),
				Vector4(-(max.X + min.X) / (max.X - min.X), -(max.Y + min.Y) / (max.Y - min.Y), -(max.Z + min.Z) / (max.Z - min.Z), 1));
		}

		public static Projection CreateLightAtlasRect(Rect2 rect) => .(
			Vector4(rect.Size.X, 0, 0, 0), Vector4(0, rect.Size.Y, 0, 0), Vector4(0, 0, 1, 0), Vector4(rect.Position.X, rect.Position.Y, 0, 1));

		public static Projection CreateFrustum(float left, float right, float bottom, float top, float depthNear, float depthFar)
		{
			float x = 2 * depthNear / (right - left);
			float y = 2 * depthNear / (top - bottom);
			float a = (right + left) / (right - left);
			float b = (top + bottom) / (top - bottom);
			float c = -(depthFar + depthNear) / (depthFar - depthNear);
			float d = -2 * depthFar * depthNear / (depthFar - depthNear);
			return .(Vector4(x, 0, 0, 0), Vector4(0, y, 0, 0), Vector4(a, b, c, -1), Vector4(0, 0, d, 0));
		}

		public static Projection CreateFrustumAspect(float size, float aspect, Vector2 offset, float depthNear, float depthFar, bool flipFov)
		{
			float s = flipFov ? size : size * aspect;
			return CreateFrustum(-s / 2 + offset.X, s / 2 + offset.X, -s / aspect / 2 + offset.Y, s / aspect / 2 + offset.Y, depthNear, depthFar);
		}

		public static Projection CreateOrthogonal(float left, float right, float bottom, float top, float zNear, float zFar)
		{
			Projection proj = Identity;
			proj.X.X = 2.0f / (right - left);
			proj.W.X = -((right + left) / (right - left));
			proj.Y.Y = 2.0f / (top - bottom);
			proj.W.Y = -((top + bottom) / (top - bottom));
			proj.Z.Z = -2.0f / (zFar - zNear);
			proj.W.Z = -((zFar + zNear) / (zFar - zNear));
			proj.W.W = 1.0f;
			return proj;
		}

		public static Projection CreateOrthogonalAspect(float size, float aspect, float zNear, float zFar, bool flipFov)
		{
			float s = flipFov ? size : size * aspect;
			return CreateOrthogonal(-s / 2, s / 2, -s / aspect / 2, s / aspect / 2, zNear, zFar);
		}

		public static Projection CreatePerspective(float fovyDegrees, float aspect, float zNear, float zFar, bool flipFov)
		{
			float fovy = flipFov ? GetFovy(fovyDegrees, 1.0f / aspect) : fovyDegrees;
			float radians = Mathf.DegToRad(fovy / 2.0f);
			float deltaZ = zFar - zNear;
			(float sin, float cos) = Mathf.SinCos(radians);
			if ((deltaZ == 0) || (sin == 0) || (aspect == 0))
				return Zero;
			float cotangent = cos / sin;
			Projection proj = Identity;
			proj.X.X = cotangent / aspect;
			proj.Y.Y = cotangent;
			proj.Z.Z = -(zFar + zNear) / deltaZ;
			proj.Z.W = -1;
			proj.W.Z = -2 * zNear * zFar / deltaZ;
			proj.W.W = 0;
			return proj;
		}

		public static Projection CreateForHmd(int eye, float aspect, float intraocularDist, float displayWidth, float displayToLens, float oversample, float zNear, float zFar)
		{
			float f1 = (intraocularDist * 0.5f) / displayToLens;
			float f2 = ((displayWidth - intraocularDist) * 0.5f) / displayToLens;
			float f3 = (displayWidth / 4.0f) / displayToLens;
			float add = ((f1 + f2) * (oversample - 1.0f)) / 2.0f;
			f1 += add;
			f2 += add;
			f3 *= oversample;
			f3 /= aspect;
			switch (eye)
			{
			case 1: return CreateFrustum(-f2 * zNear, f1 * zNear, -f3 * zNear, f3 * zNear, zNear, zFar);
			case 2: return CreateFrustum(-f1 * zNear, f2 * zNear, -f3 * zNear, f3 * zNear, zNear, zFar);
			default: return Zero;
			}
		}

		public static Projection CreatePerspectiveHmd(float fovyDegrees, float aspect, float zNear, float zFar, bool flipFov, int eye, float intraocularDist, float convergenceDist)
		{
			float fovy = flipFov ? GetFovy(fovyDegrees, 1.0f / aspect) : fovyDegrees;
			float ymax = zNear * Mathf.Tan(Mathf.DegToRad(fovy / 2.0f));
			float xmax = ymax * aspect;
			float frustumshift = (intraocularDist / 2.0f) * zNear / convergenceDist;
			float left;
			float right;
			float modeltranslation;
			switch (eye)
			{
			case 1: left = -xmax + frustumshift; right = xmax + frustumshift; modeltranslation = intraocularDist / 2.0f;
			case 2: left = -xmax - frustumshift; right = xmax - frustumshift; modeltranslation = -intraocularDist / 2.0f;
			default: left = -xmax; right = xmax; modeltranslation = 0.0f;
			}
			Projection proj = CreateFrustum(left, right, -ymax, ymax, zNear, zFar);
			Projection cm = Identity;
			cm.W.X = modeltranslation;
			return proj * cm;
		}

		public bool IsOrthogonal() => Z.W == 0.0f;

		public float Determinant() =>
			X.W * Y.Z * Z.Y * W.X - X.Z * Y.W * Z.Y * W.X -
			X.W * Y.Y * Z.Z * W.X + X.Y * Y.W * Z.Z * W.X +
			X.Z * Y.Y * Z.W * W.X - X.Y * Y.Z * Z.W * W.X -
			X.W * Y.Z * Z.X * W.Y + X.Z * Y.W * Z.X * W.Y +
			X.W * Y.X * Z.Z * W.Y - X.X * Y.W * Z.Z * W.Y -
			X.Z * Y.X * Z.W * W.Y + X.X * Y.Z * Z.W * W.Y +
			X.W * Y.Y * Z.X * W.Z - X.Y * Y.W * Z.X * W.Z -
			X.W * Y.X * Z.Y * W.Z + X.X * Y.W * Z.Y * W.Z +
			X.Y * Y.X * Z.W * W.Z - X.X * Y.Y * Z.W * W.Z -
			X.Z * Y.Y * Z.X * W.W + X.Y * Y.Z * Z.X * W.W +
			X.Z * Y.X * Z.Y * W.W - X.X * Y.Z * Z.Y * W.W -
			X.Y * Y.X * Z.Z * W.W + X.X * Y.Y * Z.Z * W.W;

		public Projection Inverse()
		{
			Projection proj = this;
			int[4] pvtI = .();
			int[4] pvtJ = .();
			float pvtVal;
			float hold;
			float det = 1.0f;
			for (int k = 0; k < 4; k++)
			{
				pvtVal = proj[k][k];
				pvtI[k] = k;
				pvtJ[k] = k;
				for (int i = k; i < 4; i++)
					for (int j = k; j < 4; j++)
						if (Mathf.Abs(proj[i][j]) > Mathf.Abs(pvtVal))
						{
							pvtI[k] = i;
							pvtJ[k] = j;
							pvtVal = proj[i][j];
						}
				det *= pvtVal;
				if (Mathf.IsZeroApprox(det))
					return Zero;
				int ii = pvtI[k];
				if (ii != k)
					for (int j = 0; j < 4; j++)
					{
						hold = -proj[k][j];
						proj[k, j] = proj[ii][j];
						proj[ii, j] = hold;
					}
				int jj = pvtJ[k];
				if (jj != k)
					for (int i = 0; i < 4; i++)
					{
						hold = -proj[i][k];
						proj[i, k] = proj[i][jj];
						proj[i, jj] = hold;
					}
				for (int i = 0; i < 4; i++)
					if (i != k)
						proj[i, k] = proj[i][k] / (-pvtVal);
				for (int i = 0; i < 4; i++)
				{
					hold = proj[i][k];
					for (int j = 0; j < 4; j++)
						if (i != k && j != k)
							proj[i, j] = proj[i][j] + hold * proj[k][j];
				}
				for (int j = 0; j < 4; j++)
					if (j != k)
						proj[k, j] = proj[k][j] / pvtVal;
				proj[k, k] = 1.0f / pvtVal;
			}
			for (int k = 2; k >= 0; k--)
			{
				int ii = pvtJ[k];
				if (ii != k)
					for (int j = 0; j < 4; j++)
					{
						hold = proj[k][j];
						proj[k, j] = -proj[ii][j];
						proj[ii, j] = hold;
					}
				int jj = pvtI[k];
				if (jj != k)
					for (int i = 0; i < 4; i++)
					{
						hold = proj[i][k];
						proj[i, k] = -proj[i][jj];
						proj[i, jj] = hold;
					}
			}
			return proj;
		}

		public Plane GetProjectionPlane(Planes plane)
		{
			Plane newPlane;
			switch (plane)
			{
			case .Near: newPlane = .(X.W + X.Z, Y.W + Y.Z, Z.W + Z.Z, W.W + W.Z);
			case .Far: newPlane = .(X.W - X.Z, Y.W - Y.Z, Z.W - Z.Z, W.W - W.Z);
			case .Left: newPlane = .(X.W + X.X, Y.W + Y.X, Z.W + Z.X, W.W + W.X);
			case .Top: newPlane = .(X.W - X.Y, Y.W - Y.Y, Z.W - Z.Y, W.W - W.Y);
			case .Right: newPlane = .(X.W - X.X, Y.W - Y.X, Z.W - Z.X, W.W - W.X);
			case .Bottom: newPlane = .(X.W + X.Y, Y.W + Y.Y, Z.W + Z.Y, W.W + W.Y);
			default: newPlane = .();
			}
			newPlane.Normal = -newPlane.Normal;
			return newPlane.Normalized();
		}

		public Vector2 GetViewportHalfExtents()
		{
			Vector3? res = GetProjectionPlane(.Near).Intersect3(GetProjectionPlane(.Right), GetProjectionPlane(.Top));
			if (res == null)
				return .();
			Vector3 r = res.Value;
			return .(r.X, r.Y);
		}

		public Vector2 GetFarPlaneHalfExtents()
		{
			Vector3? res = GetProjectionPlane(.Far).Intersect3(GetProjectionPlane(.Right), GetProjectionPlane(.Top));
			if (res == null)
				return .();
			Vector3 r = res.Value;
			return .(r.X, r.Y);
		}

		public float GetZNear() => -GetProjectionPlane(.Near).D;
		public float GetZFar() => GetProjectionPlane(.Far).D;
		public float GetAspect() { Vector2 he = GetViewportHalfExtents(); return he.X / he.Y; }

		public float GetFov()
		{
			Plane rightPlane = Plane(X.W - X.X, Y.W - Y.X, Z.W - Z.X, -W.W + W.X).Normalized();
			if (Z.X == 0 && Z.Y == 0)
				return Mathf.RadToDeg(Mathf.Acos(Mathf.Abs(rightPlane.Normal.X))) * 2.0f;
			Plane leftPlane = Plane(X.W + X.X, Y.W + Y.X, Z.W + Z.X, W.W + W.X).Normalized();
			return Mathf.RadToDeg(Mathf.Acos(Mathf.Abs(leftPlane.Normal.X))) + Mathf.RadToDeg(Mathf.Acos(Mathf.Abs(rightPlane.Normal.X)));
		}

		public float GetLodMultiplier()
		{
			if (IsOrthogonal())
				return GetViewportHalfExtents().X;
			float zn = GetZNear();
			float width = GetViewportHalfExtents().X * 2.0f;
			return 1.0f / (zn / width);
		}

		public int GetPixelsPerMeter(int forPixelWidth)
		{
			Vector3 result = this * Vector3(1, 0, -1);
			return (int)((result.X * 0.5f + 0.5f) * forPixelWidth);
		}

		public Projection FlippedY()
		{
			Projection proj = this;
			proj.Y = -proj.Y;
			return proj;
		}

		public Projection JitterOffseted(Vector2 offset)
		{
			Projection proj = this;
			proj.W.X += offset.X;
			proj.W.Y += offset.Y;
			return proj;
		}

		public Projection PerspectiveZNearAdjusted(float newZNear)
		{
			Projection proj = this;
			float zFar = GetZFar();
			float deltaZ = zFar - newZNear;
			proj.Z.Z = -(zFar + newZNear) / deltaZ;
			proj.W.Z = -2 * newZNear * zFar / deltaZ;
			return proj;
		}

		public bool IsEqualApprox(Projection other) => X.IsEqualApprox(other.X) && Y.IsEqualApprox(other.Y) && Z.IsEqualApprox(other.Z) && W.IsEqualApprox(other.W);
		public bool Equals(Projection other) => X == other.X && Y == other.Y && Z == other.Z && W == other.W;

		public static Projection operator *(Projection a, Projection b)
		{
			Projection result = ?;
			for (int j < 4)
			{
				Vector4 bj = b[j];
				result[j] = .(
					a.X.X * bj.X + a.Y.X * bj.Y + a.Z.X * bj.Z + a.W.X * bj.W,
					a.X.Y * bj.X + a.Y.Y * bj.Y + a.Z.Y * bj.Z + a.W.Y * bj.W,
					a.X.Z * bj.X + a.Y.Z * bj.Y + a.Z.Z * bj.Z + a.W.Z * bj.W,
					a.X.W * bj.X + a.Y.W * bj.Y + a.Z.W * bj.Z + a.W.W * bj.W);
			}
			return result;
		}

		public static Vector4 operator *(Projection p, Vector4 v) => .(
			p.X.X * v.X + p.Y.X * v.Y + p.Z.X * v.Z + p.W.X * v.W,
			p.X.Y * v.X + p.Y.Y * v.Y + p.Z.Y * v.Z + p.W.Y * v.W,
			p.X.Z * v.X + p.Y.Z * v.Y + p.Z.Z * v.Z + p.W.Z * v.W,
			p.X.W * v.X + p.Y.W * v.Y + p.Z.W * v.Z + p.W.W * v.W);

		public static Vector3 operator *(Projection p, Vector3 v)
		{
			Vector3 ret = .(
				p.X.X * v.X + p.Y.X * v.Y + p.Z.X * v.Z + p.W.X,
				p.X.Y * v.X + p.Y.Y * v.Y + p.Z.Y * v.Z + p.W.Y,
				p.X.Z * v.X + p.Y.Z * v.Y + p.Z.Z * v.Z + p.W.Z);
			return ret / (p.X.W * v.X + p.Y.W * v.Y + p.Z.W * v.Z + p.W.W);
		}

		public static explicit operator Transform3D(Projection proj) => .(
			Basis(
				Vector3(proj.X.X, proj.X.Y, proj.X.Z),
				Vector3(proj.Y.X, proj.Y.Y, proj.Y.Z),
				Vector3(proj.Z.X, proj.Z.Y, proj.Z.Z)),
			Vector3(proj.W.X, proj.W.Y, proj.W.Z));
	}
}
