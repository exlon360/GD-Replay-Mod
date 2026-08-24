#pragma once

#include "ReplayTypes.hpp"

#include <optional>
#include <vector>

namespace replay_mod {

class ReplayStore final {
public:
    static std::filesystem::path replayDirectory();
    static SaveResult save(ReplayRun const& run);
    static std::optional<ReplayRun> load(std::filesystem::path const& path);
    static std::vector<ReplayRun> listSummaries();
    static std::vector<ReplayRun> listAll();
    static std::vector<ReplayRun> listForLevel(int levelID);
    static std::optional<ReplayRun> latestForLevel(int levelID);
};

} // namespace replay_mod
