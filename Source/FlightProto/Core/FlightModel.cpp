#include "FlightModel.h"

namespace FlightCore
{
	double FlightModel::LiftCoefficient(double AlphaRad, bool& bOutStalled) const
	{
		const double AbsAlpha = std::fabs(AlphaRad);
		bOutStalled = AbsAlpha > Params.StallAlpha;
		if (!bOutStalled)
		{
			return Params.CL0 + Params.CLAlpha * AlphaRad;
		}
		// Past the critical angle lift collapses towards a flat-plate value.
		const double Sign = AlphaRad >= 0.0 ? 1.0 : -1.0;
		const double CLMax = Params.CL0 * Sign + Params.CLAlpha * Params.StallAlpha * Sign;
		const double Excess = AbsAlpha - Params.StallAlpha;
		const double Falloff = std::fmax(0.45, 1.0 - Excess * 3.0);
		return CLMax * Falloff;
	}

	Telemetry FlightModel::Step(FlightState& State, const ControlInput& RawInput, const StepContext& Ctx) const
	{
		const ControlInput Input = RawInput.Sanitized();
		const AircraftParams& P = Params;
		Telemetry Out;

		// Relative airflow in the body frame (still air).
		const Vec3 VBody = State.Orientation.Unrotate(State.Velocity);
		const double V = VBody.Length();
		const double DynamicPressure = 0.5 * P.AirDensity * V * V;
		const double QS = DynamicPressure * P.WingArea;

		double Alpha = 0.0, Beta = 0.0;
		if (V > 1.0)
		{
			Alpha = std::atan2(-VBody.Z, VBody.X);
			Beta = std::asin(Clamp(VBody.Y / V, -1.0, 1.0));
		}

		bool bStalled = false;
		const double CL = V > 1.0 ? LiftCoefficient(Alpha, bStalled) : 0.0;
		const double AspectRatio = P.WingSpan * P.WingSpan / P.WingArea;
		const double InducedK = 1.0 / (Pi * P.OswaldEfficiency * AspectRatio);
		const double CD = P.CD0 + InducedK * CL * CL + (bStalled ? 0.35 : 0.0);

		// Forces in the body frame.
		Vec3 ForceBody;
		if (V > 1.0)
		{
			const Vec3 Flow = VBody / V;
			const Vec3 LiftDir = Flow.Cross({0, 1, 0}).Normalized();
			ForceBody += LiftDir * (QS * CL);
			ForceBody += -Flow * (QS * CD);
			ForceBody += Vec3{0, -QS * P.CYBeta * Beta, 0};
		}
		const double PropFactor = Clamp(1.0 - std::fmax(VBody.X, 0.0) / P.PropZeroThrustSpeed, 0.0, 1.0);
		const double Thrust = Input.Throttle * P.MaxStaticThrust * PropFactor * Clamp(Ctx.EngineThrustScale, 0.0, 1.0);
		ForceBody += Vec3{Thrust, 0, 0};

		// Moments, computed in aero sign convention then mapped onto body axes.
		// Positive rotation about +Y pitches the nose down and about +X lifts the right wing.
		const double PitchRateUp = -State.AngularVelocity.Y;
		const double RollRateRight = -State.AngularVelocity.X;
		const double YawRateRight = State.AngularVelocity.Z;
		const double SafeV = std::fmax(V, 5.0);
		// Keep a little control authority at low speed (prop wash over the tail).
		const double ControlQS = std::fmax(QS, 0.5 * P.AirDensity * 12.0 * 12.0 * P.WingArea * Input.Throttle);

		const double PitchUpMoment = QS * P.MeanChord * (P.Cm0 + P.CmAlpha * Alpha - P.CmPitchDamping * PitchRateUp * P.MeanChord / (2.0 * SafeV))
			+ ControlQS * P.MeanChord * P.CmElevator * Input.Pitch * 0.25;
		const double RollRightMoment = QS * P.WingSpan * (P.ClBeta * Beta - P.ClRollDamping * RollRateRight * P.WingSpan / (2.0 * SafeV))
			+ ControlQS * P.WingSpan * P.ClAileron * Input.Roll;
		const double YawRightMoment = QS * P.WingSpan * (P.CnBeta * Beta - P.CnYawDamping * YawRateRight * P.WingSpan / (2.0 * SafeV))
			+ ControlQS * P.WingSpan * P.CnRudder * Input.Yaw;

		const Vec3 MomentBody{-RollRightMoment, -PitchUpMoment, YawRightMoment};

		// Rigid body integration (semi-implicit Euler, fixed step).
		const Vec3& I = P.Inertia;
		const Vec3& W = State.AngularVelocity;
		const Vec3 IW{I.X * W.X, I.Y * W.Y, I.Z * W.Z};
		const Vec3 Gyro = W.Cross(IW);
		const Vec3 AngAccel{(MomentBody.X - Gyro.X) / I.X, (MomentBody.Y - Gyro.Y) / I.Y, (MomentBody.Z - Gyro.Z) / I.Z};
		State.AngularVelocity += AngAccel * Ctx.Dt;

		const Vec3 ForceWorld = State.Orientation.Rotate(ForceBody) + Vec3{0, 0, -P.Gravity * P.Mass};
		State.Velocity += ForceWorld * (Ctx.Dt / P.Mass);
		State.Position += State.Velocity * Ctx.Dt;
		State.Orientation = State.Orientation.Integrated(State.AngularVelocity, Ctx.Dt);

		ResolveGroundContact(State, Input, Ctx, Out);

		Out.Airspeed = V;
		Out.AlphaDeg = Alpha / DegToRad;
		Out.BetaDeg = Beta / DegToRad;
		Out.LiftCoefficient = CL;
		Out.LiftN = QS * CL;
		Out.DragN = QS * CD;
		Out.ThrustN = Thrust;
		Out.VerticalSpeed = State.Velocity.Z;
		Out.bStalled = bStalled;
		return Out;
	}

