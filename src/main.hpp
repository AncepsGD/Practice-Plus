#pragma once
#include <Geode/utils/web.hpp>
#include <string>
#include <vector>

struct PML {
    int id = 0;
    int rank = 0;
    std::string name;

    bool operator==(const PML& other) const {
        return id == other.id && rank == other.rank;
    }
};

namespace PracticeModeList {
    extern std::vector<PML> levels;
    extern bool levelsLoaded;
    extern std::string currentListUrl; // Tracks which file to fetch (levels.json or verifications.json)

    void loadPracticeList(
        geode::async::TaskHolder<geode::utils::web::WebResponse>& listener, 
        geode::Function<void()> success, 
        geode::CopyableFunction<void(int)> failure
    );
}