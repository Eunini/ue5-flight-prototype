// Standalone checks for the engine-agnostic flight core.
// Build: cmake -S Checks -B build && cmake --build build && ./build/flight_checks
#include "../Source/FlightProto/Core/FlightModel.h"

#include <cstdio>
#include <functional>
#include <vector>

using namespace FlightCore;

namespace
{
	int Failures = 0;

	void Check(bool bCondition, const char* What)
	{
		if (!bCondition)
		{
			std::printf("    FAIL: %s\n", What);
			++Failures;
		}
	}

	bool SameVec(const Vec3& A, const Vec3& B) { return A.X == B.X && A.Y == B.Y && A.Z == B.Z; }

	bool SameState(const FlightState& A, const FlightState& B)
	{
		const Quat& P = A.Orientation;
		const Quat& Q = B.Orientation;
		return SameVec(A.Position, B.Position) && SameVec(A.Velocity, B.Velocity) && SameVec(A.AngularVelocity, B.AngularVelocity)
			&& P.W == Q.W && P.X == Q.X && P.Y == Q.Y && P.Z == Q.Z && A.bOnGround == B.bOnGround;
	}

	FlightState Airborne(double Speed, double Altitude)
	{
		FlightState S;
		S.Position = {0, 0, Altitude};
		S.Velocity = {Speed, 0, 0};
		S.bOnGround = false;
		return S;
	}

	void Run(const FlightModel& Model, FlightState& S, const ControlInput& In, double Seconds, Telemetry* Last = nullptr)
	{
		StepContext Ctx;
		const int Steps = static_cast<int>(Seconds / Ctx.Dt);
		for (int i = 0; i < Steps; ++i)
		{
			const Telemetry T = Model.Step(S, In, Ctx);
			if (Last) *Last = T;
		}
	}

	void ParkedAircraftStaysParked()
	{
		FlightModel Model;
		FlightState S;
		S.Position = {0, 0, Model.GetParams().GearHeight};
		Run(Model, S, ControlInput{}, 10.0);
		Check(S.bOnGround, "aircraft remains on the ground");
		Check(S.Position.Length() < Model.GetParams().GearHeight + 0.01, "aircraft does not drift");
	}

	void TakeoffAndClimb()
	{
		FlightModel Model;
		FlightState S;
		S.Position = {0, 0, Model.GetParams().GearHeight};
		StepContext Ctx;
		double LiftoffSpeed = 0.0;
		for (double t = 0; t < 60.0; t += Ctx.Dt)
		{
			const double Speed = S.Velocity.Length();
			ControlInput In;
			In.Throttle = 1.0;
			In.Pitch = Speed > 25.0 ? 0.6 : 0.0; // rotate at Vr
			const bool bWasOnGround = S.bOnGround;
			Model.Step(S, In, Ctx);
			if (bWasOnGround && !S.bOnGround && LiftoffSpeed == 0.0) LiftoffSpeed = Speed;
		}
		std::printf("    liftoff %.1f m/s, altitude after 60 s: %.0f m, speed %.1f m/s\n", LiftoffSpeed, S.Position.Z, S.Velocity.Length());
		Check(LiftoffSpeed > 22.0 && LiftoffSpeed < 40.0, "lifts off at a plausible speed");
		Check(S.Position.Z > 100.0, "climbs above 100 m within 60 s");
	}

	void CruiseSpeedIsBounded()
	{
		FlightModel Model;
		FlightState S = Airborne(50.0, 1000.0);
		ControlInput In;
		In.Throttle = 1.0;
		In.Pitch = 0.0;
		Run(Model, S, In, 120.0);
		const double Speed = S.Velocity.Length();
		std::printf("    full throttle after 120 s: %.1f m/s, altitude %.0f m\n", Speed, S.Position.Z);
		Check(Speed > 35.0 && Speed < 80.0, "speed settles in a light-aircraft envelope");
	}

	void GlideLosesEnergy()
	{
		FlightModel Model;
		FlightState S = Airborne(50.0, 1000.0);
		const double G = Model.GetParams().Gravity;
		const double E0 = G * S.Position.Z + 0.5 * S.Velocity.Dot(S.Velocity);
		Run(Model, S, ControlInput{}, 60.0);
		const double E1 = G * S.Position.Z + 0.5 * S.Velocity.Dot(S.Velocity);
		Check(E1 < E0, "engine-off flight never gains total energy");
		Check(S.Position.Z < 1000.0, "engine-off flight descends");
	}

