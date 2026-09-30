# FlightProto: networked light-aircraft prototype (UE5, C++)

A small Unreal Engine 5 C++ prototype covering three areas:

1. **Core flight mechanics:** a deterministic, fixed-step rigid-body flight model with lift, induced drag, stall, propeller thrust, stability derivatives and ground handling.
2. **Player interaction:** cabin switches you look at and use (battery, fuel pump, magnetos, starter, nav lights). The switches drive a server-side engine start sequence with fault diagnostics.
3. **Multiplayer replication:** server-authoritative flight with client-side prediction, input replay/reconciliation, correction smoothing, and interpolation for other players.

No content assets are needed. Aircraft are blocked out from engine cubes and the HUD is drawn with Canvas, so the project runs from source on an empty level.

## Layout

```
Source/FlightProto/
  Core/            Engine-agnostic flight model (plain C++17, unit tested outside Unreal)
    FlightMath.h     Vec3 / Quat in Unreal's axis convention (X fwd, Y right, Z up)
    FlightModel.*    Aerodynamics, rigid-body integration, ground contact
  Aircraft/
    FlightPawn.*     Pawn: fixed-step sim, prediction, reconciliation, proxy interpolation
  Interaction/
    Interactable.h             Interface implemented by any usable component
    CabinSwitchComponent.*     Panel switch mirroring replicated systems state
    CabinInteractionComponent.*  Client look-trace + server-validated use RPC
  Systems/
    AircraftSystemsComponent.*  Electrical/fuel/engine state machine, diagnostic rows
  UI/
    FlightHUD.*      Telemetry, interaction prompt, diagnostic menu, net stats
Tests/               CMake target running the flight-model tests without the engine
```

## Flight model

`FlightCore::FlightModel::Step` advances one fixed sub-step (1/120 s):

- Airflow is transformed into the body frame to get airspeed, angle of attack and sideslip.
- Lift uses a linear lift curve up to the critical angle (16°), then falls towards a flat-plate value. Drag is `CD0 + k·CL²` with `k = 1 / (π·e·AR)`, plus a separate drag penalty once stalled.
- Propeller thrust decays with forward speed.
- Pitch, roll and yaw moments include static stability (`Cmα`, `Cnβ`, dihedral `Clβ`), control power and rate damping. At low airspeed the tail keeps some authority from prop wash.
- Angular motion uses Euler's rigid-body equations with a diagonal inertia tensor. The integrator is semi-implicit Euler, and the orientation quaternion is integrated from body rates.
- Ground contact keeps the wings level, limits nose-up rotation, removes tyre side-slip and applies rolling or brake friction and nosewheel steering. Touchdowns faster than the gear's sink-rate limit are flagged.

The model uses no engine types, so it is **deterministic for identical inputs**. That property is what makes client-side replay work, and it is covered by a test.

## Networking

| Role | Behaviour |
| --- | --- |
| Owning client | Samples input each 1/60 s frame and quantises it (8-bit axes). It simulates immediately and sends the newest 6 frames per unreliable RPC, so a lost packet costs nothing. |
| Server | Simulates only frames it receives, in sequence order, then replicates the resulting state and the last processed sequence number. A per-client time budget rejects clients asking to simulate faster than real time (speed-hack guard). Out-of-range inputs are clamped in the model. |
| Owning client on update | Rewinds to the server state, drops acknowledged inputs, replays the rest, then blends the visual error away. Corrections over 10 m snap instead. |
| Other clients | Buffer server snapshots and render 100 ms in the past, lerping position and slerping rotation. |
| Listen-server host | Simulates locally with no latency. |

Replicated state holds the model's own double-precision values, so a replay starts from exactly what the server had. Aircraft systems (switches, engine state, fuel, battery) are replicated properties and are changed only on the server. Clients predict thrust from the replicated engine state.

Interaction follows the same trust model. The client traces for a focused switch and requests use through an RPC. The server re-checks reach from the pawn's eye point and ownership before it acts.

## Running

1. Right-click `FlightProto.uproject` → *Generate Visual Studio project files* (or use Rider), then build `FlightProtoEditor` (Development Editor).
2. Open the project, create a level with a floor or landscape, and add a Player Start. `FlightProtoGameMode` is the global default.
3. Multiplayer: *Play → Number of Players 2–3, Net Mode: Play As Client*. To see prediction and reconciliation under latency and loss, enable network emulation in *Editor Preferences → Level Editor → Play → Multiplayer Options*. The HUD shows unacknowledged inputs and the last correction size.

Controls: `W/S` pitch, `A/D` roll, `Q/E` rudder, `Shift/Ctrl` throttle, `B` brake, `V` cabin view, `F` use switch, `Tab` diagnostics.
Engine start: in cabin view, turn on the battery, fuel pump and magnetos, then press the starter. Skipping a step produces a named fault on the diagnostic panel.

## Tests

```
cmake -S Tests -B build && cmake --build build && ./build/flight_tests
```

Coverage: parked stability, takeoff and climb, a bounded cruise speed, energy loss in a glide, stall lift loss, the correct control sense on all three axes, hard-landing detection, bit-identical replay determinism, and input sanitisation.

## Network simulation harness

`Tools/NetSim` runs the same replication scheme as `AFlightPawn` outside the engine: two scripted pilots, one server, and simulated latency, jitter, packet loss and reordering. It drives the real `FlightCore` model and reports prediction corrections.

```
g++ -std=c++17 -O2 Tools/NetSim/NetSim.cpp Source/FlightProto/Core/FlightModel.cpp -o netsim && ./netsim trace.json
```

Results for the included 78 s scenario (120 ms round trip with 2% loss, plus a 16 s window at 360 ms, ±40 ms jitter and 15% loss):

- On the steady network, corrections are 0 cm apart from under 1 cm at engine start (the client learns the engine is running one round trip late). The model is deterministic, so client and server compute identical states.
- In the degraded window, late and reordered inputs cause corrections of up to 3.7 m. The client blends them out after replaying its unacknowledged inputs.
- The largest correction (7.4 m) comes at the instant the network recovers. Packets on the now-fast link overtake packets still in flight on the slow one by about 7 frames, which is more than the 6-frame redundancy, so the server skips a few inputs. A short server-side jitter buffer for sequence gaps would remove this; it is the next change I would make.

`viewer.html` replays a trace as two players' screens side by side (three.js). `record.mjs` (Playwright) renders it frame by frame for video.

## Next steps for a production pipeline

- Enhanced Input actions and mapping contexts in place of the legacy axis mappings.
- UMG diagnostic menu fed by `BuildDiagnostics()`, which already returns the rows. FMOD/MetaSounds engine audio driven by RPM and throttle.
- Modular aircraft meshes on sockets in place of the blockout cubes, and collision between aircraft.
- Net-quantised state and a lower send rate for bandwidth. Moving the sim onto Unreal's Network Prediction plugin or Mover once it stabilises.
