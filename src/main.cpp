#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/ui/Notification.hpp>

#include "ModState.hpp"
#include "Overlay.hpp"
#include "Popups.hpp"

using namespace geode::prelude;
using namespace ffc;

$on_mod(Loaded) {
    ModState::get().syncFromGeodeSettings();

    listenForAllSettingChanges([](std::string_view, std::shared_ptr<SettingV3>) {
        ModState::get().syncFromGeodeSettings();
        if (auto* pl = PlayLayer::get()) {
            if (auto* ov = FFCOverlay::find(pl)) {
                ov->refreshCounterDisplay();
                ov->updateOverlay();
            }
        }
    });

    listenForKeybindSettingPresses("open-menu-keybind", [](Keybind const&, bool down, bool repeat, double) {
        if (!down || repeat) return;
        if (auto* pop = FFCPopup::create()) {
            pop->show();
        }
    });

    listenForKeybindSettingPresses("toggle-overlay-keybind", [](Keybind const&, bool down, bool repeat, double) {
        if (!down || repeat) return;
        ModState::get().toggleOverlayHiddenByKey();
        bool hidden = ModState::get().isOverlayHiddenByKey();
        if (auto* pl = PlayLayer::get()) {
            if (auto* ov = FFCOverlay::find(pl)) {
                ov->refreshCounterDisplay();
                ov->updateOverlay();
            }
        }
        Notification::create(
            hidden ? "FFC Overlay Hidden" : "FFC Overlay Visible",
            NotificationIcon::Info,
            0.6f
        )->show();
    });
}

class $modify(FFCBaseGameLayer, GJBaseGameLayer) {
    double getModifiedDelta(float dt) {
        auto& state = ModState::get();
        if (state.isScanningLevel(this) && state.isInScanTick()) {
            return 1.0 / state.macroPlayer().effectiveTps();
        }
        if (state.macroPlayer().isPlaying() && state.macroPlayer().lockDeltaToTps()) {
            if (auto* pl = typeinfo_cast<PlayLayer*>(this)) {
                if (!pl->m_playerDied && !pl->m_levelEndAnimationStarted) {
                    return 1.0 / state.macroPlayer().effectiveTps();
                }
            }
        }
        return GJBaseGameLayer::getModifiedDelta(dt);
    }

    void processCommands(float dt, bool isHalfTick, bool isLastTick) {
        GJBaseGameLayer::processCommands(dt, isHalfTick, isLastTick);

        auto* pl = typeinfo_cast<PlayLayer*>(this);
        if (!pl) return;

        auto& state = ModState::get();
        int64_t tick = static_cast<int64_t>(this->m_gameState.m_currentProgress);

        if (state.isScanningLevel(this)) {
            state.onScanProcessCommands(this, tick);
            return;
        }

        if (state.macroPlayer().isPlaying() && !pl->m_playerDied && !pl->m_levelEndAnimationStarted) {
            auto due = state.macroPlayer().collectDueInputs(tick);
            for (auto const& act : due) {
                state.setInjectingInput(true);
                this->handleButton(act.down, static_cast<int>(act.button), !act.player2);
                state.setInjectingInput(false);

                PlayerObject* p = (act.player2 && pl->m_player2) ? pl->m_player2 : pl->m_player1;
                state.processPlayerInput(pl, p, act.down, static_cast<int>(act.button), act.player2, act.macroIndex);
            }
        }
    }

    void handleButton(bool down, int button, bool isPlayer1) {
        GJBaseGameLayer::handleButton(down, button, isPlayer1);

        auto& state = ModState::get();
        if (state.isInjectingInput() || state.isScanningLevel(this) || state.macroPlayer().isPlaying()) {
            return;
        }

        if (auto* pl = typeinfo_cast<PlayLayer*>(this)) {
            bool isP2 = !isPlayer1;
            PlayerObject* p = (isP2 && pl->m_player2) ? pl->m_player2 : pl->m_player1;
            state.processPlayerInput(pl, p, down, button, isP2);
        }
    }
};

class $modify(FFCPlayLayer, PlayLayer) {
    void setupHasCompleted() {
        PlayLayer::setupHasCompleted();
        auto& state = ModState::get();
        state.syncFromGeodeSettings();
        state.resetAttemptCounts();
        FFCOverlay::ensure(this);
    }

    void postUpdate(float dt) {
        auto& state = ModState::get();
        if (state.isScanningLevel(this)) {
            state.runScanFrame(this, [this](float stepDt) {
                PlayLayer::postUpdate(stepDt);
            });
            if (auto* ov = FFCOverlay::ensure(this)) {
                ov->updateOverlay();
            }
            return;
        }

        PlayLayer::postUpdate(dt);
        if (auto* ov = FFCOverlay::ensure(this)) {
            ov->updateOverlay();
        }
    }

    void resetLevel() {
        PlayLayer::resetLevel();

        auto& state = ModState::get();
        if (state.isScanningLevel(this)) {
            if (!state.isSelfResetting()) {
                state.stopExperimentalRealScan(this);
            }
            return;
        }

        state.resetAttemptCounts();
        bool autoReplay = Mod::get()->getSettingValue<bool>("macro-auto-replay");
        state.macroPlayer().onLevelReset(autoReplay);

        if (auto* ov = FFCOverlay::find(this)) {
            ov->onReset();
        }
    }

    void destroyPlayer(PlayerObject* player, GameObject* object) {
        auto& state = ModState::get();
        if (state.isScanningLevel(this)) {
            if (object != this->m_anticheatSpike) {
                state.markScanPlayerDead();
            }
            return;
        }
        PlayLayer::destroyPlayer(player, object);
    }

    void levelComplete() {
        auto& state = ModState::get();
        if (state.isScanningLevel(this)) {
            state.markScanLevelComplete();
            return;
        }
        PlayLayer::levelComplete();
    }

    void onQuit() {
        auto& state = ModState::get();
        state.stopExperimentalRealScan(this);
        state.stopMacroPlaybackInLevel(this);
        PlayLayer::onQuit();
    }
};

class $modify(FFCPauseLayer, PauseLayer) {
    void onOpenFFCPopup(CCObject*) {
        if (auto* pop = FFCPopup::create()) {
            pop->show();
        }
    }

    void customSetup() {
        PauseLayer::customSetup();

        auto* spr = ButtonSprite::create("FFC", "goldFont.fnt", "GJ_button_01.png", 0.8f);
        spr->setScale(0.62f);
        auto* btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(FFCPauseLayer::onOpenFFCPopup));
        btn->setID("nananqj96.fake-frame-counter/pause-button");

        if (auto* rightMenu = this->getChildByID("right-button-menu")) {
            rightMenu->addChild(btn);
            rightMenu->updateLayout();
        } else {
            auto winSize = CCDirector::sharedDirector()->getWinSize();
            auto* fallbackMenu = CCMenu::create();
            fallbackMenu->setPosition({0.0f, 0.0f});
            btn->setPosition({winSize.width - 42.0f, 92.0f});
            fallbackMenu->addChild(btn);
            this->addChild(fallbackMenu, 10);
        }
    }
};