	void StallReducesLift()
	{
		FlightModel Model;
		bool bStalledLow = false, bStalledHigh = false;
		const double ClPeak = Model.LiftCoefficient(15.9 * DegToRad, bStalledLow);
		const double ClPast = Model.LiftCoefficient(25.0 * DegToRad, bStalledHigh);
		Check(!bStalledLow && bStalledHigh, "stall flag switches at the critical angle");
		Check(ClPast < ClPeak, "lift coefficient drops after the stall");
	}

	void ControlsRespondInTheRightSense()
	{
		FlightModel Model;
		ControlInput RollRight;
		RollRight.Throttle = 0.7;
		RollRight.Roll = 1.0;
		FlightState A = Airborne(50.0, 1000.0);
		Run(Model, A, RollRight, 1.0);
		Check(A.Orientation.RollRad() > 5.0 * DegToRad, "right aileron lowers the right wing");

		ControlInput PitchUp;
		PitchUp.Throttle = 0.7;
		PitchUp.Pitch = 1.0;
		FlightState B = Airborne(50.0, 1000.0);
		Run(Model, B, PitchUp, 1.0);
		Check(B.Orientation.PitchRad() > 5.0 * DegToRad, "back stick raises the nose");

		ControlInput YawRight;
		YawRight.Throttle = 0.7;
		YawRight.Yaw = 1.0;
		FlightState C = Airborne(50.0, 1000.0);
		Run(Model, C, YawRight, 1.0);
		Check(C.Orientation.YawRad() > 1.0 * DegToRad, "right rudder yaws the nose right");
	}

	void HardLandingIsDetected()
	{
		FlightModel Model;
		FlightState S = Airborne(30.0, Model.GetParams().GearHeight + 0.02);
		S.Velocity.Z = -6.0;
		Telemetry T;
		StepContext Ctx;
		T = Model.Step(S, ControlInput{}, Ctx);
		Check(T.bHardLanding, "sink rate above the gear limit is reported");
		Check(S.bOnGround, "aircraft is on the ground after touchdown");
	}

	// Client prediction replays inputs after a server correction, so identical
	// inputs must produce bit-identical states.
	void StepIsDeterministic()
	{
		FlightModel Model;
		std::vector<ControlInput> Inputs;
		for (int i = 0; i < 2400; ++i)
		{
			ControlInput In;
			In.Throttle = 1.0;
			In.Pitch = (i % 300) < 150 ? 0.3 : -0.1;
			In.Roll = (i % 500) < 100 ? 0.5 : 0.0;
			Inputs.push_back(In);
		}
		FlightState A, B;
		A.Position = B.Position = {0, 0, Model.GetParams().GearHeight};
		StepContext Ctx;
		for (const ControlInput& In : Inputs) Model.Step(A, In, Ctx);
		for (const ControlInput& In : Inputs) Model.Step(B, In, Ctx);
		Check(SameState(A, B), "replaying the same inputs reproduces the same state");
	}

	void InputsAreSanitized()
	{
		FlightModel Model;
		FlightState A = Airborne(50.0, 1000.0), B = Airborne(50.0, 1000.0);
		ControlInput Cheat;
		Cheat.Throttle = 50.0;
		ControlInput Max;
		Max.Throttle = 1.0;
		Run(Model, A, Cheat, 2.0);
		Run(Model, B, Max, 2.0);
		Check(SameState(A, B), "out-of-range inputs are clamped");
	}
}

int main()
{
	const std::vector<std::pair<const char*, std::function<void()>>> Checks = {
		{"parked aircraft stays parked", ParkedAircraftStaysParked},
		{"takeoff and climb", TakeoffAndClimb},
		{"cruise speed is bounded", CruiseSpeedIsBounded},
		{"glide loses energy", GlideLosesEnergy},
		{"stall reduces lift", StallReducesLift},
		{"controls respond in the right sense", ControlsRespondInTheRightSense},
		{"hard landing is detected", HardLandingIsDetected},
		{"step is deterministic", StepIsDeterministic},
		{"inputs are sanitized", InputsAreSanitized},
	};
	for (const auto& [Name, Fn] : Checks)
	{
		const int Before = Failures;
		std::printf("%s\n", Name);
		Fn();
		std::printf("  %s\n", Failures == Before ? "ok" : "FAILED");
	}
	std::printf("\n%s (%d failure%s)\n", Failures == 0 ? "ALL PASSED" : "FAILURES", Failures, Failures == 1 ? "" : "s");
	return Failures == 0 ? 0 : 1;
}
