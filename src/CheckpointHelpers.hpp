#pragma once

#include <Geode/Geode.hpp>
#include <string>
#include <string_view>

namespace cocos2d
{
    class CCSprite;
}

namespace checkpoint_mod
{
    struct Pack
    {
        const char *outerName;
        const char *innerName;
    };

    struct CheckpointStyle
    {
        std::string shape;
        float scale;
        cocos2d::ccColor3B outerColor;
        cocos2d::ccColor3B innerColor;
    };

    static constexpr char const *OuterId = "checkpoint-mod-outer";
    static constexpr char const *InnerId = "checkpoint-mod-inner";

    cocos2d::ccColor3B parseColor(std::string const &value);
    Pack const &getPack(std::string_view key);
    CheckpointStyle const &getStyle();
}