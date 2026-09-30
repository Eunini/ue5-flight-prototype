// Offline network simulation of the FlightPawn replication scheme, driving the real
// FlightCore model. Two scripted pilots, one server, simulated latency/jitter/loss.
// Writes a JSON trace used by Tools/NetSim/viewer.html to render the demo video.
//
// The prediction/reconciliation/interpolation logic mirrors AFlightPawn so it can be
// exercised and measured without the engine.
#include "../../Source/FlightProto/Core/FlightModel.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <random>
#include <string>
#include <vector>

using namespace FlightCore;

namespace
{
	constexpr double FrameSeconds = 1.0 / 60.0;
	constexpr int SubSteps = 2;
	constexpr int RedundantInputs = 6;
	constexpr double ServerSendInterval = 1.0 / 30.0;
	constexpr double ProxyDelay = 0.1;
	constexpr double MaxCatchUp = 0.25;
	constexpr double SnapThresholdM = 10.0;
	constexpr double TraceInterval = 1.0 / 30.0;
	constexpr double Duration = 78.0;

	struct InputFrame
	{
		uint32_t Seq = 0;
		uint8_t Throttle = 0;
		int8_t Pitch = 0, Roll = 0, Yaw = 0;
		bool bBrake = false;

		static int8_t Q(double V) { return static_cast<int8_t>(std::lround(Clamp(V, -1, 1) * 127.0)); }
		static InputFrame Make(uint32_t S, const ControlInput& In)
		{
			InputFrame F;
			F.Seq = S;
			F.Throttle = static_cast<uint8_t>(std::lround(Clamp(In.Throttle, 0, 1) * 255.0));
			F.Pitch = Q(In.Pitch);
			F.Roll = Q(In.Roll);
			F.Yaw = Q(In.Yaw);
			F.bBrake = In.bBrake;
			return F;
		}
		ControlInput ToControl() const { return {Throttle / 255.0, Pitch / 127.0, Roll / 127.0, Yaw / 127.0, bBrake}; }
	};

	struct NetConditions
	{
		double OneWayLatency;
		double Jitter;
		double Loss;
	};

	// Conditions change during the run so the video shows good and bad networks.
	NetConditions ConditionsAt(double T)
	{
		if (T >= 50.0 && T < 66.0) return {0.18, 0.04, 0.15};
		return {0.06, 0.01, 0.02};
	}

	template <typename Payload>
	struct Link
	{
		struct Packet { double DeliverAt; Payload Data; };
		std::vector<Packet> InFlight;
		int Sent = 0, Lost = 0;

		void Send(double Now, const Payload& P, std::mt19937& Rng)
		{
			const NetConditions C = ConditionsAt(Now);
			std::uniform_real_distribution<double> U(0.0, 1.0);
			++Sent;
			if (U(Rng) < C.Loss) { ++Lost; return; }
			InFlight.push_back({Now + C.OneWayLatency + (U(Rng) * 2 - 1) * C.Jitter, P});
		}

		template <typename Fn>
		void Deliver(double Now, Fn&& Receive)
		{
			// Unreliable delivery can reorder: deliver in arrival-time order.
			std::sort(InFlight.begin(), InFlight.end(), [](const Packet& A, const Packet& B) { return A.DeliverAt < B.DeliverAt; });
			size_t N = 0;
			while (N < InFlight.size() && InFlight[N].DeliverAt <= Now) Receive(InFlight[N++].Data);
			InFlight.erase(InFlight.begin(), InFlight.begin() + static_cast<long>(N));
		}
	};

	struct StateMsg
	{
		int Pilot;
		uint32_t LastProcessed;
		FlightState State;
		double ServerTime;
		bool bEngineRunning;
	};

	struct Snapshot { double ReceivedAt; Vec3 Pos; Quat Rot; };

