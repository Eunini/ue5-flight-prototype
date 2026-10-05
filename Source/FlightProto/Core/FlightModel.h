// Deterministic fixed-step flight model. No engine types, so it can be verified
// outside Unreal and replayed exactly during client-side prediction.
#pragma once

#include "FlightMath.h"

namespace FlightCore
{
	// Defaults approximate a light single-engine trainer (C172 class).
	struct AircraftParams
	{
		double Mass = 1100.0;            // kg
		double WingArea = 16.2;          // m^2
		double WingSpan = 11.0;          // m
		double MeanChord = 1.5;          // m
		double MaxStaticThrust = 2800.0; // N at full throttle, zero airspeed
		double PropZeroThrustSpeed = 90.0; // m/s where prop thrust falls to zero

		double CL0 = 0.25;
		double CLAlpha = 5.0;            // per rad
		double StallAlpha = 16.0 * DegToRad;
		double CD0 = 0.027;
		double OswaldEfficiency = 0.8;
		double CYBeta = 0.6;

		// Pitch (positive = nose up), roll (positive = right wing down), yaw (positive = nose right).
		double CmAlpha = -1.2, CmElevator = 1.1, CmPitchDamping = 12.0, Cm0 = 0.04;
		double ClAileron = 0.18, ClBeta = -0.08, ClRollDamping = 0.5;
		double CnRudder = 0.07, CnBeta = 0.07, CnYawDamping = 0.1;

		Vec3 Inertia{1285.0, 1825.0, 2667.0}; // kg m^2 about body X, Y, Z

		double GearHeight = 1.2;          // m, CG above ground when resting on gear
		double RollingFriction = 0.03;
		double BrakeFriction = 0.45;
		double MaxSafeSinkRate = 3.0;     // m/s
		double AirDensity = 1.225;        // kg/m^3
		double Gravity = 9.81;            // m/s^2
	};

	struct ControlInput
	{
		double Throttle = 0.0; // 0..1
		double Pitch = 0.0;    // -1..1, positive = nose up
		double Roll = 0.0;     // -1..1, positive = roll right
		double Yaw = 0.0;      // -1..1, positive = yaw right
		bool bBrake = false;

		ControlInput Sanitized() const
		{
			return {Clamp(Throttle, 0, 1), Clamp(Pitch, -1, 1), Clamp(Roll, -1, 1), Clamp(Yaw, -1, 1), bBrake};
		}
	};

	struct FlightState
	{
		Vec3 Position;         // world, m
		Vec3 Velocity;         // world, m/s
		Quat Orientation;      // body -> world
		Vec3 AngularVelocity;  // body frame, rad/s
		bool bOnGround = true;
	};

	struct Telemetry
	{
		double Airspeed = 0.0;
		double AlphaDeg = 0.0;
		double BetaDeg = 0.0;
		double LiftCoefficient = 0.0;
		double LiftN = 0.0;
		double DragN = 0.0;
		double ThrustN = 0.0;
		double VerticalSpeed = 0.0;
		bool bStalled = false;
		bool bHardLanding = false;
	};

	struct StepContext
	{
		double Dt = 1.0 / 120.0;
		double GroundHeight = 0.0;  // terrain height under the aircraft (world Z, m)
		double EngineThrustScale = 1.0; // 0 when the engine is not running
	};

	class FlightModel
	{
	public:
		explicit FlightModel(const AircraftParams& InParams = AircraftParams()) : Params(InParams) {}

		Telemetry Step(FlightState& State, const ControlInput& RawInput, const StepContext& Ctx) const;
		double LiftCoefficient(double AlphaRad, bool& bOutStalled) const;

		const AircraftParams& GetParams() const { return Params; }

	private:
		void ResolveGroundContact(FlightState& State, const ControlInput& Input, const StepContext& Ctx, Telemetry& Out) const;

		AircraftParams Params;
	};
}
