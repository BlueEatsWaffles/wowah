#include "AI.hpp"

#include <Geode/modify/LevelInfoLayer.hpp>

using namespace geode::prelude;

class $modify(AILevelInfoLayer, LevelInfoLayer) {
    bool init(GJGameLevel* level, bool challenge) {
        if (!LevelInfoLayer::init(level, challenge)) return false;
        this->addAIButtons();
        return true;
    }

    CCMenuItemSpriteExtra* makeButton(char const* text, char const* bg, float targetHeight, SEL_MenuHandler cb, char const* id) {
        auto spr = ButtonSprite::create(text, "goldFont.fnt", bg, 0.8f);
        if (spr->getContentSize().height > 0.f) {
            spr->setScale(targetHeight / spr->getContentSize().height);
        }
        auto btn = CCMenuItemSpriteExtra::create(spr, this, cb);
        btn->setID(id);
        return btn;
    }

    void addAIButtons() {
        auto clone = m_cloneBtn;
        float size = clone ? clone->getScaledContentSize().height : 35.f;
        if (size < 20.f) size = 35.f;

        std::vector<CCMenuItemSpriteExtra*> buttons;
        buttons.push_back(makeButton("AI", "GJ_button_01.png", size * 0.9f,
            menu_selector(AILevelInfoLayer::onAISolve), "ai-solve-button"_spr));
        if (ai::hasMacro(m_level)) {
            buttons.push_back(makeButton("Replay", "GJ_button_02.png", size * 0.7f,
                menu_selector(AILevelInfoLayer::onAIReplay), "ai-replay-button"_spr));
        }

        auto menu = clone ? typeinfo_cast<CCMenu*>(clone->getParent()) : nullptr;

        if (menu && menu->getLayout()) {
            // The copy button's menu auto-arranges its children, just add ours at the end.
            for (auto b : buttons) menu->addChild(b);
            menu->updateLayout();
            return;
        }

        // Otherwise stack them manually right underneath the copy button.
        CCPoint anchor;
        float prevHalf;
        if (menu) {
            anchor = clone->getPosition();
            prevHalf = clone->getScaledContentSize().height / 2.f;
        }
        else {
            // No copy button on this page: make our own menu on the left side.
            auto win = CCDirector::sharedDirector()->getWinSize();
            menu = CCMenu::create();
            menu->setID("ai-menu"_spr);
            menu->setPosition({0.f, 0.f});
            this->addChild(menu, 10);
            anchor = ccp(win.width / 2.f - 205.f, win.height / 2.f - 20.f);
            prevHalf = 0.f;
        }

        float y = anchor.y;
        for (auto b : buttons) {
            float half = b->getScaledContentSize().height / 2.f;
            y -= prevHalf + 6.f + half;
            b->setPosition({anchor.x, y});
            menu->addChild(b);
            prevHalf = half;
        }
    }

    bool canRunAI() {
        if (m_isBusy || !m_level) return false;
        if (m_level->isPlatformer()) {
            FLAlertLayer::create("AI Autoplayer", "Platformer levels aren't supported.", "OK")->show();
            return false;
        }
        if (m_level->m_twoPlayerMode) {
            FLAlertLayer::create("AI Autoplayer", "2-player levels aren't supported.", "OK")->show();
            return false;
        }
        if (std::string(m_level->m_levelString).empty()) {
            FLAlertLayer::create("AI Autoplayer", "Wait for the level to finish downloading first.", "OK")->show();
            return false;
        }
        return true;
    }

    void onAISolve(CCObject*) {
        if (!this->canRunAI()) return;
        Ref<AILevelInfoLayer> self = this;
        createQuickPopup(
            "AI Autoplayer",
            "The AI will play this level in <cg>practice mode</c> (so it never counts as your completion) "
            "until it beats it, then save the winning run to your PC.\n\n"
            "Hard levels can take a long time. Leave the level to stop it.",
            "Cancel", "Start",
            [self](FLAlertLayer*, bool start) {
                if (start) self->startAI(ai::Mode::Search);
            }
        );
    }

    void onAIReplay(CCObject*) {
        if (!this->canRunAI()) return;
        this->startAI(ai::Mode::Playback);
    }

    void startAI(ai::Mode mode) {
        auto& s = ai::state();
        s.pendingMode = mode;
        auto scene = PlayLayer::scene(m_level, false, false);
        s.pendingMode = ai::Mode::None; // consumed by PlayLayer::init (or cleared if it failed)
        if (!scene) return;
        CCDirector::sharedDirector()->replaceScene(CCTransitionFade::create(0.5f, scene));
    }
};
