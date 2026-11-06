#include "AI.hpp"

#include <Geode/utils/file.hpp>
#include <Geode/ui/Notification.hpp>
#include <algorithm>
#include <bit>
#include <cctype>
#include <cstdlib>

using namespace geode::prelude;

namespace ai {

State& state() {
    static State s;
    return s;
}

// ---------------------------------------------------------------- files

static std::string sanitize(std::string const& name) {
    std::string out;
    for (char c : name) {
        out += (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_') ? c : '_';
    }
    return out.empty() ? "level" : out;
}

static std::string jsonEscape(std::string const& str) {
    std::string out;
    for (char c : str) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) out += ' ';
                else out += c;
        }
    }
    return out;
}

std::filesystem::path macroDir() {
    return Mod::get()->getSaveDir() / "macros";
}

std::filesystem::path macroPath(GJGameLevel* level) {
    int id = level->m_levelID.value();
    std::string file = id > 0
        ? fmt::format("{}.gdr.json", id)
        : fmt::format("local_{}.gdr.json", sanitize(std::string(level->m_levelName)));
    return macroDir() / file;
}

bool hasMacro(GJGameLevel* level) {
    std::error_code ec;
    return std::filesystem::exists(macroPath(level), ec);
}

// GDR-style JSON so the run is readable by other tools too.
static std::string serializeMacro(State const& s, int finalTick) {
    std::string out;
    out += "{\n";
    out += "  \"author\": \"AI Autoplayer\",\n";
    out += "  \"description\": \"Generated automatically by the AI Autoplayer Geode mod\",\n";
    out += fmt::format("  \"duration\": {:.4f},\n", finalTick / 240.0);
    out += "  \"gameVersion\": 2.2081,\n";
    out += "  \"version\": 1.0,\n";
    out += "  \"framerate\": 240,\n";
    out += "  \"seed\": 0,\n";
    out += "  \"coins\": 0,\n";
    out += "  \"ldm\": false,\n";
    out += "  \"bot\": { \"name\": \"AI Autoplayer\", \"version\": \"1.1.0\" },\n";
    out += fmt::format("  \"level\": {{ \"id\": {}, \"name\": \"{}\" }},\n", s.levelID, jsonEscape(s.levelName));
    out += fmt::format("  \"attempts\": {},\n", s.attempts + 1);
    out += "  \"inputs\": [\n";
    for (size_t i = 0; i < s.macro.size(); i++) {
        auto const& in = s.macro[i];
        out += fmt::format(
            "    {{ \"frame\": {}, \"btn\": 1, \"2p\": false, \"down\": {} }}{}\n",
            in.tick, in.down ? "true" : "false", i + 1 < s.macro.size() ? "," : ""
        );
    }
    out += "  ]\n}\n";
    return out;
}

// Tiny reader for the file format written above.
static bool parseMacro(std::string const& text, std::vector<Input>& out) {
    out.clear();
    auto pos = text.find("\"inputs\"");
    if (pos == std::string::npos) return false;
    while (true) {
        auto f = text.find("\"frame\"", pos);
        if (f == std::string::npos) break;
        auto colon = text.find(':', f);
        if (colon == std::string::npos) return false;
        int frame = std::atoi(text.c_str() + colon + 1);

        auto d = text.find("\"down\"", colon);
        if (d == std::string::npos) return false;
        auto dc = text.find(':', d);
        if (dc == std::string::npos) return false;
        auto t = text.find_first_not_of(" \t\r\n", dc + 1);
        if (t == std::string::npos) return false;
        bool down = text.compare(t, 4, "true") == 0;

        out.push_back({frame, down});
        pos = dc + 1;
    }
    std::stable_sort(out.begin(), out.end(), [](Input const& a, Input const& b) { return a.tick < b.tick; });
    return true;
}

// ---------------------------------------------------------------- helpers

// Candidate inputs for one segment. Coarse timings first (cheap, usually enough),
// then every remaining tick for when precision is needed.
static std::vector<Pattern> makePatterns(bool heldAtStart, int len, int resolution, int fineStep) {
    std::vector<Pattern> out;
    resolution = std::clamp(resolution, 1, len);
    int coarseStep = std::max(1, len / resolution);
    constexpr int tap = 2;

    std::vector<int> coarse;
    for (int o = 0; o < len && static_cast<int>(coarse.size()) < resolution; o += coarseStep) coarse.push_back(o);

    std::vector<int> fine;
    if (fineStep > 0) {
        for (int o = 0; o < len; o += fineStep) {
            if (std::find(coarse.begin(), coarse.end(), o) == coarse.end()) fine.push_back(o);
        }
    }

    // keep doing what we're doing (nothing, or keep holding)
    out.push_back({});
    for (auto const* offsets : {&coarse, &fine}) {
        // short pulse: a tap, or a brief release while holding
        for (int o : *offsets) out.push_back({{o, !heldAtStart}, {o + tap, heldAtStart}});
        // switch state: start holding / let go
        for (int o : *offsets) out.push_back({{o, !heldAtStart}});
    }
    return out;
}

