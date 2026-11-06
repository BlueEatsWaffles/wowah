# AI Autoplayer (Geode mod for Geometry Dash 2.2081, PC)

Puts an **AI** button under the copy button on a level's page. Press it and an AI plays the level until it beats it, then saves the winning run to your PC. A **Replay** button then shows up under it so you can watch the saved run.

## How the AI works

It's a search bot that uses the game's own physics, not a neural network:

1. The level runs in **practice mode**, one physics tick (1/240 s) at a time.
2. Every *segment* (16 ticks by default) the AI drops a checkpoint and picks an input pattern for that stretch: do nothing, tap, start holding, let go.
   - It tries a few quick timings first.
   - If none of them work, it tries **every single tick** (frame-perfect timing).
3. When it dies, it rewinds to the last checkpoint and tries the next pattern. If a whole segment fails, it backs up one more segment.
4. **Skip repeats:** it remembers the exact situation (moment, position, speed, gamemode) at the start of every segment that led nowhere. Many different inputs end up in the same situation, for example taps in mid-air that do nothing, and the AI skips those instantly instead of replaying them.
5. **Turbo (speed hack):** while searching, it runs as many game updates as fit in each frame instead of just one, and rewinds instantly on death.
6. When it reaches the end, the surviving inputs are written to a macro file.

Because it runs in practice mode, the AI's run **never counts as your completion** and gives you no stars. The Replay button also uses practice mode by default (you can change that in settings).

The top-left status shows four things:
- `attempt`: real deaths
- `skipped`: repeats it avoided
- the current and best percent
- how many times faster than normal it's simulating

## Where the run is saved

```
<Geometry Dash folder>\geode\save\aiautoplayer.ai-autoplayer\macros\<levelID>.gdr.json
```

When the AI finishes, a popup shows the exact path, with an **Open Folder** button.

The file uses a GDR-style JSON layout (`frame` = physics tick at 240 TPS, plus `down` true/false).

## Settings

| Setting | Default | What it does |
|---|---|---|
| Turbo (Speed Hack) | on | Runs many game updates per frame while searching. |
| Turbo Budget (ms per frame) | 12 | Higher is faster, but the window gets choppier. |
| Skip Repeated Situations | on | Never re-tries a situation that already failed. |
| Frame-Perfect Timing | on | Falls back to trying every tick when the quick timings fail. |
| Decision Segment (ticks) | 16 | Ticks between checkpoints. |
| Quick Timings per Segment | 4 | Click moments tried before frame-perfect timing kicks in. |
| Game Speed (when Turbo is off) | 4x | Old-style speed-up if you turn Turbo off. |
| Max Attempts | 200000 | Gives up after this many real deaths. |
| Replay in Practice Mode | on | Replays never count as real completions. |
| Show AI Status | on | Status text in the top-left. |

To stop the AI, pause and leave the level. Restarting manually also stops it.

## Other mods

While the AI is running, turn these off:
- **Noclip** in Mega Hack (or any other mod). The AI needs to see deaths.
- **Speedhack** in Mega Hack.
- **xdBot or zcblive playback.**

They hook the same parts of the game and can confuse the AI.

## Building it

### Option A: GitHub, no tools needed
1. Upload these files to a GitHub repo, then add `.github/workflows/build.yml`, or paste its contents via Actions, then **set up a workflow yourself**.
2. Wait for the green ✓ in the **Actions** tab, open the run's **Summary**, and download **AI-Autoplayer** under **Artifacts**.
3. Put the `.geode` file from it in `<Geometry Dash folder>\geode\mods\`.

### Option B: build on your PC
1. Install Visual Studio 2022 (with the "Desktop development with C++" workload), CMake, and the [Geode CLI](https://github.com/geode-sdk/cli/releases) **v3.x**.
2. Run `geode sdk install`, then `geode sdk install-binaries` (pick **v5.11.2**, the version for GD 2.2081).
3. In this folder, run `geode build`.

## Limitations

- Classic, 1-player levels only. Platformer and 2-player levels are blocked.
- Very hard levels can still take a long time.
- Practice checkpoints don't restore every trigger perfectly. On some trigger-heavy levels the Replay might desync, and if it does you'll get a message saying at what percent.
- 2.2081 only (Geode v5.11.2).
