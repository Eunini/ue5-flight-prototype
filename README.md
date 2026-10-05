# Alpine Flight / Unreal Engine 5.4

A C++ light-aircraft project with an original alpine airfield, an interactive cabin and a server-authoritative flight model.

The native editor modules compile in Unreal Engine 5.4.4. The repository includes the engine-generated Blueprint aircraft, material and airfield map. Native gameplay recording is underway; the download will be linked here once reviewed.

## Featured systems

- Fixed-step 120 Hz aerodynamics: lift, drag, stall, propeller thrust, angular dynamics and ground handling.
- Battery, fuel pump, magnetos and starter switches with a replicated engine-start sequence and fault diagnostics.
- Quantized pilot inputs, client prediction, replay after authoritative updates and interpolation for remote aircraft.
- A 480 m runway, hangar, tower, runway lights and surrounding mountain silhouettes.
- A Blueprint aircraft child that calls the C++ livery function from its BeginPlay graph.

## Build and launch

Install Unreal Engine 5.4, then run:

```bash
python3 Tools/portfolio.py --engine /path/to/UE5.4 --run
```

The editor module compiles first. PortfolioForge then saves the material, Blueprint graph and FlightField map as native Unreal assets. Add `--package` to produce a Development build or `--capture` to record the viewport as an MP4. The capture path requires FFmpeg and a working graphics renderer.

Controls: W/S pitch, A/D roll, Q/E rudder, Shift/Ctrl throttle, B brake, V cabin view, mouse look, F use switch and Tab diagnostics. Turn on the battery, fuel pump and magnetos before using the starter.

## Architecture

`Source/FlightProto/Core` contains the engine-independent flight model. `Aircraft` handles simulation and replication, `Systems` owns the engine state, `Interaction` validates switch use, and `Demo` builds the field and records the viewport. The Canvas HUD shows flight telemetry and network correction measurements.

The owning client sends redundant recent input frames. The server validates its simulation budget and publishes state with the processed sequence number. The client discards acknowledged inputs and replays the rest from the authoritative state.

## Gameplay checks

```bash
cmake -S Checks -B build
cmake --build build
./build/flight_checks
```

Nine scenarios cover parked stability, takeoff and climb, cruise bounds, glide energy loss, stall behavior, control directions, landing impact, deterministic replay and input sanitization. These checks cover the flight model; they do not establish native gameplay or multiplayer performance.

`Tools/NetSim` also exercises the replication algorithm with latency, loss and reordering outside Unreal. Its browser viewer is a visualization of that simulation. Native gameplay footage is produced only by the engine recording command above.