static uint64_t mix(uint64_t h, uint64_t v) {
    h ^= v + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
    return h;
}

static uint64_t hashPlayer(uint64_t h, PlayerObject* p) {
    if (!p) return mix(h, 0x1234);
    h = mix(h, std::bit_cast<uint32_t>(p->m_position.x));
    h = mix(h, std::bit_cast<uint32_t>(p->m_position.y));
    h = mix(h, std::bit_cast<uint64_t>(p->m_yVelocity));
    h = mix(h, std::bit_cast<uint32_t>(p->m_vehicleSize));
    h = mix(h, std::bit_cast<uint32_t>(p->m_playerSpeed));
    h = mix(h, std::bit_cast<uint32_t>(p->m_gravityMod));
    uint32_t flags = 0;
    int bit = 0;
    for (bool b : {
        p->m_isShip, p->m_isBird, p->m_isBall, p->m_isDart, p->m_isRobot, p->m_isSpider,
        p->m_isSwing, p->m_isUpsideDown, p->m_isOnGround, p->m_isOnSlope, p->m_wasOnSlope,
        p->m_isDashing, p->m_isSideways, p->m_isGoingLeft, p->m_jumpBuffered,
        p->m_touchedRing, p->m_stateRingJump, p->m_ringJumpRelated, p->m_isAccelerating,
    }) {
        if (b) flags |= 1u << bit;
        bit++;
    }
    return mix(h, flags);
}

static uint64_t hashState(PlayLayer* pl) {
    auto& s = state();
    uint64_t h = mix(0xA1A1A1A1ULL, static_cast<uint64_t>(s.tick));
    h = mix(h, s.held ? 1 : 0);
    h = hashPlayer(h, pl->m_player1);
    if (pl->m_gameState.m_isDualMode) h = hashPlayer(h, pl->m_player2);
    return h;
}

static void sendButton(PlayLayer* pl, bool down, bool record) {
    auto& s = state();
    if (s.held == down) return;
    s.injecting = true;
    pl->handleButton(down, 1, true);
    s.injecting = false;
    s.held = down;
    if (record) s.macro.push_back({s.tick, down});
}

static void setSpeed(float speed) {
    CCDirector::sharedDirector()->getScheduler()->setTimeScale(speed);
}

static void addStatus(PlayLayer* pl, std::string const& text) {
    auto& s = state();
    if (!Mod::get()->getSettingValue<bool>("show-status")) return;
    auto win = CCDirector::sharedDirector()->getWinSize();
    s.status = CCLabelBMFont::create(text.c_str(), "bigFont.fnt");
    s.status->setScale(0.35f);
    s.status->setAnchorPoint({0.f, 1.f});
    s.status->setPosition({6.f, win.height - 6.f});
    s.status->setOpacity(200);
    s.status->setID("ai-status"_spr);
    pl->addChild(s.status, 9999);
}

static void releaseStack() {
    auto& s = state();
    for (auto& seg : s.stack) {
        if (seg.checkpoint) seg.checkpoint->release();
    }
    s.stack.clear();
}

static void resetCommon(PlayLayer* pl, Mode mode) {
    auto& s = state();
    releaseStack();
    s.deadStates.clear();
    s.macro.clear();
    s.mode = mode;
    s.layer = pl;
    s.tick = 0;
    s.held = false;
    s.injecting = false;
    s.ourReset = false;
    s.dead = false;
    s.pruneDeath = false;
    s.attempts = 0;
    s.pruned = 0;
    s.bestPercent = 0.f;
    s.simTicks = 0;
    s.lastSimTicks = 0;
    s.lastRateTime = std::chrono::steady_clock::now();
    s.lastStatusTime = {};
    s.speedFactor = 0.f;
    s.playIdx = 0;
    s.status = nullptr;
    s.levelID = pl->m_level->m_levelID.value();
    s.levelName = std::string(pl->m_level->m_levelName);
}

// ---------------------------------------------------------------- lifecycle

