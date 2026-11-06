#include "AI.hpp"

#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <algorithm>
#include <chrono>

using namespace geode::prelude;

static bool isAILayer(GJBaseGameLayer* layer) {
    auto& s = ai::state();
    return s.mode != ai::Mode::None && s.layer && static_cast<GJBaseGameLayer*>(s.layer) == layer;
}

class $modify(AIBaseLayer, GJBaseGameLayer) {
    // Block your own clicks while the AI is driving so they can't mess up the run.
    void handleButton(bool down, int button, bool isPlayer1) {
        if (isAILayer(this) && !ai::state().injecting) return;
        GJBaseGameLayer::handleButton(down, button, isPlayer1);
    }

    // Turbo ("speed hack"): while searching, run as many game updates as fit in the
    // frame's time budget instead of just one, and rewind immediately on death.
    void update(float dt) {
        auto& s = ai::state();
        if (!isAILayer(this) || s.mode != ai::Mode::Search || !s.turbo) {
            return GJBaseGameLayer::update(dt);
        }

        auto start = std::chrono::steady_clock::now();
        auto budget = std::chrono::milliseconds(std::max(1, s.turboBudgetMs));
        int loops = 0;
        while (true) {
            if (s.dead) ai::onSearchDeath(s.layer);
            if (!isAILayer(this) || s.mode != ai::Mode::Search) break;

            GJBaseGameLayer::update(1.f / 60.f);
            loops++;

            if (!isAILayer(this) || m_levelEndAnimationStarted) break;
            if (loops >= 5000 || std::chrono::steady_clock::now() - start >= budget) break;
        }
        // If the AI stopped before running anything, still give the game its normal update.
        if (loops == 0) GJBaseGameLayer::update(dt);
    }

    // Called once per physics tick (240 per second).
    void processCommands(float dt, bool isHalfTick, bool isLastTick) {
        if (!isAILayer(this) || !m_started || m_levelEndAnimationStarted) {
            return GJBaseGameLayer::processCommands(dt, isHalfTick, isLastTick);
        }
        auto& s = ai::state();
        auto pl = s.layer;

        if (s.mode == ai::Mode::Search) {
            if (s.dead) return; // frozen until the rewind runs
            ai::searchTick(pl);
            if (s.mode != ai::Mode::Search) return GJBaseGameLayer::processCommands(dt, isHalfTick, isLastTick);
            if (s.dead) return; // this state was already known to be a dead end
        }
        else {
            ai::playbackTick(pl);
        }

        GJBaseGameLayer::processCommands(dt, isHalfTick, isLastTick);
        s.tick++;
        s.simTicks++;
    }
};

class $modify(AIPlayLayer, PlayLayer) {
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        auto& s = ai::state();
        auto mode = s.pendingMode;
        s.pendingMode = ai::Mode::None;
        if (s.mode != ai::Mode::None) {
            s.status = nullptr; // belonged to an old layer
            ai::stop(nullptr);
        }

        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;

        if (mode == ai::Mode::Search) ai::beginSearch(this);
        else if (mode == ai::Mode::Playback) ai::beginPlayback(this);
        return true;
    }

    void destroyPlayer(PlayerObject* player, GameObject* object) {
        auto& s = ai::state();
        if (!isAILayer(this) || object == m_anticheatSpike) {
            return PlayLayer::destroyPlayer(player, object);
        }

        if (s.mode == ai::Mode::Playback) {
            PlayLayer::destroyPlayer(player, object);
            if (!m_levelEndAnimationStarted) {
                ai::fail(this, fmt::format("AI replay died at {:.0f}% (the run desynced).", this->getCurrentPercent()));
            }
            return;
        }

        // Search mode: don't actually die, rewind to a checkpoint and try something else.
        if (m_levelEndAnimationStarted) return;
        ai::requestRewind(false);
    }

    void resetLevel() {
        auto& s = ai::state();
        if (isAILayer(this)) {
            if (s.mode == ai::Mode::Search && !s.ourReset) {
                if (!s.stack.empty()) {
                    ai::fail(this, "Level was restarted manually, AI stopped.");
                }
                else {
                    s.tick = 0;
                    s.held = false;
                    s.macro.clear();
                }
            }
            else if (s.mode == ai::Mode::Playback) {
                s.tick = 0;
                s.playIdx = 0;
                s.held = false;
            }
        }
        PlayLayer::resetLevel();
    }

    void levelComplete() {
        if (isAILayer(this)) ai::onLevelComplete(this);
        PlayLayer::levelComplete();
    }

    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);
        if (isAILayer(this)) ai::updateStatus(this);
    }

    void onExit() {
        if (ai::state().layer == this) ai::stop(this);
        PlayLayer::onExit();
    }
};
