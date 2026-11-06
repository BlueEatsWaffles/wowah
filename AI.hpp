#pragma once

#include <Geode/Geode.hpp>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_set>
#include <vector>

// How the AI works:
//   * The level is played in practice mode, one physics tick (1/240 s) at a time.
//   * Every "segment" (16 ticks by default) it drops a practice checkpoint and picks an
//     input pattern for that stretch (do nothing, tap at some tick, start/stop holding...).
//     Coarse timings are tried first; if they all fail, every single tick is tried.
//   * When the player dies, it rewinds to the last checkpoint and tries the next pattern.
//     If every pattern in a segment dies, it removes that checkpoint and backs up one more
//     segment (depth-first search with backtracking).
//   * Smart pruning: the exact player state (tick, position, velocity, gamemode, ...) at the
//     start of every segment that turned out to be a dead end is remembered. If any other
//     input sequence leads to that same state again, it's skipped instantly.
//   * Turbo: instead of one game update per frame, the AI runs as many as fit in a time
//     budget each frame, and rewinds immediately on death.
//   * When the level is completed, the surviving inputs are written to a macro file.

namespace ai {

enum class Mode { None, Search, Playback };

struct Input {
    int tick;
    bool down;
};

// One timed button change inside a segment (offset is relative to the segment start).
struct Event {
    int offset;
    bool down;
};
using Pattern = std::vector<Event>;

struct Segment {
    int startTick;
    bool heldAtStart;
    int pattern;                  // index of the pattern currently being tried
    size_t macroSize;             // inputs recorded before this segment began
    CheckpointObject* checkpoint; // retained by us
    uint64_t startHash;           // player state when this segment began
};

struct State {
    Mode pendingMode = Mode::None; // set on the level page, consumed by PlayLayer::init
    Mode mode = Mode::None;
    PlayLayer* layer = nullptr;

    int levelID = 0;
    std::string levelName;

    // shared
    int tick = 0;           // physics ticks since the attempt started
    bool held = false;      // is the AI holding the jump button right now
    bool injecting = false; // true while the AI itself is calling handleButton
    bool ourReset = false;  // true while the AI itself is calling resetLevel
    std::vector<Input> macro;

    // search settings
    int segLen = 16;
    bool turbo = true;
    int turboBudgetMs = 12;
    bool skipRepeats = true;
    int maxAttempts = 200000;
    std::vector<Pattern> patternsReleased; // candidates when the segment starts released
    std::vector<Pattern> patternsHeld;     // candidates when the segment starts held

    // search state
    std::vector<Segment> stack;
    std::unordered_set<uint64_t> deadStates;
    bool dead = false;
    bool pruneDeath = false; // the current "death" is a skipped repeat, not a real death
    int attempts = 0;
    int pruned = 0;
    float bestPercent = 0.f;

    // speed measurement for the status label
    int64_t simTicks = 0;
    int64_t lastSimTicks = 0;
    std::chrono::steady_clock::time_point lastRateTime{};
    std::chrono::steady_clock::time_point lastStatusTime{};
    float speedFactor = 0.f;

    // playback
    size_t playIdx = 0;

    cocos2d::CCLabelBMFont* status = nullptr;
};

State& state();

std::filesystem::path macroDir();
std::filesystem::path macroPath(GJGameLevel* level);
bool hasMacro(GJGameLevel* level);

void beginSearch(PlayLayer* pl);
void beginPlayback(PlayLayer* pl);
void stop(PlayLayer* pl);
void fail(PlayLayer* pl, std::string const& reason);

void searchTick(PlayLayer* pl);
void playbackTick(PlayLayer* pl);
void requestRewind(bool prune);
void onSearchDeath(PlayLayer* pl);
void onLevelComplete(PlayLayer* pl);
void updateStatus(PlayLayer* pl);

} // namespace ai