void beginSearch(PlayLayer* pl) {
    auto& s = state();
    resetCommon(pl, Mode::Search);

    if (pl->m_level->isPlatformer() || pl->m_isPlatformer) {
        return fail(pl, "AI Autoplayer doesn't support platformer levels.");
    }

    auto mod = Mod::get();
    s.segLen = static_cast<int>(mod->getSettingValue<int64_t>("segment-ticks"));
    int res = static_cast<int>(mod->getSettingValue<int64_t>("input-resolution"));
    int fineStep = mod->getSettingValue<bool>("fine-timing") ? 1 : 0;
    s.maxAttempts = static_cast<int>(mod->getSettingValue<int64_t>("max-attempts"));
    s.turbo = mod->getSettingValue<bool>("turbo");
    s.turboBudgetMs = static_cast<int>(mod->getSettingValue<int64_t>("turbo-budget-ms"));
    s.skipRepeats = mod->getSettingValue<bool>("skip-repeats");
    s.patternsReleased = makePatterns(false, s.segLen, res, fineStep);
    s.patternsHeld = makePatterns(true, s.segLen, res, fineStep);

    // Practice mode gives us checkpoints to rewind to, and means the AI's
    // run never counts as a real completion on your account.
    if (!pl->m_isPracticeMode) pl->togglePracticeMode(true);
    if (!pl->m_isPracticeMode) {
        return fail(pl, "Couldn't enable practice mode, AI stopped.");
    }

    // Turbo runs extra game updates itself, so the normal game speed stays at 1x.
    setSpeed(s.turbo ? 1.f : static_cast<float>(mod->getSettingValue<double>("sim-speed")));
    addStatus(pl, "AI starting...");
}

void beginPlayback(PlayLayer* pl) {
    auto& s = state();
    resetCommon(pl, Mode::Playback);

    std::string text = file::readString(macroPath(pl->m_level)).unwrapOr(std::string());
    if (text.empty() || !parseMacro(text, s.macro)) {
        return fail(pl, "Couldn't read the saved AI run for this level.");
    }
    if (Mod::get()->getSettingValue<bool>("playback-practice") && !pl->m_isPracticeMode) {
        pl->togglePracticeMode(true);
    }
    setSpeed(1.f);
    addStatus(pl, "AI replay");
}

void stop(PlayLayer* pl) {
    auto& s = state();
    releaseStack();
    s.deadStates.clear();
    if (s.status) {
        s.status->removeFromParent();
        s.status = nullptr;
    }
    if (s.mode != Mode::None) setSpeed(1.f);
    s.mode = Mode::None;
    s.layer = nullptr;
    s.dead = false;
    s.held = false;
    s.injecting = false;
    s.ourReset = false;
}

void fail(PlayLayer* pl, std::string const& reason) {
    if (pl && state().held) sendButton(pl, false, false);
    stop(pl);
    Notification::create(reason.c_str(), NotificationIcon::Error, 4.f)->show();
}

// ---------------------------------------------------------------- search

void requestRewind(bool prune) {
    auto& s = state();
    if (s.dead) return;
    s.dead = true;
    s.pruneDeath = prune;
    // In turbo mode the update loop rewinds right away; otherwise do it next frame.
    if (!s.turbo) {
        Loader::get()->queueInMainThread([] {
            if (auto pl = PlayLayer::get()) onSearchDeath(pl);
        });
    }
}

void searchTick(PlayLayer* pl) {
    auto& s = state();

    // Start a new segment (= drop a checkpoint) once the current one is long enough.
    // Prefer to drop it while the button is released so rewinds are exact; if the AI
    // holds for a long time (ship/wave), drop it anyway after 4 segments.
    bool needNew = s.stack.empty();
    if (!needNew) {
        int age = s.tick - s.stack.back().startTick;
        needNew = age >= s.segLen && (!s.held || age >= s.segLen * 4);
    }
    if (needNew) {
        uint64_t h = hashState(pl);
        if (s.skipRepeats && !s.stack.empty() && s.deadStates.contains(h)) {
            // Some other input sequence already reached exactly this state and it
            // went nowhere, so don't waste time playing it out again.
            return requestRewind(true);
        }
        auto arr = pl->m_checkpointArray;
        auto cp = pl->markCheckpoint();
        if (cp && arr && arr->containsObject(cp)) {
            cp->retain();
            s.stack.push_back({s.tick, s.held, 0, s.macro.size(), cp, h});
        }
    }
    if (s.stack.empty()) return;

    auto const& seg = s.stack.back();
    auto const& pats = seg.heldAtStart ? s.patternsHeld : s.patternsReleased;
    int rel = s.tick - seg.startTick;
    for (auto const& ev : pats[seg.pattern]) {
        if (ev.offset == rel) sendButton(pl, ev.down, true);
    }
}

