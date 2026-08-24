#include "ReplayUI.hpp"

#include "ReplaySession.hpp"
#include "ReplayStore.hpp"

#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/GJGameLevel.hpp>
#include <Geode/binding/PauseLayer.hpp>
#include <Geode/binding/PlayLayer.hpp>
#include <Geode/ui/Notification.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <Geode/ui/TextInput.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>

using namespace geode::prelude;

namespace replay_mod {
namespace {

constexpr cocos2d::ccColor3B kPurple { 181, 76, 255 };
constexpr cocos2d::ccColor3B kDeepPurple { 54, 19, 83 };
ReplayPlaybackOverlay* g_activeOverlay = nullptr;

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

CCMenuItemSpriteExtra* addButton(
    CCMenu* menu,
    CCObject* target,
    SEL_MenuHandler callback,
    char const* text,
    CCPoint position,
    int width = 105,
    ButtonSprite** outputSprite = nullptr,
    float scale = .72f
) {
    auto sprite = ButtonSprite::create(text, width, 0, .65f, true);
    sprite->setScale(scale);
    sprite->setColor(kPurple);
    auto button = CCMenuItemSpriteExtra::create(sprite, target, callback);
    button->setPosition(position);
    menu->addChild(button);
    if (outputSprite) *outputSprite = sprite;
    return button;
}

std::string runLabel(ReplayRun const& run) {
    return fmt::format(
        "{:.1f}%  |  {} inputs{}",
        run.progress,
        run.storedEventCount,
        run.completed ? "  |  COMPLETE" : ""
    );
}

CCSprite* replayIcon(float targetSize) {
    auto const iconPath = Mod::get()->getResourcesDir() / "replay-icon.png";
    auto icon = CCSprite::create(iconPath.string().c_str());
    if (!icon) icon = CCSprite::createWithSpriteFrameName("GJ_playBtn2_001.png");
    auto const longestSide = std::max(icon->getContentWidth(), icon->getContentHeight());
    if (longestSide > 0.0f) icon->setScale(targetSize / longestSide);
    return icon;
}

} // namespace

CCMenuItemSpriteExtra* createReplayEntryButton(CCObject* target, SEL_MenuHandler callback) {
    auto button = CCMenuItemSpriteExtra::create(replayIcon(46.0f), target, callback);
    button->setID("xviongd.replay-mod/replay-button");
    return button;
}

void placeReplayEntryButton(
    CCMenu* menu,
    CCMenuItemSpriteExtra* replayButton,
    CCSprite* knownPlaySprite
) {
    if (!menu || !replayButton) return;

    CCPoint playPosition { 0.0f, -72.0f };
    bool foundPlayButton = false;
    if (auto playButton = menu->getChildByID("play-button")) {
        playPosition = playButton->getPosition();
        foundPlayButton = true;
    }

    float largestArea = 0.0f;
    for (auto object : menu->getChildrenExt()) {
        auto item = typeinfo_cast<CCMenuItemSpriteExtra*>(object);
        if (!item || item == replayButton) continue;
        if (knownPlaySprite &&
            (item->getNormalImage() == knownPlaySprite || knownPlaySprite->getParent() == item)) {
            playPosition = item->getPosition();
            foundPlayButton = true;
            break;
        }
        if (foundPlayButton) continue;
        auto const size = item->getContentSize();
        auto const area = size.width * size.height * item->getScaleX() * item->getScaleY();
        if (area > largestArea) {
            largestArea = area;
            playPosition = item->getPosition();
        }
    }

    replayButton->setPosition({ playPosition.x + 62.0f, playPosition.y });
    menu->addChild(replayButton, 20);
}

ReplayViewerPopup* ReplayViewerPopup::create(
    GJGameLevel* level,
    ReplayLaunchCallback launchReplay
) {
    auto result = new ReplayViewerPopup();
    if (result->init(level, std::move(launchReplay))) {
        result->autorelease();
        return result;
    }
    delete result;
    return nullptr;
}

bool ReplayViewerPopup::init(GJGameLevel* level, ReplayLaunchCallback launchReplay) {
    if (!level || !Popup::init(440.0f, 286.0f)) return false;
    m_level = level;
    m_launchReplay = std::move(launchReplay);
    this->setTitle("Replay Viewer");

    for (auto const& summary : ReplayStore::listSummaries()) {
        if (ReplaySession::get().isCompatible(level, summary)) {
            m_latest = summary;
            break;
        }
    }

    auto levelName = CCLabelBMFont::create(level->m_levelName.c_str(), "bigFont.fnt");
    levelName->setScale(.42f);
    levelName->limitLabelWidth(280.0f, .42f, .2f);
    m_mainLayer->addChildAtPosition(levelName, Anchor::Top, { 0.0f, -48.0f });

    auto byline = CCLabelBMFont::create("xVionGD REPLAY ARCHIVE", "chatFont.fnt");
    byline->setScale(.48f);
    byline->setColor(kPurple);
    m_mainLayer->addChildAtPosition(byline, Anchor::Top, { 0.0f, -68.0f });

    auto panel = cocos2d::extension::CCScale9Sprite::create("square02_001.png");
    panel->setContentSize({ 414.0f, 150.0f });
    panel->setColor(kDeepPurple);
    panel->setOpacity(205);
    m_mainLayer->addChildAtPosition(panel, Anchor::Center, { 0.0f, -4.0f });

    auto latestButton = CCMenuItemSpriteExtra::create(
        replayIcon(88.0f),
        this,
        menu_selector(ReplayViewerPopup::onLatest)
    );
    latestButton->setPosition({ 220.0f, 148.0f });
    latestButton->setEnabled(m_latest.has_value());
    if (!m_latest) latestButton->setOpacity(90);
    m_buttonMenu->addChild(latestButton);

    auto watchLabel = CCLabelBMFont::create(
        m_latest ? "WATCH LATEST" : "NO ATTEMPTS YET",
        "bigFont.fnt"
    );
    watchLabel->setScale(.36f);
    watchLabel->setColor(m_latest ? kPurple : ccColor3B { 170, 170, 170 });
    m_mainLayer->addChildAtPosition(watchLabel, Anchor::Center, { 0.0f, -54.0f });

    auto latestDetail = CCLabelBMFont::create(
        m_latest
            ? fmt::format("Attempt {}\n{:.1f}%\n{} inputs", m_latest->attempt,
                m_latest->progress, m_latest->storedEventCount).c_str()
            : "Play the level once\nto record an attempt.",
        "chatFont.fnt"
    );
    latestDetail->setAlignment(kCCTextAlignmentCenter);
    latestDetail->setScale(.56f);
    latestDetail->setOpacity(220);
    m_mainLayer->addChildAtPosition(latestDetail, Anchor::Center, { 128.0f, -1.0f });

    addButton(
        m_buttonMenu,
        this,
        menu_selector(ReplayViewerPopup::onLibrary),
        "Search Attempts",
        { 90.0f, 154.0f },
        138
    );
    addButton(
        m_buttonMenu,
        this,
        menu_selector(ReplayViewerPopup::onInfo),
        "Info",
        { 90.0f, 116.0f },
        74
    );

    auto hint = CCLabelBMFont::create(
        "Select a saved run here, before entering the level.",
        "chatFont.fnt"
    );
    hint->setScale(.45f);
    hint->setOpacity(185);
    m_mainLayer->addChildAtPosition(hint, Anchor::Bottom, { 0.0f, 24.0f });
    return true;
}

bool ReplayViewerPopup::queueAndLaunch(ReplayRun const& summary) {
    auto run = ReplayStore::load(summary.sourcePath);
    if (!run) {
        FLAlertLayer::create("Replay Viewer", "That replay file is damaged or incomplete.", "OK")->show();
        return false;
    }

    std::string error;
    if (!ReplaySession::get().queueRun(m_level, std::move(*run), error)) {
        FLAlertLayer::create("Replay Viewer", error, "OK")->show();
        return false;
    }

    auto launch = m_launchReplay;
    this->onClose(nullptr);
    if (launch) launch();
    return true;
}

void ReplayViewerPopup::onLatest(CCObject*) {
    if (m_latest) queueAndLaunch(*m_latest);
}

void ReplayViewerPopup::onLibrary(CCObject*) {
    if (auto popup = ReplayLibraryPopup::create(m_level, m_launchReplay)) popup->show();
}

void ReplayViewerPopup::onInfo(CCObject*) {
    FLAlertLayer::create(
        "Replay Viewer",
        "This screen opens <cp>before Play</c>. Choose an attempt, then watch it in a protected replay-only session. A death freezes on its final frame.",
        "OK"
    )->show();
}

ReplayLibraryPopup* ReplayLibraryPopup::create(
    GJGameLevel* level,
    ReplayLaunchCallback launchReplay
) {
    auto result = new ReplayLibraryPopup();
    if (result->init(level, std::move(launchReplay))) {
        result->autorelease();
        return result;
    }
    delete result;
    return nullptr;
}

bool ReplayLibraryPopup::init(GJGameLevel* level, ReplayLaunchCallback launchReplay) {
    if (!level || !Popup::init(440.0f, 286.0f)) return false;
    m_level = level;
    m_launchReplay = std::move(launchReplay);
    this->setTitle("Search Attempts");

    for (auto const& summary : ReplayStore::listSummaries()) {
        if (ReplaySession::get().isCompatible(level, summary)) {
            m_allRuns.push_back(summary);
        }
    }

    auto levelLabel = CCLabelBMFont::create(level->m_levelName.c_str(), "chatFont.fnt");
    levelLabel->setScale(.5f);
    levelLabel->setColor(kPurple);
    levelLabel->limitLabelWidth(260.0f, .5f, .25f);
    m_mainLayer->addChildAtPosition(levelLabel, Anchor::Top, { 0.0f, -45.0f });

    m_searchInput = TextInput::create(320.0f, "Search attempt, percent, or complete...");
    m_searchInput->setScale(.7f);
    m_searchInput->setCommonFilter(CommonFilter::Any);
    m_searchInput->setTextAlign(TextInputAlign::Left);
    m_searchInput->setCallback([this](std::string const& value) {
        rebuild(value);
    });
    m_mainLayer->addChildAtPosition(m_searchInput, Anchor::Top, { 0.0f, -72.0f });

    m_scroll = ScrollLayer::create({ 384.0f, 150.0f });
    m_scroll->setStealingTouches(true);
    m_mainLayer->addChildAtPosition(
        m_scroll,
        Anchor::BottomLeft,
        { 28.0f, 43.0f },
        { 0.0f, 0.0f }
    );

    m_countLabel = CCLabelBMFont::create("0 attempts", "chatFont.fnt");
    m_countLabel->setScale(.5f);
    m_countLabel->setOpacity(190);
    m_mainLayer->addChildAtPosition(m_countLabel, Anchor::Bottom, { 0.0f, 23.0f });

    rebuild("");
    return true;
}

void ReplayLibraryPopup::rebuild(std::string const& query) {
    auto const normalized = lower(query);
    m_visibleIndices.clear();

    for (std::size_t index = 0; index < m_allRuns.size(); ++index) {
        auto const& run = m_allRuns[index];
        auto const searchable = lower(fmt::format(
            "attempt {} {:.1f}% {} inputs {}",
            run.attempt,
            run.progress,
            run.storedEventCount,
            run.completed ? "complete" : "death"
        ));
        if (normalized.empty() || searchable.find(normalized) != std::string::npos) {
            m_visibleIndices.push_back(index);
        }
    }

    m_scroll->m_contentLayer->removeAllChildren();
    auto const rowHeight = 40.0f;
    auto const contentHeight = std::max(
        m_scroll->getContentHeight(),
        rowHeight * static_cast<float>(m_visibleIndices.size())
    );
    m_scroll->m_contentLayer->setContentSize({ m_scroll->getContentWidth(), contentHeight });

    for (std::size_t index = 0; index < m_visibleIndices.size(); ++index) {
        auto const& run = m_allRuns[m_visibleIndices[index]];
        auto row = CCLayerColor::create(
            { 63, 24, 91, 190 },
            m_scroll->getContentWidth() - 4.0f,
            36.0f
        );
        row->setPosition({ 2.0f, contentHeight - rowHeight * (index + 1) + 2.0f });
        m_scroll->m_contentLayer->addChild(row);

        auto attemptLabel = CCLabelBMFont::create(
            fmt::format("Attempt {}", run.attempt).c_str(),
            "bigFont.fnt"
        );
        attemptLabel->setAnchorPoint({ 0.0f, 0.5f });
        attemptLabel->setScale(.34f);
        attemptLabel->setPosition({ 9.0f, 25.0f });
        row->addChild(attemptLabel);

        auto detailLabel = CCLabelBMFont::create(runLabel(run).c_str(), "chatFont.fnt");
        detailLabel->setAnchorPoint({ 0.0f, 0.5f });
        detailLabel->setScale(.4f);
        detailLabel->setOpacity(215);
        detailLabel->setPosition({ 9.0f, 11.0f });
        row->addChild(detailLabel);

        auto menu = CCMenu::create();
        menu->ignoreAnchorPointForPosition(false);
        menu->setAnchorPoint({ 0.0f, 0.0f });
        menu->setPosition({ 0.0f, 0.0f });
        menu->setContentSize(row->getContentSize());
        row->addChild(menu);

        auto watchSprite = ButtonSprite::create("Watch", 78, 0, .55f, true);
        watchSprite->setScale(.64f);
        watchSprite->setColor(kPurple);
        auto watchButton = CCMenuItemSpriteExtra::create(
            watchSprite,
            this,
            menu_selector(ReplayLibraryPopup::onWatch)
        );
        watchButton->setTag(static_cast<int>(index));
        watchButton->setPosition({ row->getContentWidth() - 43.0f, 18.0f });
        menu->addChild(watchButton);
    }

    m_scroll->scrollToTop();
    m_countLabel->setString(fmt::format(
        "{} attempt{}",
        m_visibleIndices.size(),
        m_visibleIndices.size() == 1 ? "" : "s"
    ).c_str());
}

bool ReplayLibraryPopup::queueAndLaunch(ReplayRun const& summary) {
    auto run = ReplayStore::load(summary.sourcePath);
    if (!run) {
        FLAlertLayer::create("Replay Viewer", "That replay file is damaged or incomplete.", "OK")->show();
        return false;
    }

    std::string error;
    if (!ReplaySession::get().queueRun(m_level, std::move(*run), error)) {
        FLAlertLayer::create("Replay Viewer", error, "OK")->show();
        return false;
    }

    auto launch = m_launchReplay;
    this->onClose(nullptr);
    if (launch) launch();
    return true;
}

void ReplayLibraryPopup::onWatch(CCObject* sender) {
    auto const index = static_cast<std::size_t>(sender->getTag());
    if (index >= m_visibleIndices.size()) return;
    queueAndLaunch(m_allRuns[m_visibleIndices[index]]);
}

ReplayPlaybackOverlay* ReplayPlaybackOverlay::create(PlayLayer* layer) {
    auto result = new ReplayPlaybackOverlay();
    if (result->init(layer)) {
        result->autorelease();
        return result;
    }
    delete result;
    return nullptr;
}

bool ReplayPlaybackOverlay::init(PlayLayer* layer) {
    if (!layer || !CCNode::init()) return false;
    m_layer = layer;
    g_activeOverlay = this;
    this->setID("xviongd.replay-mod/replay-viewer-overlay");

    auto const size = CCDirector::get()->getWinSize();
    this->setContentSize(size);
    this->setAnchorPoint({ 0.0f, 0.0f });

    auto topBar = CCLayerColor::create({ 24, 7, 38, 220 }, size.width, 44.0f);
    topBar->setPosition({ 0.0f, size.height - 44.0f });
    this->addChild(topBar);

    m_stateLabel = CCLabelBMFont::create("REPLAY", "bigFont.fnt");
    m_stateLabel->setAnchorPoint({ 0.0f, 0.5f });
    m_stateLabel->setScale(.38f);
    m_stateLabel->setColor(kPurple);
    m_stateLabel->setPosition({ 12.0f, size.height - 16.0f });
    this->addChild(m_stateLabel);

    auto byline = CCLabelBMFont::create("xVionGD", "chatFont.fnt");
    byline->setAnchorPoint({ 0.0f, 0.5f });
    byline->setScale(.45f);
    byline->setOpacity(190);
    byline->setPosition({ 13.0f, size.height - 32.0f });
    this->addChild(byline);

    m_speedLabel = CCLabelBMFont::create("1x", "bigFont.fnt");
    m_speedLabel->setScale(.34f);
    m_speedLabel->setColor(kPurple);
    m_speedLabel->setPosition({ size.width - 119.0f, size.height - 22.0f });
    this->addChild(m_speedLabel);

    m_freezeLabel = CCLabelBMFont::create("DEATH PAUSED", "bigFont.fnt");
    m_freezeLabel->setScale(.7f);
    m_freezeLabel->setColor(kPurple);
    m_freezeLabel->setPosition({ size.width / 2.0f, size.height / 2.0f + 48.0f });
    this->addChild(m_freezeLabel, 5);

    auto menu = CCMenu::create();
    menu->ignoreAnchorPointForPosition(false);
    menu->setAnchorPoint({ 0.0f, 0.0f });
    menu->setPosition({ 0.0f, 0.0f });
    menu->setContentSize(size);
    this->addChild(menu, 10);

    addButton(menu, this, menu_selector(ReplayPlaybackOverlay::onSlow), "Slow", { size.width - 193.0f, size.height - 22.0f }, 70, nullptr, .62f);
    addButton(menu, this, menu_selector(ReplayPlaybackOverlay::onPause), "Pause", { size.width - 71.0f, size.height - 22.0f }, 76, &m_pauseSprite, .62f);
    addButton(menu, this, menu_selector(ReplayPlaybackOverlay::onFast), "Speed", { size.width - 34.0f, size.height - 22.0f }, 76, nullptr, .62f);
    addButton(menu, this, menu_selector(ReplayPlaybackOverlay::onBack), "Exit Replay", { 58.0f, 24.0f }, 105, nullptr, .66f);
    addButton(menu, this, menu_selector(ReplayPlaybackOverlay::onRestart), "Restart", { size.width / 2.0f, 24.0f }, 88, nullptr, .66f);
    addButton(menu, this, menu_selector(ReplayPlaybackOverlay::onHitboxes), "Hitboxes Off", { size.width - 68.0f, 24.0f }, 112, &m_hitboxSprite, .66f);

    refresh();
    return true;
}

void ReplayPlaybackOverlay::onExit() {
    if (g_activeOverlay == this) g_activeOverlay = nullptr;
    CCNode::onExit();
}

void ReplayPlaybackOverlay::refreshActive() {
    if (g_activeOverlay) g_activeOverlay->refresh();
}

void ReplayPlaybackOverlay::refresh() {
    auto& session = ReplaySession::get();
    auto const mode = session.mode();
    m_stateLabel->setString(session.statusText().c_str());
    m_speedLabel->setString(fmt::format("{:.2g}x", session.speed()).c_str());
    m_pauseSprite->setString(
        mode == SessionMode::Paused
            ? "Resume"
            : (mode == SessionMode::DeathPaused || mode == SessionMode::Finished)
                ? "Frozen"
                : "Pause"
    );
    m_hitboxSprite->setString(
        m_layer && m_layer->m_isDebugDrawEnabled ? "Hitboxes On" : "Hitboxes Off"
    );

    auto const isFrozen = mode == SessionMode::DeathPaused || mode == SessionMode::Finished;
    m_freezeLabel->setVisible(isFrozen);
    if (isFrozen) {
        m_freezeLabel->setString(
            mode == SessionMode::DeathPaused ? "DEATH PAUSED" : "REPLAY COMPLETE"
        );
    }
}

void ReplayPlaybackOverlay::onPause(CCObject*) {
    auto& session = ReplaySession::get();
    if (session.mode() == SessionMode::Paused) {
        session.togglePause();
        if (auto pauseLayer = typeinfo_cast<PauseLayer*>(this->getParent())) {
            pauseLayer->onResume(nullptr);
        }
    }
    else if (!session.togglePause()) {
        Notification::create("Restart to play this replay again", NotificationIcon::Info, 2.0f)->show();
    }
    refresh();
}

void ReplayPlaybackOverlay::onSlow(CCObject*) {
    ReplaySession::get().adjustSpeed(-1);
    refresh();
}

void ReplayPlaybackOverlay::onFast(CCObject*) {
    ReplaySession::get().adjustSpeed(1);
    refresh();
}

void ReplayPlaybackOverlay::onRestart(CCObject*) {
    std::string error;
    if (!ReplaySession::get().restartPlayback(m_layer, error)) {
        FLAlertLayer::create("Replay Viewer", error, "OK")->show();
        refresh();
        return;
    }

    auto pauseLayer = typeinfo_cast<PauseLayer*>(this->getParent());
    Ref<ReplayPlaybackOverlay> keepAlive(this);
    this->removeFromParent();
    if (pauseLayer) {
        pauseLayer->onResume(nullptr);
    }
}

void ReplayPlaybackOverlay::onHitboxes(CCObject*) {
    if (m_layer) {
        m_layer->toggleDebugDraw();
        m_layer->updateDebugDraw();
    }
    refresh();
}

void ReplayPlaybackOverlay::onBack(CCObject*) {
    if (m_layer) m_layer->onQuit();
}

} // namespace replay_mod
