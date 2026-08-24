# Replay Mod

<cp>Replay Mod</c> by <cl>xVionGD</c> records the buttons you press during an attempt, then lets Geometry Dash re-simulate those inputs from the beginning.

Before pressing the normal <cg>Play</c> button, open the purple <cp>Replay</c> button on the level selection or level info screen to:

- open a dedicated replay viewer for the selected level;
- search that level's saved attempts;
- watch the latest compatible replay;
- pause or resume watched footage;
- choose a playback speed from 0.25x to 4x;
- toggle Geometry Dash's debug hitbox drawing;
- freeze on the recorded death frame.

Replay controls appear only while watching a replay. They are not placed over a normal live attempt.

## Safety

Watching a replay is clearly marked and uses a protected playback session. Replay completion never calls the normal completion path, so it cannot award stars, coins, records, or achievements.

## Early-version limits

This is an input replay rather than recorded video. Playback is best-effort and not yet desync-verified. Rapid sub-step input, random-heavy levels, physics changes, or other gameplay mods can cause a run to diverge. Speed controls change the whole game scheduler, but music remains at its normal rate outside 1x and can drift.

Practice recording, seeking, video rendering, non-colliding ghost playback, replay retention controls, and large-library pagination are planned separately.
