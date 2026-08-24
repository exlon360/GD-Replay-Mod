#include "ReplaySession.hpp"
#include "ReplayUI.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/UILayer.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/ui/Notification.hpp>

using namespace geode::prelude;
using namespace replay_mod;

class $modify(ReplayBaseGameLayer, GJBaseGameLayer) {
    void handleButton(bool down, int button, bool isPlayer1) {
        if (ReplaySession::get().shouldForwardInput(this, down, button, isPlayer1)) {
            GJBaseGameLayer::handleButton(down, button, isPlayer1);
        }
    }

    void processCommands(float dt, bool isHalfTick, bool isLastTick) {
        auto const shouldPause = ReplaySession::get().beforeProcessCommands(this);
        GJBaseGameLayer::processCommands(dt, isHalfTick, isLastTick);
        if (shouldPause) {
            if (auto layer = PlayLayer::get();
                layer && static_cast<GJBaseGameLayer*>(layer) ==
                    static_cast<GJBaseGameLayer*>(this)) {
                layer->pauseGame(false);
            }
        }
    }
};

class $modify(ReplayPlayLayer, PlayLayer) {
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;

        ReplaySession::get().onLayerInit(this);
        if (Mod::get()->getSettingValue<bool>("show-hud")) {
            if (auto hud = ReplayHud::create(this)) {
                CCNode* parent = m_uiLayer
                    ? static_cast<CCNode*>(m_uiLayer)
                    : static_cast<CCNode*>(this);
                parent->addChild(hud, 1000);
            }
        }
        return true;
    }

    void resetLevel() {
        ReplaySession::get().onLayerWillReset(this);
        PlayLayer::resetLevel();
        ReplaySession::get().onLayerDidReset(this);
    }

    void resetLevelFromStart() {
        ReplaySession::get().onLayerWillReset(this);
        PlayLayer::resetLevelFromStart();
        ReplaySession::get().onLayerDidReset(this);
    }

    void destroyPlayer(PlayerObject* player, GameObject* object) {
        auto& session = ReplaySession::get();
        if (session.isPlaybackSession()) {
            PlayLayer::destroyPlayer(player, object);
            session.onPlaybackDeath(this);
            Notification::create("Replay ended early", NotificationIcon::Warning, 2.5f)->show();
            return;
        }

        session.onRecordingDeath(this);
        PlayLayer::destroyPlayer(player, object);
    }

    void levelComplete() {
        auto& session = ReplaySession::get();
        if (session.isPlaybackSession()) {
            session.onPlaybackComplete(this);
            Notification::create(
                "Replay complete - progress was not saved",
                NotificationIcon::Success,
                3.0f
            )->show();
            this->pauseGame(false);
            return;
        }

        session.onRecordingComplete(this);
        PlayLayer::levelComplete();
    }

    void onQuit() {
        ReplaySession::get().onLayerWillExit(this);
        PlayLayer::onQuit();
        ReplaySession::get().onLayerDidExit(this);
    }

    void onExit() {
        ReplaySession::get().onLayerWillExit(this);
        PlayLayer::onExit();
        ReplaySession::get().onLayerDidExit(this);
    }
};
