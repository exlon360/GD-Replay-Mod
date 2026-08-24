#include "ReplaySession.hpp"

#include "ReplayStore.hpp"

#include <Geode/binding/GJBaseGameLayer.hpp>
#include <Geode/binding/GJGameLevel.hpp>
#include <Geode/binding/PlayLayer.hpp>
#include <Geode/ui/Notification.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <iterator>
#include <string_view>

using namespace geode::prelude;

namespace replay_mod {

ReplaySession& ReplaySession::get() {
    static ReplaySession instance;
    return instance;
}

std::uint64_t ReplaySession::levelFingerprint(GJGameLevel* level) {
    if (!level) return 0;

    // Stable FNV-1a fingerprint. This is an identity check, not a security hash.
    std::uint64_t hash = 14695981039346656037ull;
    auto feed = [&hash](std::string_view value) {
        for (unsigned char byte : value) {
            hash ^= byte;
            hash *= 1099511628211ull;
        }
        hash ^= 0xff;
        hash *= 1099511628211ull;
    };

    feed({ level->m_levelString.c_str(), level->m_levelString.size() });
    feed({ level->m_levelName.c_str(), level->m_levelName.size() });
    feed(fmt::format("{}", static_cast<int>(level->m_levelID)));
    feed(fmt::format("{}", level->m_levelVersion));
    feed(fmt::format("{}", level->m_gameVersion));
    feed(level->isPlatformer() ? "1" : "0");
    return hash;
}

std::uint64_t ReplaySession::levelFingerprint(PlayLayer* layer) {
    return layer ? levelFingerprint(layer->m_level) : 0;
}

std::uint64_t ReplaySession::fingerprintFor(PlayLayer* layer) const {
    return layer == m_layer ? m_levelFingerprint : levelFingerprint(layer);
}

std::int64_t ReplaySession::nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

bool ReplaySession::onLayerInit(PlayLayer* layer) {
    if (m_layer) restorePlaybackEnvironment(m_layer);

    if (!layer || !layer->m_level) {
        m_layer = nullptr;
        cancelQueuedPlayback();
        return false;
    }

    auto const fingerprint = levelFingerprint(layer);
    auto const queuedForLayer = m_queuedRun &&
        m_queuedRun->levelID == static_cast<int>(layer->m_level->m_levelID) &&
        m_queuedRun->levelHash != 0 &&
        m_queuedRun->levelHash == fingerprint &&
        m_queuedRun->platformer == layer->m_isPlatformer;
    auto const saveWasPrepared = queuedForLayer && m_hasDontSaveOverride &&
        m_overriddenLevel == layer->m_level;

    if (!queuedForLayer) {
        m_queuedRun.reset();
        restoreLevelSaveFlag();
    }
    else if (!saveWasPrepared && m_hasDontSaveOverride) {
        restoreLevelSaveFlag();
    }

    m_layer = layer;
    m_mode = SessionMode::Idle;
    m_pendingRun.reset();
    m_playbackIndex = 0;
    m_injecting = false;
    m_speed = 1.0f;
    m_recordingLimitNotified = false;
    m_resetDepth = 0;
    m_exitPrepared = false;
    m_levelFingerprint = fingerprint;
    m_hasPracticeOverride = false;
    if (!saveWasPrepared) {
        m_hasDontSaveOverride = false;
        m_overriddenLevel = nullptr;
    }
    m_hasTimeScaleOverride = false;
    for (auto& held : m_heldButtons) {
        std::fill(std::begin(held), std::end(held), false);
    }

    if (!queuedForLayer && Mod::get()->getSettingValue<bool>("auto-record") &&
        !layer->m_isPracticeMode) {
        beginRecording(layer);
    }
    return queuedForLayer;
}

void ReplaySession::beginRecording(PlayLayer* layer) {
    if (!layer || !layer->m_level || layer->m_isPracticeMode) {
        m_mode = SessionMode::Idle;
        return;
    }

    m_currentRun = {};
    m_currentRun.levelID = static_cast<int>(layer->m_level->m_levelID);
    m_currentRun.levelHash = fingerprintFor(layer);
    m_currentRun.levelName = std::string(layer->m_level->m_levelName);
    m_currentRun.attempt = std::max(1, layer->m_attempts);
    m_currentRun.createdAtMs = nowMs();
    m_currentRun.platformer = layer->m_isPlatformer;
    m_currentRun.practice = false;
    m_currentRun.randomSeed = layer->m_randomSeed;
    m_currentRun.replayRandomSeed = layer->m_replayRandSeed;
    m_startStep = layer->m_currentStep;
    m_startTimestamp = layer->m_timestamp;
    m_lastRecordedStep = -1;
    m_sameStepOrder = 0;
    m_recordingLimitNotified = false;
    m_mode = SessionMode::Recording;
}

void ReplaySession::onLayerWillReset(PlayLayer* layer) {
    if (!layer || layer != m_layer) return;
    ++m_resetDepth;
    if (m_resetDepth != 1) return;

    // Manual restarts count as attempts too. Death/completion hooks already
    // finalize first, so this only catches the remaining reset paths.
    if (m_mode == SessionMode::Recording) {
        finalizeRecording(layer, false);
    }

    if (m_mode == SessionMode::ArmedPlayback && m_pendingRun) {
        m_currentRun = std::move(*m_pendingRun);
        m_pendingRun.reset();
        layer->m_isPracticeMode = true;
        if (layer->m_level) layer->m_level->m_dontSave = true;
        layer->m_randomSeed = m_currentRun.randomSeed;
        layer->m_replayRandSeed = m_currentRun.replayRandomSeed;
        return;
    }

    // A death/reset may be triggered inside the game's original playback
    // lifecycle hook. Keep the protected playback environment alive until
    // the matching death handler has finished.
    if (m_mode == SessionMode::Playback || m_mode == SessionMode::Paused) {
        layer->m_isPracticeMode = true;
        if (layer->m_level) layer->m_level->m_dontSave = true;
        layer->m_randomSeed = m_currentRun.randomSeed;
        layer->m_replayRandSeed = m_currentRun.replayRandomSeed;
        return;
    }

    // Finished playback remains protected while vanilla performs the reset.
    if (m_mode == SessionMode::DeathPaused || m_mode == SessionMode::Finished) {
        layer->m_isPracticeMode = true;
        if (layer->m_level) layer->m_level->m_dontSave = true;
    }
}

void ReplaySession::onLayerDidReset(PlayLayer* layer) {
    if (!layer || layer != m_layer || m_resetDepth <= 0) return;
    --m_resetDepth;
    if (m_resetDepth != 0) return;

    if (m_mode == SessionMode::ArmedPlayback) {
        m_playbackIndex = 0;
        m_startStep = layer->m_currentStep;
        m_startTimestamp = layer->m_timestamp;
        layer->m_isPracticeMode = true;
        if (layer->m_level) layer->m_level->m_dontSave = true;
        for (auto& held : m_heldButtons) {
            std::fill(std::begin(held), std::end(held), false);
        }
        m_mode = SessionMode::Playback;
        return;
    }

    if (m_mode == SessionMode::Playback || m_mode == SessionMode::Paused) {
        m_playbackIndex = 0;
        m_startStep = layer->m_currentStep;
        m_startTimestamp = layer->m_timestamp;
        layer->m_isPracticeMode = true;
        if (layer->m_level) layer->m_level->m_dontSave = true;
        for (auto& held : m_heldButtons) {
            std::fill(std::begin(held), std::end(held), false);
        }
        return;
    }

    if (m_mode == SessionMode::DeathPaused || m_mode == SessionMode::Finished) {
        restorePlaybackEnvironment(layer);
        m_mode = SessionMode::Idle;
    }

    if (Mod::get()->getSettingValue<bool>("auto-record") && !layer->m_isPracticeMode) {
        beginRecording(layer);
    }
    else {
        m_mode = SessionMode::Idle;
    }
}

void ReplaySession::finalizeRecording(PlayLayer* layer, bool completed) {
    if (!layer || m_mode != SessionMode::Recording) return;

    m_currentRun.durationSteps = std::max<std::int64_t>(0, layer->m_currentStep - m_startStep);
    m_currentRun.progress = completed ? 100.0 : static_cast<double>(layer->getCurrentPercent());
    m_currentRun.completed = completed;
    m_currentRun.storedEventCount = m_currentRun.events.size();
    m_mode = SessionMode::Idle;

    if (m_currentRun.durationSteps <= 0 && m_currentRun.events.empty()) return;
    if (m_currentRun.durationSteps > kMaximumReplayDurationSteps) {
        Notification::create("Replay was too long to save", NotificationIcon::Warning, 2.5f)->show();
        return;
    }

    auto result = ReplayStore::save(m_currentRun);
    if (!result.ok) {
        log::warn("Replay save failed: {}", result.error);
        Notification::create("Replay save failed", NotificationIcon::Error, 2.5f)->show();
    }
}

void ReplaySession::onRecordingDeath(PlayLayer* layer) {
    finalizeRecording(layer, false);
}

void ReplaySession::onRecordingComplete(PlayLayer* layer) {
    finalizeRecording(layer, true);
}

bool ReplaySession::shouldForwardInput(
    GJBaseGameLayer* layer,
    bool down,
    int button,
    bool player1
) {
    if (!layer || layer != m_layer || layer != PlayLayer::get()) return true;
    if (button < 1 || button > 3) return true;

    if (isPlaybackSession() && !m_injecting) {
        return false;
    }

    if (m_mode == SessionMode::Recording && !m_injecting) {
        if (m_currentRun.events.size() >= kMaximumReplayEvents) {
            m_mode = SessionMode::Idle;
            if (!m_recordingLimitNotified) {
                m_recordingLimitNotified = true;
                Notification::create(
                    "Replay recording stopped: input limit reached",
                    NotificationIcon::Warning,
                    3.0f
                )->show();
            }
            return true;
        }

        auto const step = std::max<std::int64_t>(0, layer->m_currentStep - m_startStep);
        if (step != m_lastRecordedStep) {
            m_lastRecordedStep = step;
            m_sameStepOrder = 0;
        }

        m_currentRun.events.push_back(InputEvent {
            .step = step,
            .order = m_sameStepOrder++,
            .timestamp = std::max(0.0, layer->m_timestamp - m_startTimestamp),
            .button = button,
            .player1 = player1,
            .down = down,
        });
    }

    return true;
}

bool ReplaySession::beforeProcessCommands(GJBaseGameLayer* layer) {
    if (!layer || layer != m_layer || m_mode != SessionMode::Playback) return false;

    auto const step = std::max<std::int64_t>(0, layer->m_currentStep - m_startStep);
    while (m_playbackIndex < m_currentRun.events.size() &&
           m_currentRun.events[m_playbackIndex].step <= step) {
        auto const& event = m_currentRun.events[m_playbackIndex++];
        m_heldButtons[event.player1 ? 0 : 1][event.button] = event.down;
        m_injecting = true;
        layer->handleButton(event.down, event.button, event.player1);
        m_injecting = false;
    }
    if (step > m_currentRun.durationSteps + kReplayEndToleranceSteps) {
        finishPlayback(static_cast<PlayLayer*>(layer), SessionMode::Finished);
        return true;
    }
    return false;
}

bool ReplaySession::queueLatest(GJGameLevel* level, std::string& error) {
    if (!level) {
        error = "Open a level page before choosing a replay.";
        return false;
    }

    for (auto const& summary : ReplayStore::listSummaries()) {
        if (!isCompatible(level, summary)) continue;
        if (auto run = ReplayStore::load(summary.sourcePath)) {
            return queueRun(level, std::move(*run), error);
        }
    }

    error = "No compatible saved attempts exist for this exact level version yet.";
    return false;
}

bool ReplaySession::queueRun(GJGameLevel* level, ReplayRun run, std::string& error) {
    if (!level) {
        error = "Open a level page before choosing a replay.";
        return false;
    }
    if (!isCompatible(level, run)) {
        error = "That replay belongs to a different level or level version.";
        return false;
    }
    if (run.practice) {
        error = "Practice-mode playback is not supported yet.";
        return false;
    }

    m_queuedRun = std::move(run);
    return true;
}

bool ReplaySession::prepareQueuedPlayback(GJGameLevel* level) {
    if (!m_queuedRun || !isCompatible(level, *m_queuedRun)) return false;

    if (m_hasDontSaveOverride && m_overriddenLevel == level) {
        level->m_dontSave = true;
        return true;
    }

    restoreLevelSaveFlag();
    m_originalDontSave = level->m_dontSave;
    m_hasDontSaveOverride = true;
    m_overriddenLevel = level;
    m_overriddenLevel->retain();
    m_overriddenLevel->m_dontSave = true;
    return true;
}

void ReplaySession::cancelQueuedPlayback() {
    m_queuedRun.reset();
    if (!m_layer && m_mode == SessionMode::Idle) restoreLevelSaveFlag();
}

bool ReplaySession::startQueuedPlayback(PlayLayer* layer, std::string& error) {
    if (!m_queuedRun) {
        error = "The selected replay is no longer queued.";
        return false;
    }
    auto run = std::move(*m_queuedRun);
    m_queuedRun.reset();
    return startRun(layer, std::move(run), error);
}

bool ReplaySession::isCompatible(GJGameLevel* level, ReplayRun const& run) const {
    return level &&
        run.levelID == static_cast<int>(level->m_levelID) &&
        run.levelHash != 0 && run.levelHash == levelFingerprint(level) &&
        run.platformer == level->isPlatformer() &&
        !run.practice;
}

bool ReplaySession::isCompatible(PlayLayer* layer, ReplayRun const& run) const {
    auto const isActualPractice = layer == m_layer && m_hasPracticeOverride
        ? m_originalPractice
        : layer && layer->m_isPracticeMode;
    return layer && layer->m_level &&
        run.levelID == static_cast<int>(layer->m_level->m_levelID) &&
        run.levelHash != 0 && run.levelHash == fingerprintFor(layer) &&
        run.platformer == layer->m_isPlatformer &&
        !run.practice && !isActualPractice;
}

bool ReplaySession::startRun(PlayLayer* layer, ReplayRun run, std::string& error) {
    if (!layer || !layer->m_level) {
        error = "Open a level before starting a replay.";
        return false;
    }
    if (run.levelID != static_cast<int>(layer->m_level->m_levelID) ||
        run.levelHash == 0 || run.levelHash != fingerprintFor(layer)) {
        error = "That replay belongs to a different level or level version.";
        return false;
    }
    if (run.platformer != layer->m_isPlatformer) {
        error = "The replay's game mode does not match this level.";
        return false;
    }
    auto const isActualPractice = layer == m_layer && m_hasPracticeOverride
        ? m_originalPractice
        : layer->m_isPracticeMode;
    if (run.practice || isActualPractice) {
        error = "Practice-mode playback is not supported in this first version.";
        return false;
    }
    auto const preservePreparedSave = m_mode == SessionMode::Idle &&
        m_hasDontSaveOverride && m_overriddenLevel == layer->m_level;
    if (isPlaybackSession()) releaseHeldButtons(layer);
    if (!preservePreparedSave) {
        restorePlaybackEnvironment(layer);
    }
    else {
        restoreTimeScale();
    }
    m_previousTimeScale = cocos2d::CCScheduler::get()->getTimeScale();
    m_hasTimeScaleOverride = true;
    m_speed = 1.0f;
    cocos2d::CCScheduler::get()->setTimeScale(m_speed);

    m_originalPractice = layer->m_isPracticeMode;
    m_hasPracticeOverride = true;
    layer->m_isPracticeMode = true;

    if (!m_hasDontSaveOverride || m_overriddenLevel != layer->m_level) {
        restoreLevelSaveFlag();
        m_originalDontSave = layer->m_level->m_dontSave;
        m_hasDontSaveOverride = true;
        m_overriddenLevel = layer->m_level;
        m_overriddenLevel->retain();
    }
    m_overriddenLevel->m_dontSave = true;

    m_pendingRun = std::move(run);
    m_mode = SessionMode::ArmedPlayback;
    layer->resetLevelFromStart();
    return true;
}

void ReplaySession::releaseHeldButtons(GJBaseGameLayer* layer) {
    if (!layer) return;
    for (int player = 0; player < 2; ++player) {
        for (int button = 1; button <= 3; ++button) {
            if (!m_heldButtons[player][button]) continue;
            m_injecting = true;
            layer->handleButton(false, button, player == 0);
            m_injecting = false;
            m_heldButtons[player][button] = false;
        }
    }
}

void ReplaySession::restorePlaybackSafety(PlayLayer* layer) {
    if (m_hasPracticeOverride) {
        if (layer) layer->m_isPracticeMode = m_originalPractice;
        m_hasPracticeOverride = false;
    }

    restoreLevelSaveFlag();
}

void ReplaySession::restoreLevelSaveFlag() {
    if (m_hasDontSaveOverride && m_overriddenLevel) {
        m_overriddenLevel->m_dontSave = m_originalDontSave;
        m_overriddenLevel->release();
    }
    m_overriddenLevel = nullptr;
    m_hasDontSaveOverride = false;
}

void ReplaySession::restoreTimeScale() {
    if (m_hasTimeScaleOverride) {
        cocos2d::CCScheduler::get()->setTimeScale(m_previousTimeScale);
        m_hasTimeScaleOverride = false;
    }
}

void ReplaySession::restorePlaybackEnvironment(PlayLayer* layer) {
    restorePlaybackSafety(layer);
    restoreTimeScale();
}

void ReplaySession::finishPlayback(PlayLayer* layer, SessionMode finishMode) {
    // Keep practice mode and the level's no-save flag enabled until an
    // explicit restart or exit. Freeze on the final rendered frame so a death
    // is reviewable instead of immediately dropping into another attempt.
    releaseHeldButtons(layer);
    m_pendingRun.reset();
    m_playbackIndex = 0;
    m_mode = finishMode;
    cocos2d::CCScheduler::get()->setTimeScale(0.0f);
}

void ReplaySession::onPlaybackDeath(PlayLayer* layer) {
    finishPlayback(layer, SessionMode::DeathPaused);
}

void ReplaySession::onPlaybackComplete(PlayLayer* layer) {
    finishPlayback(layer, SessionMode::Finished);
}

bool ReplaySession::restartPlayback(PlayLayer* layer, std::string& error) {
    if (!isPlaybackSession() || !layer || layer != m_layer) {
        error = "No replay is open.";
        return false;
    }
    auto const requestedSpeed = m_speed;
    auto run = m_currentRun;
    if (!startRun(layer, std::move(run), error)) return false;
    m_speed = requestedSpeed;
    if (m_mode == SessionMode::Playback) {
        cocos2d::CCScheduler::get()->setTimeScale(m_speed);
    }
    return true;
}

bool ReplaySession::stopPlayback(PlayLayer* layer) {
    if (!isPlaybackSession() && m_mode != SessionMode::Finished) return false;

    releaseHeldButtons(layer);
    restoreTimeScale();
    m_pendingRun.reset();
    m_playbackIndex = 0;
    m_mode = SessionMode::Finished;

    if (layer) {
        layer->resetLevelFromStart();
    }
    else {
        restorePlaybackEnvironment(layer);
        m_mode = SessionMode::Idle;
    }
    return true;
}

bool ReplaySession::togglePause() {
    if (m_mode == SessionMode::Playback) {
        m_mode = SessionMode::Paused;
        cocos2d::CCScheduler::get()->setTimeScale(0.0f);
        return true;
    }
    if (m_mode == SessionMode::Paused) {
        m_mode = SessionMode::Playback;
        cocos2d::CCScheduler::get()->setTimeScale(m_speed);
        return true;
    }
    return false;
}

float ReplaySession::adjustSpeed(int direction) {
    static constexpr std::array speeds { 0.25f, 0.5f, 1.0f, 2.0f, 4.0f };
    if (m_mode != SessionMode::Playback && m_mode != SessionMode::Paused &&
        m_mode != SessionMode::DeathPaused && m_mode != SessionMode::Finished) {
        return m_speed;
    }

    auto iterator = std::find(speeds.begin(), speeds.end(), m_speed);
    if (iterator == speeds.end()) iterator = std::find(speeds.begin(), speeds.end(), 1.0f);
    if (direction < 0 && iterator != speeds.begin()) --iterator;
    if (direction > 0 && std::next(iterator) != speeds.end()) ++iterator;
    m_speed = *iterator;
    if (m_mode == SessionMode::Playback) cocos2d::CCScheduler::get()->setTimeScale(m_speed);
    return m_speed;
}

void ReplaySession::onLayerWillExit(PlayLayer* layer) {
    if (!layer || layer != m_layer) return;
    if (m_exitPrepared) return;
    m_exitPrepared = true;
    if (m_mode == SessionMode::Recording) finalizeRecording(layer, false);
    releaseHeldButtons(layer);
    restoreTimeScale();
}

void ReplaySession::onLayerDidExit(PlayLayer* layer) {
    if (!layer || layer != m_layer) return;

    // The layer has already exited. Do not call engine methods or touch its
    // fields here; the safety overrides did their job through vanilla exit.
    m_hasPracticeOverride = false;
    restoreLevelSaveFlag();
    m_hasTimeScaleOverride = false;
    m_queuedRun.reset();
    m_pendingRun.reset();
    m_mode = SessionMode::Idle;
    m_layer = nullptr;
    m_levelFingerprint = 0;
    m_resetDepth = 0;
    m_exitPrepared = false;
}

SessionMode ReplaySession::mode() const {
    return m_mode;
}

bool ReplaySession::isPlaybackSession() const {
    return m_mode == SessionMode::ArmedPlayback ||
           m_mode == SessionMode::Playback ||
           m_mode == SessionMode::Paused ||
           m_mode == SessionMode::DeathPaused ||
           m_mode == SessionMode::Finished;
}

bool ReplaySession::isActivelyPlaying() const {
    return m_mode == SessionMode::Playback;
}

float ReplaySession::speed() const {
    return m_speed;
}

std::string ReplaySession::statusText() const {
    switch (m_mode) {
        case SessionMode::Idle:
            return "READY";
        case SessionMode::Recording:
            return fmt::format("REC  ATTEMPT {}", m_currentRun.attempt);
        case SessionMode::ArmedPlayback:
            return "LOADING REPLAY";
        case SessionMode::Playback:
            return fmt::format("REPLAY  {:.2g}x", m_speed);
        case SessionMode::Paused:
            return fmt::format("PAUSED  {:.2g}x", m_speed);
        case SessionMode::DeathPaused:
            return "DEATH PAUSED";
        case SessionMode::Finished:
            return "REPLAY COMPLETE";
    }
    return "READY";
}

} // namespace replay_mod