	void FlightModel::ResolveGroundContact(FlightState& State, const ControlInput& Input, const StepContext& Ctx, Telemetry& Out) const
	{
		const AircraftParams& P = Params;
		const double RestZ = Ctx.GroundHeight + P.GearHeight;
		if (State.Position.Z > RestZ)
		{
			State.bOnGround = false;
			return;
		}

		if (!State.bOnGround && State.Velocity.Z < -P.MaxSafeSinkRate)
		{
			Out.bHardLanding = true;
		}
		State.bOnGround = true;
		State.Position.Z = RestZ;
		if (State.Velocity.Z < 0.0)
		{
			State.Velocity.Z = 0.0;
		}

		// Gear keeps the wings level and the nose from digging in; heading is steered by rudder/nosewheel.
		const double Yaw = State.Orientation.YawRad();
		const double Pitch = Clamp(State.Orientation.PitchRad(), 0.0, 15.0 * DegToRad);
		State.Orientation = Quat::FromYawPitchRoll(Yaw, Pitch, 0.0);
		State.AngularVelocity.X = 0.0;
		if (State.Orientation.PitchRad() <= 1e-6 && State.AngularVelocity.Y > 0.0)
		{
			State.AngularVelocity.Y = 0.0;
		}

		// Tyres: remove sideways slip, apply rolling/brake friction along the heading.
		const Vec3 Forward = State.Orientation.Forward();
		const Vec3 FlatForward = Vec3{Forward.X, Forward.Y, 0.0}.Normalized();
		const double GroundSpeed = State.Velocity.Dot(FlatForward);
		const double Mu = Input.bBrake ? P.BrakeFriction : P.RollingFriction;
		const double Decel = Mu * P.Gravity * Ctx.Dt;
		const double NewSpeed = std::fabs(GroundSpeed) <= Decel ? 0.0 : GroundSpeed - Decel * (GroundSpeed > 0 ? 1.0 : -1.0);
		State.Velocity = FlatForward * NewSpeed + Vec3{0, 0, State.Velocity.Z};

		// Nosewheel steering at taxi speeds.
		const double SteerRate = Input.Yaw * Clamp(std::fabs(NewSpeed) / 10.0, 0.0, 1.0) * 0.5;
		State.AngularVelocity.Z = SteerRate;
	}
}
