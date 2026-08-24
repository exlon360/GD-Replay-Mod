# Replay Mod

Replay Mod by **xVionGD** is a Geode mod for Geometry Dash 2.2081. It records ordered gameplay inputs and can re-simulate a saved attempt from the beginning of the same level.

## How it works

- Normal attempts are recorded automatically when recording is enabled.
- A purple **Replay** button appears on the level selection or level info screen, before you press the normal **Play** button.
- The Replay screen opens a dedicated viewer for that level. Its latest compatible attempt can be watched immediately, or the current level's attempts can be searched and selected.
- Pause, resume, 0.25x, 0.5x, 1x, 2x, and 4x speed controls and the vanilla debug hitbox toggle are shown only while watching a replay.
- When the recorded player dies, the viewer freezes on the death frame instead of continuing into a live attempt.
- Replay viewing is protected: the normal completion path is blocked while a replay is running.

## Important limitations

This is an input replay, not a video recorder. Playback is best-effort and unverified: Replay Mod records input order but does not yet use sub-step timestamps or state checkpoints. Rapid click-between-steps input, random-heavy levels, physics changes, and gameplay mods can therefore desync a run.

Version 0.2.0 does not seek backwards, render video, draw a separate ghost, or record practice attempts. Speed changes affect the whole game scheduler while music remains at its normal rate, so audio can drift outside 1x. The library keeps attempts until its replay files are manually removed; retention controls, deletion, and pagination are planned separately.

Vanilla hitboxes are available during watched replays, which run inside a protected practice environment. Replay controls are not placed over normal live gameplay.

## Build on macOS

1. Install the Geode CLI and SDK by following the official Geode setup guide.
2. Set `GEODE_SDK` to the SDK directory.
3. Run `geode build` in this directory.
