// Godot Transform3D math, ported from modules/mono/glue/GodotSharp/GodotSharp/Core/Transform3D.cs.
// Extension over the [CRepr] layout stub (Basis Basis; Vector3 Origin) in GodotPrimitives.bf.
// TODO: LookingAt, InterpolateWith, RotatedLocal/ScaledLocal, the Transform3D(Projection) ctor.
using System;

namespace Godot
{
	extension Transform3D
	{
		// 'Basis' is also the name of a field on Transform3D, so the type must be qualified as Godot.Basis
		// when referring to its static members / constructing one.
		public static Transform3D Identity => .(Godot.Basis.Identity, Vector3.Zero);
		public static Transform3D FlipX => .(Godot.Basis.FlipX, Vector3.Zero);
		public static Transform3D FlipY => .(Godot.Basis.FlipY, Vector3.Zero);
		public static Transform3D FlipZ => .(Godot.Basis.FlipZ, Vector3.Zero);

		public this(Basis basis, Vector3 origin)
		{
			Basis = basis;
			Origin = origin;
		}
		public this(Vector3 column0, Vector3 column1, Vector3 column2, Vector3 origin)
		{
			Basis = .(column0, column1, column2);
			Origin = origin;
		}
		public this(float xx, float yx, float zx, float xy, float yy, float zy, float xz, float yz, float zz, float ox, float oy, float oz)
		{
			Basis = .(xx, yx, zx, xy, yy, zy, xz, yz, zz);
			Origin = .(ox, oy, oz);
		}
		public this(Projection projection)
		{
			Basis = .(
				projection.X.X, projection.Y.X, projection.Z.X,
				projection.X.Y, projection.Y.Y, projection.Z.Y,
				projection.X.Z, projection.Y.Z, projection.Z.Z);
			Origin = .(projection.W.X, projection.W.Y, projection.W.Z);
		}

		public Vector3 this[int column]
		{
			get => column == 3 ? Origin : Basis[column];
			set mut { if (column == 3) Origin = value; else Basis[column] = value; }
		}

		public Transform3D AffineInverse()
		{
			Godot.Basis basisInv = Basis.Inverse();
			return .(basisInv, basisInv * -Origin);
		}

		public Transform3D Inverse()
		{
			Godot.Basis basisTr = Basis.Transposed();
			return .(basisTr, basisTr * -Origin);
		}

		public Transform3D Orthonormalized() => .(Basis.Orthonormalized(), Origin);
		public Transform3D Rotated(Vector3 axis, float angle) => Transform3D(Godot.Basis(axis, angle), Vector3.Zero) * this;
		public Transform3D Scaled(Vector3 scale) => .(Basis.Scaled(scale), Origin * scale);
		public Transform3D Translated(Vector3 offset) => .(Basis, Origin + offset);

		public Transform3D TranslatedLocal(Vector3 offset) => .(Basis, .(
			Origin.X + Basis.Row0.Dot(offset),
			Origin.Y + Basis.Row1.Dot(offset),
			Origin.Z + Basis.Row2.Dot(offset)));

		public Transform3D RotatedLocal(Vector3 axis, float angle) => .(Basis * Godot.Basis(axis, angle), Origin);
		public Transform3D ScaledLocal(Vector3 scale) => .(Basis * Godot.Basis.FromScale(scale), Origin);

		public Transform3D LookingAt(Vector3 target, Vector3 up, bool useModelFront = false) => .(Godot.Basis.LookingAt(target - Origin, up, useModelFront), Origin);
		public Transform3D LookingAt(Vector3 target) => LookingAt(target, Vector3.Up, false);

		public Transform3D InterpolateWith(Transform3D transform, float weight)
		{
			Quaternion sourceRotation = Basis.GetRotationQuaternion();
			Vector3 sourceScale = Basis.Scale;
			Quaternion destRotation = transform.Basis.GetRotationQuaternion();
			Vector3 destScale = transform.Basis.Scale;
			Quaternion q = sourceRotation.Slerp(destRotation, weight).Normalized();
			Vector3 scale = sourceScale.Lerp(destScale, weight);
			Godot.Basis ib = Godot.Basis(q) * Godot.Basis.FromScale(scale);
			return .(ib, Origin.Lerp(transform.Origin, weight));
		}

		public bool IsFinite() => Basis.IsFinite() && Origin.IsFinite();
		public bool IsEqualApprox(Transform3D other) => Basis.IsEqualApprox(other.Basis) && Origin.IsEqualApprox(other.Origin);
		public bool Equals(Transform3D other) => Basis == other.Basis && Origin == other.Origin;

		public static Transform3D operator *(Transform3D t, Transform3D other) => .(t.Basis * other.Basis, t * other.Origin);

		public static Vector3 operator *(Transform3D t, Vector3 vector) => .(
			t.Basis.Row0.Dot(vector) + t.Origin.X,
			t.Basis.Row1.Dot(vector) + t.Origin.Y,
			t.Basis.Row2.Dot(vector) + t.Origin.Z);

		public static bool operator ==(Transform3D a, Transform3D b) => a.Basis == b.Basis && a.Origin == b.Origin;
	}
}