	Quat Slerp(Quat A, Quat B, double T)
	{
		double D = A.W * B.W + A.X * B.X + A.Y * B.Y + A.Z * B.Z;
		if (D < 0) { B = {-B.W, -B.X, -B.Y, -B.Z}; D = -D; }
		if (D > 0.9995)
		{
			return Quat{A.W + (B.W - A.W) * T, A.X + (B.X - A.X) * T, A.Y + (B.Y - A.Y) * T, A.Z + (B.Z - A.Z) * T}.Normalized();
		}
		const double Th = std::acos(D), S = std::sin(Th);
		const double Wa = std::sin((1 - T) * Th) / S, Wb = std::sin(T * Th) / S;
		return Quat{A.W * Wa + B.W * Wb, A.X * Wa + B.X * Wb, A.Y * Wa + B.Y * Wb, A.Z * Wa + B.Z * Wb}.Normalized();
	}

	double Distance(const Vec3& A, const Vec3& B) { return (A - B).Length(); }

	// Scripted pilot: engine start checklist, takeoff roll, rotate, climb, turns.
	struct Pilot
	{
		double StartDelay;
		double TurnDir;

		bool EngineRunning(double T) const { return T >= StartDelay + 5.0; }

		ControlInput Control(double T, const FlightState& S) const
		{
			ControlInput In;
			const double L = T - StartDelay;
			if (L < 5.0) { In.bBrake = true; return In; }
			In.Throttle = 1.0;
			const Vec3 VBody = S.Orientation.Unrotate(S.Velocity);
			const double Airspeed = VBody.Length();
			const double Pitch = S.Orientation.PitchRad() / DegToRad;
			const double Roll = S.Orientation.RollRad() / DegToRad;
			if (S.bOnGround)
			{
				In.Pitch = Airspeed > 24.0 ? 0.6 : 0.0;
				return In;
			}
			// Climb at a fixed airspeed (Vy): pitch attitude follows the speed error.
			const double Alt = S.Position.Z;
			const double TargetSpeed = Alt < 250.0 ? 36.0 : 44.0;
			const double TargetPitch = Clamp(8.0 + (Airspeed - TargetSpeed) * 1.5, 2.0, 12.0);
			double TargetRoll = 0.0;
			if (Alt > 25.0 && L > 30.0) TargetRoll = 25.0 * TurnDir;
			if (L > 50.0) TargetRoll = -25.0 * TurnDir;
			In.Pitch = Clamp((TargetPitch - Pitch) * 0.1 + std::fabs(Roll) * 0.01, -1, 1);
			In.Roll = Clamp((TargetRoll - Roll) * 0.03, -1, 1);
			In.Yaw = Clamp(VBody.Y * 0.03, -0.5, 0.5); // rudder into the sideslip to coordinate the turn
			return In;
		}
	};

	struct Client
	{
		int Id;
		FlightState Predicted;
		std::deque<InputFrame> Pending;
		uint32_t NextSeq = 1;
		Vec3 VisualOffset;
		double LastCorrectionCm = 0;
		double MaxCorrectionCm = 0;
		bool bEngineRunningReplicated = false;
		std::vector<Snapshot> Remote; // snapshots of the other pilot
		Link<std::vector<InputFrame>> Up;
		Link<StateMsg> Down;
	};

	struct ServerPilot
	{
		FlightState State;
		uint32_t LastProcessed = 0;
		double Budget = 0;
		bool bEngineRunning = false;
	};

	void Simulate(const FlightModel& M, FlightState& S, const InputFrame& F, bool bEngine)
	{
		StepContext Ctx;
		Ctx.Dt = FrameSeconds / SubSteps;
		Ctx.GroundHeight = 0.0;
		Ctx.EngineThrustScale = bEngine ? 1.0 : 0.0;
		for (int i = 0; i < SubSteps; ++i) M.Step(S, F.ToControl(), Ctx);
	}

	std::string J(const Vec3& V) { char B[96]; std::snprintf(B, sizeof B, "[%.3f,%.3f,%.3f]", V.X, V.Y, V.Z); return B; }
	std::string J(const Quat& Q) { char B[128]; std::snprintf(B, sizeof B, "[%.5f,%.5f,%.5f,%.5f]", Q.W, Q.X, Q.Y, Q.Z); return B; }

	FlightState Parked(double Y)
	{
		FlightState S;
		S.Position = {0, Y, AircraftParams().GearHeight};
		return S;
	}
}

