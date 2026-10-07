#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>

#include "ClickTrack.hpp"
#include "ManiaOverlay.hpp"
#include "TrainerPopup.hpp"
#include "WorldMarkers.hpp"

#include <eclipse.eclipse-menu/include/modules.hpp>

using namespace geode::prelude;

// Show up in Eclipse's cheat indicator / auto safe mode (no-op if Eclipse isn't installed).
$on_game(Loaded) {
    eclipse::modules::registerCheat("Click Trainer", [] {
        auto& t = Trainer::get();
        return t.isActive() || t.capturing;
    });
}

// Hotkeys (set them in the mod's settings): on/off (Alt+C), switch view (Alt+V)
$execute {
    listenForAllSettingChanges([](std::string_view, std::shared_ptr<SettingV3>) {
        if (PlayLayer::get()) Trainer::get().reloadSettings();
    });
    listenForKeybindSettingPresses("toggle-key", [](Keybind const&, bool down, bool repeat, double) {
        if (!down || repeat) return false;
        Trainer::get().toggleEnabled();
        return true;
    });
    listenForKeybindSettingPresses("style-key", [](Keybind const&, bool down, bool repeat, double) {
        if (!down || repeat) return false;
        Trainer::get().cycleStyle();
        return true;
    });
}

class $modify(CTPlayLayer, PlayLayer) {
    struct Fields {
        // Set when we forced test mode on for a completion, so we can undo exactly that.
        bool forcedTestMode = false;
        bool testModeBefore = false;
        Ref<ClickTrack> track;
        Ref<ManiaOverlay> overlay;
        Ref<WorldMarkers> markers;
    };

    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;

        auto& trainer = Trainer::get();
        trainer.levelLength = m_levelLength;
        trainer.enterLevel(level);
        // Started from a start position? Line the chart up with it.
        trainer.alignToStart(this);
        trainer.rebuild();
        if (auto track = ClickTrack::create()) {
            this->addChild(track, 999);
            m_fields->track = track;
        }
        if (auto overlay = ManiaOverlay::create()) {
            this->addChild(overlay, 1000);
            m_fields->overlay = overlay;
        }
        // Markers live in the level itself so they scroll and zoom with it
        if (auto markers = WorldMarkers::create(); markers && m_objectLayer) {
            m_objectLayer->addChild(markers, 99999);
            m_fields->markers = markers;
        }
        return true;
    }

    // Redraw everything right after this frame's physics, in sync with your icon.
    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);
        if (m_fields->overlay) m_fields->overlay->update(dt);
        if (m_fields->markers) m_fields->markers->update(dt);
        if (m_fields->track) m_fields->track->update(dt);
    }

    void resetLevel() {
        if (m_fields->forcedTestMode) {
            m_isTestMode = m_fields->testModeBefore;
            m_fields->forcedTestMode = false;
        }
        PlayLayer::resetLevel();
        Trainer::get().onReset(m_gameState.m_currentProgress, this);
    }

    // Safe mode: GD doesn't save anything for test-mode runs (like a start pos),
    // so flag the attempt as test mode before completion / death is processed.
    void levelComplete() {
        auto& trainer = Trainer::get();
        bool cheated = trainer.cheatedThisAttempt;
        trainer.onComplete(m_level);
        if (cheated && !m_isTestMode) {
            m_fields->testModeBefore = m_isTestMode;
            m_fields->forcedTestMode = true;
            m_isTestMode = true;
        }
        PlayLayer::levelComplete();
    }

    void destroyPlayer(PlayerObject* player, GameObject* object) {
        auto& trainer = Trainer::get();
        if (trainer.autoplay && object != m_anticheatSpike) return;
        if (!trainer.cheatedThisAttempt) {
            PlayLayer::destroyPlayer(player, object);
        }
        else {
            bool old = m_isTestMode;
            m_isTestMode = true;
            PlayLayer::destroyPlayer(player, object);
            m_isTestMode = old;
        }
        if (player && player->m_isDead) trainer.onDeath(m_gameState.m_currentProgress);
    }

    void onQuit() {
        Trainer::get().exitLevel();
        PlayLayer::onQuit();
    }
};

class $modify(CTBaseGameLayer, GJBaseGameLayer) {
    bool isPlayLayer() {
        return PlayLayer::get() == static_cast<GJBaseGameLayer*>(this); // ignore the editor
    }

    void handleButton(bool down, int button, bool isPlayer1) {
        if (!isPlayLayer()) {
            GJBaseGameLayer::handleButton(down, button, isPlayer1);
            return;
        }
        auto& trainer = Trainer::get();
        // During the bot demo only the bot plays.
        if (trainer.shouldBlockPlayerInput()) return;

        GJBaseGameLayer::handleButton(down, button, isPlayer1);
        trainer.onInput(m_gameState.m_currentProgress, down, button, !isPlayer1);
    }

    // Bot inputs go in at the same point real inputs are handled,
    // so they land on the same tick they were recorded on.
    void processQueuedButtons(float dt, bool clearInputQueue) {
        if (isPlayLayer()) Trainer::get().injectInputs(this, m_gameState.m_currentProgress);
        GJBaseGameLayer::processQueuedButtons(dt, clearInputQueue);
    }

    void processCommands(float dt, bool isHalfTick, bool isLastTick) {
        GJBaseGameLayer::processCommands(dt, isHalfTick, isLastTick);
        if (!isPlayLayer()) return;
        auto& trainer = Trainer::get();
        // Safety net in case no queued-button pass ran this tick
        trainer.injectInputs(this, static_cast<int64_t>(m_gameState.m_currentProgress) - 1);
        trainer.onTick(m_gameState.m_currentProgress, static_cast<PlayLayer*>(static_cast<GJBaseGameLayer*>(this)));
    }
};

class $modify(CTPauseLayer, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();

        auto menu = this->getChildByID("right-button-menu");
        if (!menu) return;

        auto spr = ButtonSprite::create("Clicks", "bigFont.fnt", "GJ_button_04.png", .6f);
        spr->setScale(.55f);
        auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(CTPauseLayer::onClickTrainer));
        btn->setID("click-trainer-button"_spr);
        menu->addChild(btn);
        menu->updateLayout();
    }

    void onClickTrainer(CCObject*) {
        if (auto pl = PlayLayer::get()) {
            TrainerPopup::create(pl->m_level)->show();
        }
    }
};
