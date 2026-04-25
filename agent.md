# Racing Logic Implementation Summary

This document summarizes the work done on the `feature/racing-logic` branch to add lap tracking and a checkpoint system to the ECS game engine.

## 1. New ECS Components
We added two new components to manage race data:
* **`CheckpointComponent`** (`source/common/components/checkpoint.hpp` & `.cpp`)
  * Represents a checkpoint on the track.
  * Fields: `index` (the sequence number of the checkpoint, starting at 0) and `radius` (the detection distance).
* **`RaceProgressComponent`** (`source/common/components/race-progress.hpp` & `.cpp`)
  * Attached to racers (e.g., the player's car).
  * Fields: `currentLap`, `nextCheckpointIndex` (the target checkpoint), and `totalLaps`.

## 2. New ECS System
* **`RaceSystem`** (`source/common/systems/race.hpp`)
  * Every frame, it collects all checkpoints in the world and sorts them by their `index`.
  * For every entity with a `CarControllerComponent` and `RaceProgressComponent`, it checks the 2D (XZ plane) distance to its next target checkpoint.
  * If the racer is within the checkpoint's `radius`, their `nextCheckpointIndex` is incremented.
  * If they complete the final checkpoint, their `currentLap` is incremented, and the target index resets to 0.

## 3. Play State Updates
We wired everything into the main game loop in `source/states/play-state.hpp`:
* Added `RaceSystem` to the list of systems updated each frame (`onDraw`).
* **Lap HUD**: Added a new UI panel in `onImmediateGui` that displays the current lap, the next target checkpoint, and a "RACE FINISHED!" message when all laps are complete.
* **Checkpoint Placement Mode**:
  * Added a tool to physically place checkpoints while driving the car.
  * Press **`C`** to toggle placement mode.
  * Press **`P`** to place a checkpoint at the player's current position.
  * Press **`U`** to undo the last placement.
  * Press **`O`** to output all placed checkpoints as a JSON array to the console (ready to copy-paste into `app.jsonc`).
  * Draws visual green circle markers and labels (e.g., "CP 0") in the 3D world for all placed checkpoints.

## 4. Wiring and Configuration
* **Deserialization**: Updated `source/common/components/component-deserializer.hpp` to instantiate `CheckpointComponent` and `RaceProgressComponent` from JSON config.
* **Build System**: Added the new `.cpp` and `.hpp` files to `CMakeLists.txt` so they compile correctly.
* **`app.jsonc` Updates**:
  * Added the `Race Progress` component to the `standard_kart` and `boom_karts_kart` presets (defaulting to 3 laps).
  * Added 9 actual checkpoint entities to the `south_garda` track preset based on real driving coordinates.
  * Set the checkpoint detection radii to `15.0` to ensure players don't have to drive perfectly over the center point to trigger them.