int main(int Argc, char** Argv)
{
	const char* OutPath = Argc > 1 ? Argv[1] : "trace.json";
	FlightModel Model;
	std::mt19937 Rng(1554);
	const Pilot Pilots[2] = {{0.0, 1.0}, {6.0, -1.0}};

	ServerPilot Server[2];
	Client Clients[2];
	for (int i = 0; i < 2; ++i)
	{
		Server[i].State = Parked(i == 0 ? 0.0 : 40.0);
		Clients[i].Id = i;
		Clients[i].Predicted = Server[i].State;
	}

	FILE* Out = std::fopen(OutPath, "w");
	if (!Out) { std::perror("open"); return 1; }
	std::fprintf(Out, "{\"frames\":[\n");

	double NextSend = 0, NextTrace = 0;
	bool bFirst = true;
	double SumCorrection[2] = {0, 0};
	int Corrections[2] = {0, 0};
	const int Frames = static_cast<int>(Duration / FrameSeconds);

	for (int Frame = 0; Frame < Frames; ++Frame)
	{
		const double T = Frame * FrameSeconds;

		// Server systems state (engine start sequence) is authoritative and replicated.
		for (int p = 0; p < 2; ++p) Server[p].bEngineRunning = Pilots[p].EngineRunning(T);

		// 1. Clients: predict locally and send inputs.
		for (Client& C : Clients)
		{
			const ControlInput In = Pilots[C.Id].Control(T, C.Predicted);
			const InputFrame F = InputFrame::Make(C.NextSeq++, In);
			Simulate(Model, C.Predicted, F, C.bEngineRunningReplicated);
			C.Pending.push_back(F);
			while (C.Pending.size() > 240) C.Pending.pop_front();
			const size_t Count = std::min<size_t>(RedundantInputs, C.Pending.size());
			std::vector<InputFrame> Batch(C.Pending.end() - static_cast<long>(Count), C.Pending.end());
			C.Up.Send(T, Batch, Rng);
		}

		// 2. Server: receive inputs, simulate within the time budget.
		for (int p = 0; p < 2; ++p)
		{
			ServerPilot& SP = Server[p];
			SP.Budget = std::min(SP.Budget + FrameSeconds, MaxCatchUp);
			Clients[p].Up.Deliver(T, [&](const std::vector<InputFrame>& Batch) {
				for (const InputFrame& F : Batch)
				{
					if (F.Seq <= SP.LastProcessed) continue;
					if (SP.Budget < FrameSeconds) break;
					Simulate(Model, SP.State, F, SP.bEngineRunning);
					SP.Budget -= FrameSeconds;
					SP.LastProcessed = F.Seq;
				}
			});
		}

		// 3. Server replicates both aircraft to both clients at 30 Hz.
		if (T >= NextSend)
		{
			NextSend += ServerSendInterval;
			for (Client& C : Clients)
				for (int p = 0; p < 2; ++p)
					C.Down.Send(T, StateMsg{p, Server[p].LastProcessed, Server[p].State, T, Server[p].bEngineRunning}, Rng);
		}

		// 4. Clients: reconcile own aircraft, buffer the other one.
		for (Client& C : Clients)
		{
			C.Down.Deliver(T, [&](const StateMsg& M) {
				if (M.Pilot != C.Id)
				{
					C.Remote.push_back({T, M.State.Position, M.State.Orientation});
					if (C.Remote.size() > 32) C.Remote.erase(C.Remote.begin());
					return;
				}
				C.bEngineRunningReplicated = M.bEngineRunning;
				const Vec3 Rendered = C.Predicted.Position + C.VisualOffset;
				C.Predicted = M.State;
				while (!C.Pending.empty() && C.Pending.front().Seq <= M.LastProcessed) C.Pending.pop_front();
				for (const InputFrame& F : C.Pending) Simulate(Model, C.Predicted, F, C.bEngineRunningReplicated);
				const Vec3 Error = Rendered - C.Predicted.Position;
				C.LastCorrectionCm = Error.Length() * 100.0;
				C.MaxCorrectionCm = std::max(C.MaxCorrectionCm, C.LastCorrectionCm);
				SumCorrection[C.Id] += C.LastCorrectionCm;
				++Corrections[C.Id];
				C.VisualOffset = Error.Length() > SnapThresholdM ? Vec3{} : Error;
			});
			C.VisualOffset = C.VisualOffset * std::exp(-12.0 * FrameSeconds);
		}

		// 5. Trace at 30 Hz.
		if (T >= NextTrace)
		{
			NextTrace += TraceInterval;
			const NetConditions NC = ConditionsAt(T);
			std::fprintf(Out, "%s{\"t\":%.4f,\"net\":{\"ms\":%d,\"jitter\":%d,\"loss\":%d},\"planes\":[", bFirst ? "" : ",\n", T,
				static_cast<int>(NC.OneWayLatency * 2000), static_cast<int>(NC.Jitter * 1000), static_cast<int>(NC.Loss * 100));
			bFirst = false;
			for (int p = 0; p < 2; ++p)
			{
				Client& Own = Clients[p];
				Client& Other = Clients[1 - p];
				// How the other client sees this aircraft: interpolated ProxyDelay in the past.
				Vec3 RemotePos = Server[p].State.Position;
				Quat RemoteRot = Server[p].State.Orientation;
				auto& Snaps = Other.Remote;
				const double RenderTime = T - ProxyDelay;
				while (Snaps.size() > 2 && Snaps[1].ReceivedAt <= RenderTime) Snaps.erase(Snaps.begin());
				if (!Snaps.empty())
				{
					if (Snaps.size() == 1 || RenderTime <= Snaps[0].ReceivedAt) { RemotePos = Snaps[0].Pos; RemoteRot = Snaps[0].Rot; }
					else
					{
						const double A = Clamp((RenderTime - Snaps[0].ReceivedAt) / std::max(Snaps[1].ReceivedAt - Snaps[0].ReceivedAt, 1e-4), 0, 1);
						RemotePos = Snaps[0].Pos + (Snaps[1].Pos - Snaps[0].Pos) * A;
						RemoteRot = Slerp(Snaps[0].Rot, Snaps[1].Rot, A);
					}
				}
				const FlightState& S = Server[p].State;
				const Vec3 VBody = S.Orientation.Unrotate(S.Velocity);
				const double L = T - Pilots[p].StartDelay;
				const char* Phase = L < 0 ? "cold and dark" : L < 1 ? "battery ON" : L < 2 ? "fuel pump ON" : L < 3 ? "magnetos ON"
					: L < 5 ? "starter: cranking" : S.bOnGround ? "engine running, takeoff roll" : "airborne";
				std::fprintf(Out,
					"%s{\"server\":{\"p\":%s,\"q\":%s},\"owner\":{\"p\":%s,\"q\":%s},\"remote\":{\"p\":%s,\"q\":%s},"
					"\"ias\":%.2f,\"alt\":%.2f,\"vs\":%.2f,\"ground\":%d,\"phase\":\"%s\",\"unacked\":%zu,\"corr\":%.2f,"
					"\"predErr\":%.3f,\"remoteLag\":%.3f,\"lossPct\":%.1f}",
					p ? "," : "", J(S.Position).c_str(), J(S.Orientation).c_str(), J(Own.Predicted.Position + Own.VisualOffset).c_str(),
					J(Own.Predicted.Orientation).c_str(), J(RemotePos).c_str(), J(RemoteRot).c_str(), VBody.Length(), S.Position.Z,
					S.Velocity.Z, S.bOnGround ? 1 : 0, Phase, Own.Pending.size(), Own.LastCorrectionCm,
					Distance(Own.Predicted.Position + Own.VisualOffset, S.Position), Distance(RemotePos, S.Position),
					Own.Up.Sent ? 100.0 * Own.Up.Lost / Own.Up.Sent : 0.0);
			}
			std::fprintf(Out, "]}");
		}
	}
	std::fprintf(Out, "\n]}\n");
	std::fclose(Out);

	for (int p = 0; p < 2; ++p)
	{
		std::printf("pilot %d: alt %.0f m, ias %.1f m/s, corrections %d, mean %.2f cm, max %.2f cm, uplink loss %d/%d\n", p,
			Server[p].State.Position.Z, Server[p].State.Velocity.Length(), Corrections[p],
			Corrections[p] ? SumCorrection[p] / Corrections[p] : 0.0, Clients[p].MaxCorrectionCm, Clients[p].Up.Lost, Clients[p].Up.Sent);
	}
	return 0;
}
