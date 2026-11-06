# AI Autoplayer (Geode mod for Geometry Dash 2.2081, PC)

Puts an **AI** button under the copy button on a level's page. Press it and an AI plays the level until it beats it, then saves the winning run to your PC. A **Replay** button then shows up under it so you can watch the saved run.

## How the AI works

There's no neural network. It's a search bot that uses the game's own physics:

1. The level runs in **practice mode**, one physics tick (1/240 s) at a time.
2. Every *segment* (30 ticks by default, about 1/8 s) the AI drops a checkpoint and picks an input pattern for that stretch: do nothing, tap, start holding, let go, and so on, at a few different moments.
3. When it dies, it rewinds to the last checkpoint and tries the next pattern. If every pattern in a segment dies, it deletes that checkpoint and backs up one more segment (depth-first search with backtracking).
4. When it reaches the end, the surviving inputs are written to a macro file.

Because it runs in practice mode, the AI's run **never counts as your completion** and gives you no stars. The Replay button also uses practice mode by default (you can change that in settings).

## Where the run is saved

```
<Geometry Dash folder>\geode\save\aiautoplayer.ai-autoplayer\macros\<levelID>.gdr.json
```

When the AI finishes, a popup shows the exact path, with an **Open Folder** button.

The file uses a GDR-style JSON layout (`frame` = physics tick at 240 TPS, plus `down` true/false), so other macro tools may be able to read it. It's mainly meant for this mod's own Replay button, though.

## Settings

| Setting | Default | What it does |
|---|---|---|
| Decision Segment (ticks) | 30 | Ticks between checkpoints. Smaller gives finer control but a slower search. |
| Input Positions per Segment | 3 | How many moments per segment it tries clicking at. Raise it for tight timings. |
| AI Game Speed | 4x | Speeds the game up while the AI searches. |
| Max Attempts | 50000 | Gives up after this many deaths. |
| Replay in Practice Mode | on | Replays never count as real completions. |
| Show AI Status | on | Attempt counter and progress in the top-left. |

To stop the AI, pause and leave the level. Restarting manually also stops it.

## Building it (you need to do this once)

### Option A: GitHub, no tools needed (easiest)
1. Make a new GitHub repo and upload everything in this folder, including `.github/`.
2. Open the **Actions** tab. The "Build Geode Mod" workflow runs on its own, or you can press *Run workflow*.
3. When it finishes, download the **AI-Autoplayer** artifact and unzip it to get the `.geode` file.
4. Put the `.geode` file in `<Geometry Dash folder>\geode\mods\` and start the game.

### Option B: build on your PC
1. Install Visual Studio 2022 (with the "Desktop development with C++" workload), CMake, and the [Geode CLI](https://github.com/geode-sdk/cli/releases) **v3.x**.
2. Run `geode sdk install`, then `geode sdk install-binaries` (pick **v5.11.2**, the version for GD 2.2081).
3. In this folder, run:
   ```bash
   geode build
   ```
   With Geode set up, this also installs the mod into your GD folder.

## Limitations

- Classic, 1-player levels only. Platformer and 2-player levels are blocked.
- Hard levels (tight wave sections, lots of 1-frame timings) can take a very long time, or run out of options. In that case, try a lower **Decision Segment** and a higher **Input Positions** value.
- Practice checkpoints don't restore every trigger perfectly. On some trigger-heavy levels the Replay might desync, and if it does you'll get a message saying at what percent.
- 2.2081 only (Geode v5.11.2). Other GD versions need different bindings and a different Geode version.
