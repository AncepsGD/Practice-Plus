#pragma once
#include <string>
#include <vector>

struct LevelPack {
    std::string name;
    std::string spriteFrameName;
    std::vector<int> levelIds;
};

namespace LevelPacks {
    // Add, remove, or customize any packs and level IDs here!
    inline const std::vector<LevelPack> customPacks = {
        {
            "Impossible NC Pack",
            "GJ_weeklyBtn_001.png", // Challenges icon
            { 73836744, 129974620, 80910910, 115930714 } // Custom Level IDs
        }
    };
}