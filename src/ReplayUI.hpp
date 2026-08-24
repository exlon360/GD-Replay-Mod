#pragma once

#include "ReplayTypes.hpp"

#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>

class PlayLayer;

namespace geode {
class ScrollLayer;
class TextInput;
}

namespace replay_mod {

class ReplayHud final : public cocos2d::CCNode {
public:
    static ReplayHud* create(PlayLayer* layer);
    bool init(PlayLayer* layer);
    void update(float dt) override;
    void refresh();

private:
    void onOpen(cocos2d::CCObject* sender);

    PlayLayer* m_layer = nullptr;
    cocos2d::CCLabelBMFont* m_statusLabel = nullptr;
    std::string m_lastStatus;
};

class ReplayControlPopup final : public geode::Popup {
public:
    static ReplayControlPopup* create(PlayLayer* layer);

protected:
    bool init(PlayLayer* layer);
    void refresh();
    void onLatest(cocos2d::CCObject* sender);
    void onLibrary(cocos2d::CCObject* sender);
    void onPause(cocos2d::CCObject* sender);
    void onSpeed(cocos2d::CCObject* sender);
    void onHitboxes(cocos2d::CCObject* sender);
    void onStop(cocos2d::CCObject* sender);

private:
    PlayLayer* m_layer = nullptr;
    cocos2d::CCLabelBMFont* m_stateLabel = nullptr;
    ButtonSprite* m_pauseSprite = nullptr;
    ButtonSprite* m_speedSprite = nullptr;
    ButtonSprite* m_hitboxSprite = nullptr;
};

class ReplayLibraryPopup final : public geode::Popup {
public:
    static ReplayLibraryPopup* create(PlayLayer* layer);

protected:
    bool init(PlayLayer* layer);
    void rebuild(std::string const& query);
    void onWatch(cocos2d::CCObject* sender);

private:
    PlayLayer* m_layer = nullptr;
    geode::TextInput* m_searchInput = nullptr;
    geode::ScrollLayer* m_scroll = nullptr;
    cocos2d::CCLabelBMFont* m_countLabel = nullptr;
    std::vector<ReplayRun> m_allRuns;
    std::vector<std::size_t> m_visibleIndices;
};

} // namespace replay_mod