void onSearchDeath(PlayLayer* pl) {
    auto& s = state();
    if (s.mode != Mode::Search || s.layer != pl || !s.dead) return;

    if (s.pruneDeath) {
        s.pruned++;
    }
    else {
        s.attempts++;
        s.bestPercent = std::max(s.bestPercent, pl->getCurrentPercent());
        if (s.attempts >= s.maxAttempts) {
            return fail(pl, fmt::format("AI gave up after {} attempts (best {:.0f}%).", s.attempts, s.bestPercent));
        }
    }
    s.pruneDeath = false;

    if (s.held) sendButton(pl, false, false);

    // Try the next pattern for the newest segment; if it has none left, that segment's
    // start state is a dead end: remember it, drop the segment, and move on to the next
    // pattern of the one before it.
    while (!s.stack.empty()) {
        auto& seg = s.stack.back();
        seg.pattern++;
        auto const& pats = seg.heldAtStart ? s.patternsHeld : s.patternsReleased;
        if (seg.pattern < static_cast<int>(pats.size())) break;
        if (s.skipRepeats) s.deadStates.insert(seg.startHash);
        if (seg.checkpoint) seg.checkpoint->release();
        s.stack.pop_back();
    }
    if (s.stack.empty()) {
        return fail(pl, fmt::format(
            "AI ran out of options (best {:.0f}%). Try a smaller segment or more input positions in the mod settings.",
            s.bestPercent
        ));
    }

    auto& seg = s.stack.back();
    auto arr = pl->m_checkpointArray;
    if (!arr || !arr->containsObject(seg.checkpoint)) {
        return fail(pl, "AI lost track of its checkpoints and stopped.");
    }
    // Make our segment's checkpoint the newest one so the practice respawn uses it.
    while (arr->count() > 0 && arr->lastObject() != seg.checkpoint) {
        pl->removeCheckpoint(false);
    }

    s.macro.resize(seg.macroSize);
    s.tick = seg.startTick;
    s.dead = false;

    s.ourReset = true;
    pl->resetLevel();
    s.ourReset = false;

    if (seg.heldAtStart) sendButton(pl, true, false);
}

// ---------------------------------------------------------------- playback

void playbackTick(PlayLayer* pl) {
    auto& s = state();
    while (s.playIdx < s.macro.size() && s.macro[s.playIdx].tick <= s.tick) {
        sendButton(pl, s.macro[s.playIdx].down, false);
        s.playIdx++;
    }
}

// ---------------------------------------------------------------- finish

void onLevelComplete(PlayLayer* pl) {
    auto& s = state();
    if (s.mode == Mode::Playback) {
        stop(pl);
        Notification::create("AI replay finished the level!", NotificationIcon::Success)->show();
        return;
    }
    if (s.mode != Mode::Search) return;

    int attempts = s.attempts + 1;
    int pruned = s.pruned;
    int finalTick = s.tick;
    auto path = macroPath(pl->m_level);
    (void)file::createDirectoryAll(macroDir());
    auto res = file::writeString(path, serializeMacro(s, finalTick));
    stop(pl);

    if (!res) {
        Notification::create("AI beat the level but couldn't save the run!", NotificationIcon::Error, 4.f)->show();
        return;
    }

    auto msg = fmt::format(
        "The AI beat this level in <cy>{}</c> attempts (and skipped <cl>{}</c> repeats).\n\n"
        "Saved run:\n<cg>{}</c>\n\nUse the replay button on the level page to watch it.",
        attempts, pruned, path.string()
    );
    Loader::get()->queueInMainThread([msg] {
        createQuickPopup("AI Finished!", msg, "OK", "Open Folder", [](FLAlertLayer*, bool openFolder) {
            if (openFolder) file::openFolder(macroDir());
        });
    });
}

void updateStatus(PlayLayer* pl) {
    auto& s = state();
    if (!s.status) return;

    auto now = std::chrono::steady_clock::now();
    if (now - s.lastStatusTime < std::chrono::milliseconds(100)) return;
    s.lastStatusTime = now;

    float secs = std::chrono::duration<float>(now - s.lastRateTime).count();
    if (secs >= 0.5f) {
        s.speedFactor = static_cast<float>(s.simTicks - s.lastSimTicks) / 240.f / secs;
        s.lastSimTicks = s.simTicks;
        s.lastRateTime = now;
    }

    if (s.mode == Mode::Search) {
        s.status->setString(fmt::format(
            "AI solving | attempt {} | skipped {} | {:.0f}% (best {:.0f}%) | {:.0f}x speed",
            s.attempts + 1, s.pruned, pl->getCurrentPercent(), s.bestPercent, s.speedFactor
        ).c_str());
    }
    else if (s.mode == Mode::Playback) {
        s.status->setString(fmt::format("AI replay | {:.0f}%", pl->getCurrentPercent()).c_str());
    }
}

} // namespace ai
