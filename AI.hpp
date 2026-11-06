#pragma once

#include <Geode/Geode.hpp>
#include <filesystem>
#include <string>
#include <vector>

// The AI works like this:
//   * The level is played in practice mode, one physics tick (1/240 s) at a time.
//   * Every "segment" (default 30 ticks) it drops a practice checkpoint and picks an
//     input pattern for that segment (do nothing, tap at some moment, start/stop holding...).
//   * When the player dies, it rewinds to the last checkpoint and tries the next pattern.
//     If every pattern in a segment dies, it removes that checkpoint and backs up one more
//     segment (depth-first search with backtracking).
//   * When the level is completed, the inputs that survived are written to a macro file.

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
};

struct State {
    Mode pendingMode = Mode::None; // set on the level page, consumed by PlayLayer::init
    Mode mode = Mode::None;
    PlayLayer* layer = nullptr;

    int levelID = 0;
    std::string levelName;

    // shared
    int tick = 0;          // physics ticks since the attempt started
    bool held = false;     // is the AI holding the jump button right now
    bool injecting = false;// true while the AI itself is calling handleButton
    bool ourReset = false; // true while the AI itself is calling resetLevel
    std::vector<Input> macro;

    // search
    int segLen = 30;
    std::vector<Pattern> patternsReleased; // candidates when the segment starts released
    std::vector<Pattern> patternsHeld;     // candidates when the segment starts held
    std::vector<Segment> stack;
    bool dead = false;
    int attempts = 0;
    int maxAttempts = 50000;
    float bestPercent = 0.f;

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
void onSearchDeath(PlayLayer* pl);
void onLevelComplete(PlayLayer* pl);
void updateStatus(PlayLayer* pl);

} // namespace ai
