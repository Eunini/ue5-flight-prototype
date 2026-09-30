// Engine-agnostic math used by the flight model.
// Axes match Unreal: X forward, Y right, Z up. Units are SI (metres, seconds, kg).
#pragma once

#include <cmath>

namespace FlightCore
{
	struct Vec3
	{
		double X = 0.0, Y = 0.0, Z = 0.0;

		constexpr Vec3() = default;
		constexpr Vec3(double InX, double InY, double InZ) : X(InX), Y(InY), Z(InZ) {}

		Vec3 operator+(const Vec3& O) const { return {X + O.X, Y + O.Y, Z + O.Z}; }
		Vec3 operator-(const Vec3& O) const { return {X - O.X, Y - O.Y, Z - O.Z}; }
		Vec3 operator-() const { return {-X, -Y, -Z}; }
		Vec3 operator*(double S) const { return {X * S, Y * S, Z * S}; }
		Vec3 operator/(double S) const { return {X / S, Y / S, Z / S}; }
		Vec3& operator+=(const Vec3& O) { X += O.X; Y += O.Y; Z += O.Z; return *this; }

		double Dot(const Vec3& O) const { return X * O.X + Y * O.Y + Z * O.Z; }
		Vec3 Cross(const Vec3& O) const { return {Y * O.Z - Z * O.Y, Z * O.X - X * O.Z, X * O.Y - Y * O.X}; }
		double Length() const { return std::sqrt(Dot(*this)); }
		Vec3 Normalized() const
		{
			const double L = Length();
			return L > 1e-9 ? *this / L : Vec3{};
		}
	};

	// Unit quaternion (Hamilton convention, same component layout as FQuat).
	struct Quat
	{
		double W = 1.0, X = 0.0, Y = 0.0, Z = 0.0;

		static Quat FromAxisAngle(const Vec3& Axis, double Radians)
		{
			const Vec3 A = Axis.Normalized();
			const double S = std::sin(Radians * 0.5);
			return {std::cos(Radians * 0.5), A.X * S, A.Y * S, A.Z * S};
		}

		// Yaw: nose right positive. Pitch: nose up positive. Roll: right wing down positive.
		static Quat FromYawPitchRoll(double Yaw, double Pitch, double Roll)
		{
			return FromAxisAngle({0, 0, 1}, Yaw) * FromAxisAngle({0, 1, 0}, -Pitch) * FromAxisAngle({1, 0, 0}, -Roll);
		}

		Quat operator*(const Quat& O) const
		{
			return {
				W * O.W - X * O.X - Y * O.Y - Z * O.Z,
				W * O.X + X * O.W + Y * O.Z - Z * O.Y,
				W * O.Y - X * O.Z + Y * O.W + Z * O.X,
				W * O.Z + X * O.Y - Y * O.X + Z * O.W};
		}

		Quat Conjugate() const { return {W, -X, -Y, -Z}; }

		Quat Normalized() const
		{
			const double L = std::sqrt(W * W + X * X + Y * Y + Z * Z);
			return L > 1e-12 ? Quat{W / L, X / L, Y / L, Z / L} : Quat{};
		}

		// Body -> world.
		Vec3 Rotate(const Vec3& V) const
		{
			const Vec3 U{X, Y, Z};
			const Vec3 T = U.Cross(V) * 2.0;
			return V + T * W + U.Cross(T);
		}

		// World -> body.
		Vec3 Unrotate(const Vec3& V) const { return Conjugate().Rotate(V); }

		Vec3 Forward() const { return Rotate({1, 0, 0}); }
		Vec3 Right() const { return Rotate({0, 1, 0}); }
		Vec3 Up() const { return Rotate({0, 0, 1}); }

		double PitchRad() const { return std::asin(std::fmax(-1.0, std::fmin(1.0, Forward().Z))); }
		double YawRad() const { const Vec3 F = Forward(); return std::atan2(F.Y, F.X); }
		double RollRad() const { return std::atan2(-Right().Z, Up().Z); }

		// Integrate a body-frame angular velocity (rad/s) over Dt.
		Quat Integrated(const Vec3& BodyOmega, double Dt) const
		{
			const Quat Spin{0.0, BodyOmega.X, BodyOmega.Y, BodyOmega.Z};
			const Quat D = (*this) * Spin;
			return Quat{W + 0.5 * D.W * Dt, X + 0.5 * D.X * Dt, Y + 0.5 * D.Y * Dt, Z + 0.5 * D.Z * Dt}.Normalized();
		}
	};

	inline double Clamp(double V, double Lo, double Hi) { return V < Lo ? Lo : (V > Hi ? Hi : V); }
	constexpr double Pi = 3.14159265358979323846;
	constexpr double DegToRad = Pi / 180.0;
}
