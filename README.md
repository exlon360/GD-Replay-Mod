# Replay Mod

Replay Mod is a Geode mod for Geometry Dash 2.2081. It records ordered gameplay inputs and can re-simulate a saved attempt from the beginning of the same level.

## MVP features

- automatic attempt recording
- searchable replay library
- safe replay viewing (the completion path is blocked while watching)
- pause/resume and 0.25x, 0.5x, 1x, 2x, and 4x speed presets
- vanilla debug hitbox toggle
- purple in-level replay button and status badge

## Important limitations

This is an input replay, not a video recorder. Playback is best-effort and unverified: the first version records step order but does not yet use its sub-step timestamps or state checkpoints. Rapid click-between-steps input, random-heavy levels, physics changes, and gameplay mods can therefore desync a run.

The first version does not seek backwards, render video, draw a separate ghost, or record practice attempts. Speed changes affect the whole game scheduler while music remains at its normal rate, so audio drifts outside 1x. The library currently keeps attempts until its replay files are manually removed; retention controls, deletion, and pagination are planned before a public release.

Vanilla hitboxes are visible during watched replays (which run inside a protected practice environment), ordinary practice, or after death. In a normal live attempt the control reads **Hitboxes Armed** until Geometry Dash permits the debug drawing.

## Build on macOS

1. Install the Geode CLI and SDK by following the official Geode setup guide.
2. Set `GEODE_SDK` to the SDK directory.
3. Run `geode build` in this directory.

The metadata currently uses `Lyric` and `lyric.replay-mod`; change these before publishing if you use a different creator name.
