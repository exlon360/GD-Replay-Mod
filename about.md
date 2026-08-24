# Replay Mod

<cp>Replay Mod</c> records the buttons you press during an attempt, then lets Geometry Dash re-simulate those inputs from the beginning.

Open the purple button during a level to:

- browse and search saved attempts;
- watch the latest compatible replay;
- pause or resume playback;
- choose a playback speed from 0.25x to 4x;
- toggle Geometry Dash's debug hitbox drawing.

## Safety

Watching a replay is clearly marked and uses a protected playback session. Replay completion never calls the normal completion path, so it cannot award stars, coins, records, or achievements.

## Early-version limits

Replay playback is best-effort and not yet desync-verified. Rapid sub-step input, random-heavy levels, physics changes, or other gameplay mods can cause a run to diverge. Speed controls change the game scheduler, but music remains at its normal rate outside 1x.

Practice recording, seeking, video rendering, non-colliding ghost playback, replay retention controls, and large-library pagination are planned separately rather than being faked in this first version.
