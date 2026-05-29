#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/binding/FMODAudioEngine.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <random>
#include <string_view>

using namespace geode::prelude;

namespace checkpoint_mod
{
    namespace
    {
        constexpr float kMinPitch = 0.1f;

        inline float applyPitchVariance(float basePitch, float variance)
        {
            if (variance <= 0.f || !std::isfinite(variance))
            {
                return basePitch;
            }

            thread_local std::mt19937 rng{std::random_device{}()};

            return std::max(
                kMinPitch,
                basePitch + std::uniform_real_distribution<float>(
                                -variance,
                                variance)(rng));
        }

        inline bool startsWithResources(std::string_view str)
        {
            return str.starts_with("resources/");
        }
    }

    void playCheckpointSound(
        bool enabled,
        std::filesystem::path const &file,
        char const *fallback,
        double pitchVariance = 0.0)
    {
        if (!enabled)
        {
            return;
        }

        auto *engine = FMODAudioEngine::sharedEngine();
        if (!engine)
        {
            return;
        }

        auto const pitch =
            applyPitchVariance(1.f, static_cast<float>(pitchVariance));

        if (file.empty())
        {
            if (fallback)
            {
                engine->playEffect(fallback, pitch, 1.f, 1.f);
            }
            return;
        }

        auto const fileString = file.string();

        if (fileString == "YourSound")
        {
            if (fallback)
            {
                engine->playEffect(fallback, pitch, 1.f, 1.f);
            }
            return;
        }

        auto *mod = Mod::get();
        auto const &resourcesDir = mod->getResourcesDir();

        std::filesystem::path playPath;

        if (startsWithResources(fileString))
        {
            playPath = resourcesDir / fileString.substr(10);
        }
        else if (file.is_absolute())
        {
            playPath = file;
        }
        else
        {
            playPath = resourcesDir / file;
        }

        std::error_code ec;

        if (std::filesystem::exists(playPath, ec))
        {
            auto const pathString = playPath.string();
            engine->playEffect(pathString.c_str(), pitch, 1.f, 1.f);
            return;
        }

        if (fallback)
        {
            engine->playEffect(fallback, pitch, 1.f, 1.f);
        }
    }
}

class $modify(CheckpointRemoveSFXPlayLayer, PlayLayer)
{
    void removeCheckpoint(CheckpointObject *checkpoint)
    {
        auto *mod = Mod::get();

        checkpoint_mod::playCheckpointSound(
            mod->getSettingValue<bool>("checkpoint-sfx-enabled"),
            mod->getSettingValue<std::string>("checkpoint-remove-sfx-path"),
            "checkpoint_remove_sfx.ogg",
            mod->getSettingValue<double>("checkpoint-sfx-pitch-variance"));

        PlayLayer::removeCheckpoint(checkpoint);
    }
};