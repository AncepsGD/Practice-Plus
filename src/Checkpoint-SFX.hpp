#pragma once

#include <Geode/Geode.hpp>
#include <filesystem>

namespace checkpoint_mod {
    void playCheckpointSound(bool enabled, std::filesystem::path const& file, char const* fallback);
}
