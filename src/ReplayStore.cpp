#include "ReplayStore.hpp"

#include <Geode/Geode.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>

using namespace geode::prelude;

namespace replay_mod {
namespace {

constexpr char kMagic[] = "GD_REPLAY_MOD";
constexpr std::uintmax_t kMaximumFileBytes = 64ull * 1024ull * 1024ull;
constexpr std::size_t kMaximumLevelNameBytes = 256;
std::atomic_uint64_t g_fileSequence = 0;

bool readKey(std::istream& stream, char const* expected) {
    std::string key;
    return static_cast<bool>(stream >> key) && key == expected;
}

std::optional<ReplayRun> loadInternal(std::filesystem::path const& path, bool includeEvents) {
    std::error_code sizeError;
    auto const fileBytes = std::filesystem::file_size(path, sizeError);
    if (sizeError || fileBytes > kMaximumFileBytes) return std::nullopt;

    std::ifstream input(path, std::ios::binary);
    if (!input) return std::nullopt;

    ReplayRun run;
    std::string magic;
    if (!(input >> magic >> run.formatVersion) || magic != kMagic || run.formatVersion != 2) {
        return std::nullopt;
    }

    int completed = 0;
    int platformer = 0;
    int practice = 0;
    std::size_t eventCount = 0;

    if (!readKey(input, "level_id") || !(input >> run.levelID)) return std::nullopt;
    if (!readKey(input, "level_hash") || !(input >> run.levelHash)) return std::nullopt;
    if (!readKey(input, "level_name") || !(input >> std::quoted(run.levelName))) return std::nullopt;
    if (run.levelName.size() > kMaximumLevelNameBytes) return std::nullopt;
    if (!readKey(input, "attempt") || !(input >> run.attempt)) return std::nullopt;
    if (!readKey(input, "created_at_ms") || !(input >> run.createdAtMs)) return std::nullopt;
    if (!readKey(input, "duration_steps") || !(input >> run.durationSteps)) return std::nullopt;
    if (!readKey(input, "progress") || !(input >> run.progress)) return std::nullopt;
    if (!readKey(input, "completed") || !(input >> completed)) return std::nullopt;
    if (!readKey(input, "platformer") || !(input >> platformer)) return std::nullopt;
    if (!readKey(input, "practice") || !(input >> practice)) return std::nullopt;
    if (!readKey(input, "random_seed") || !(input >> run.randomSeed)) return std::nullopt;
    if (!readKey(input, "replay_random_seed") || !(input >> run.replayRandomSeed)) return std::nullopt;
    if (!readKey(input, "event_count") || !(input >> eventCount)) return std::nullopt;
    if (eventCount > kMaximumReplayEvents || !readKey(input, "events")) return std::nullopt;
    if (run.durationSteps < 0 || run.durationSteps > kMaximumReplayDurationSteps ||
        run.attempt < 0 || !std::isfinite(run.progress) ||
        run.progress < 0.0 || run.progress > 100.001) {
        return std::nullopt;
    }

    run.completed = completed != 0;
    run.platformer = platformer != 0;
    run.practice = practice != 0;
    run.storedEventCount = eventCount;
    run.sourcePath = path;

    if (!includeEvents) return run;
    run.events.reserve(eventCount);

    for (std::size_t index = 0; index < eventCount; ++index) {
        InputEvent event;
        int player1 = 0;
        int down = 0;
        if (!(input
            >> event.step
            >> event.order
            >> event.timestamp
            >> event.button
            >> player1
            >> down)) {
            return std::nullopt;
        }
        if (event.step < 0 || event.step > run.durationSteps + kReplayEndToleranceSteps ||
            event.button < 1 || event.button > 3 || !std::isfinite(event.timestamp) ||
            event.timestamp < 0.0) {
            return std::nullopt;
        }
        event.player1 = player1 != 0;
        event.down = down != 0;
        run.events.push_back(event);
    }

    if (!std::is_sorted(run.events.begin(), run.events.end(), [](auto const& left, auto const& right) {
        if (left.step != right.step) return left.step < right.step;
        return left.order < right.order;
    })) {
        return std::nullopt;
    }

    return run;
}

} // namespace

std::filesystem::path ReplayStore::replayDirectory() {
    return Mod::get()->getSaveDir() / "replays";
}

SaveResult ReplayStore::save(ReplayRun const& run) {
    if (run.formatVersion != 2 || run.levelName.size() > kMaximumLevelNameBytes ||
        run.events.size() > kMaximumReplayEvents || run.durationSteps < 0 ||
        run.durationSteps > kMaximumReplayDurationSteps || !std::isfinite(run.progress) ||
        run.progress < 0.0 || run.progress > 100.001) {
        return { false, {}, "Replay data failed validation." };
    }

    std::error_code error;
    auto const directory = replayDirectory();
    std::filesystem::create_directories(directory, error);
    if (error) {
        return { false, {}, fmt::format("Could not create replay folder: {}", error.message()) };
    }

    auto const sequence = g_fileSequence.fetch_add(1);
    auto const filename = fmt::format(
        "level-{}-{}-{}-{}.gdrm",
        run.levelID,
        run.createdAtMs,
        run.attempt,
        sequence
    );
    auto const finalPath = directory / filename;
    auto const temporaryPath = directory / (filename + ".tmp");

    std::ofstream output(temporaryPath, std::ios::binary | std::ios::trunc);
    if (!output) {
        return { false, {}, "Could not open the replay file for writing." };
    }

    output << kMagic << ' ' << run.formatVersion << '\n';
    output << "level_id " << run.levelID << '\n';
    output << "level_hash " << run.levelHash << '\n';
    output << "level_name " << std::quoted(run.levelName) << '\n';
    output << "attempt " << run.attempt << '\n';
    output << "created_at_ms " << run.createdAtMs << '\n';
    output << "duration_steps " << run.durationSteps << '\n';
    output << "progress " << std::setprecision(17) << run.progress << '\n';
    output << "completed " << static_cast<int>(run.completed) << '\n';
    output << "platformer " << static_cast<int>(run.platformer) << '\n';
    output << "practice " << static_cast<int>(run.practice) << '\n';
    output << "random_seed " << run.randomSeed << '\n';
    output << "replay_random_seed " << run.replayRandomSeed << '\n';
    output << "event_count " << run.events.size() << '\n';
    output << "events\n";

    for (auto const& event : run.events) {
        output
            << event.step << ' '
            << event.order << ' '
            << std::setprecision(17) << event.timestamp << ' '
            << event.button << ' '
            << static_cast<int>(event.player1) << ' '
            << static_cast<int>(event.down) << '\n';
    }

    output.flush();
    if (!output.good()) {
        output.close();
        std::filesystem::remove(temporaryPath, error);
        return { false, {}, "The replay could not be written completely." };
    }
    output.close();

    std::filesystem::rename(temporaryPath, finalPath, error);
    if (error) {
        std::filesystem::remove(temporaryPath, error);
        return { false, {}, fmt::format("Could not finish saving the replay: {}", error.message()) };
    }

    return { true, finalPath, {} };
}

std::optional<ReplayRun> ReplayStore::load(std::filesystem::path const& path) {
    return loadInternal(path, true);
}

std::vector<ReplayRun> ReplayStore::listSummaries() {
    std::vector<ReplayRun> runs;
    std::error_code error;
    auto const directory = replayDirectory();
    if (!std::filesystem::exists(directory, error)) return runs;

    for (std::filesystem::directory_iterator iterator(directory, error), end;
         !error && iterator != end;
         iterator.increment(error)) {
        auto const& entry = *iterator;
        if (!entry.is_regular_file(error) || entry.path().extension() != ".gdrm") continue;
        if (auto run = loadInternal(entry.path(), false)) runs.push_back(std::move(*run));
    }

    std::sort(runs.begin(), runs.end(), [](auto const& left, auto const& right) {
        return left.createdAtMs > right.createdAtMs;
    });
    return runs;
}

std::vector<ReplayRun> ReplayStore::listAll() {
    std::vector<ReplayRun> runs;
    for (auto const& summary : listSummaries()) {
        if (auto run = load(summary.sourcePath)) runs.push_back(std::move(*run));
    }
    return runs;
}

std::vector<ReplayRun> ReplayStore::listForLevel(int levelID) {
    auto runs = listAll();
    std::erase_if(runs, [levelID](auto const& run) {
        return run.levelID != levelID;
    });
    return runs;
}

std::optional<ReplayRun> ReplayStore::latestForLevel(int levelID) {
    for (auto const& summary : listSummaries()) {
        if (summary.levelID != levelID) continue;
        if (auto run = load(summary.sourcePath)) return run;
    }
    return std::nullopt;
}

} // namespace replay_mod
