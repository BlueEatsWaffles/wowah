#include "AI.hpp"

#include <Geode/utils/file.hpp>
#include <Geode/ui/Notification.hpp>
#include <algorithm>
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
    out += "  \"bot\": { \"name\": \"AI Autoplayer\", \"version\": \"1.0.0\" },\n";
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

static std::vector<Pattern> makePatterns(bool heldAtStart, int len, int resolution) {
    std::vector<Pattern> out;
    int step = std::max(1, len / std::max(1, resolution));
    int tap = std::clamp(step / 2, 1, 6);

    // 1. keep doing what we're doing (nothing, or keep holding)
    out.push_back({});
    // 2. short pulse (a tap, or a brief release while holding) at different moments
    for (int k = 0; k < resolution; k++) {
        int o = k * step;
        if (o + tap < len) out.push_back({{o, !heldAtStart}, {o + tap, heldAtStart}});
    }
    // 3. switch state (start holding / let go) at different moments
    for (int k = 0; k < resolution; k++) {
        int o = k * step;
        if (o < len) out.push_back({{o, !heldAtStart}});
    }
    return out;
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

static void resetCommon(PlayLayer* pl, Mode mode) {
    auto& s = state();
    for (auto& seg : s.stack) {
        if (seg.checkpoint) seg.checkpoint->release();
    }
    s.stack.clear();
    s.macro.clear();
    s.mode = mode;
    s.layer = pl;
    s.tick = 0;
    s.held = false;
    s.injecting = false;
    s.ourReset = false;
    s.dead = false;
    s.attempts = 0;
    s.bestPercent = 0.f;
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

    s.segLen = static_cast<int>(Mod::get()->getSettingValue<int64_t>("segment-ticks"));
    int res = static_cast<int>(Mod::get()->getSettingValue<int64_t>("input-resolution"));
    s.maxAttempts = static_cast<int>(Mod::get()->getSettingValue<int64_t>("max-attempts"));
    s.patternsReleased = makePatterns(false, s.segLen, res);
    s.patternsHeld = makePatterns(true, s.segLen, res);

    // Practice mode gives us checkpoints to rewind to, and means the AI's
    // run never counts as a real completion on your account.
    if (!pl->m_isPracticeMode) pl->togglePracticeMode(true);
    if (!pl->m_isPracticeMode) {
        return fail(pl, "Couldn't enable practice mode, AI stopped.");
    }

    setSpeed(static_cast<float>(Mod::get()->getSettingValue<double>("sim-speed")));
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
    for (auto& seg : s.stack) {
        if (seg.checkpoint) seg.checkpoint->release();
    }
    s.stack.clear();
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
        auto arr = pl->m_checkpointArray;
        auto cp = pl->markCheckpoint();
        if (cp && arr && arr->containsObject(cp)) {
            cp->retain();
            s.stack.push_back({s.tick, s.held, 0, s.macro.size(), cp});
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

    s.attempts++;
    s.bestPercent = std::max(s.bestPercent, pl->getCurrentPercent());
    if (s.attempts >= s.maxAttempts) {
        return fail(pl, fmt::format("AI gave up after {} attempts (best {:.0f}%).", s.attempts, s.bestPercent));
    }

    if (s.held) sendButton(pl, false, false);

    // Try the next pattern for the newest segment; if it has none left, drop that
    // segment and move on to the next pattern of the one before it.
    while (!s.stack.empty()) {
        auto& seg = s.stack.back();
        seg.pattern++;
        auto const& pats = seg.heldAtStart ? s.patternsHeld : s.patternsReleased;
        if (seg.pattern < static_cast<int>(pats.size())) break;
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
        "The AI beat this level in <cy>{}</c> attempts.\n\nSaved run:\n<cg>{}</c>\n\nUse the replay button on the level page to watch it.",
        attempts, path.string()
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
    if (s.mode == Mode::Search) {
        s.status->setString(fmt::format(
            "AI solving | attempt {} | {:.0f}% (best {:.0f}%) | depth {}",
            s.attempts + 1, pl->getCurrentPercent(), s.bestPercent, s.stack.size()
        ).c_str());
    }
    else if (s.mode == Mode::Playback) {
        s.status->setString(fmt::format("AI replay | {:.0f}%", pl->getCurrentPercent()).c_str());
    }
}

} // namespace ai
