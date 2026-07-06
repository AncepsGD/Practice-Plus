#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/binding/FMODAudioEngine.hpp>

#include <filesystem>
#include <system_error>

using namespace geode::prelude;

namespace
{
    std::string resolveSound(std::filesystem::path const &file)
    {
        if (file.empty())
        {
            return {};
        }

        if (file.is_absolute())
        {
            return string::pathToString(file);
        }

        return string::pathToString(Mod::get()->getResourcesDir() / file);
    }

    void playCheckpointSound(std::filesystem::path const &file, std::filesystem::path const &fallback)
    {
        if (!Mod::get()->getSettingValue<bool>("checkpoint-sounds-enabled"))
            return;

        auto *engine = FMODAudioEngine::sharedEngine();
        if (!engine)
            return;

        std::error_code ec;

        auto resolved = resolveSound(file);
        if (!resolved.empty() && std::filesystem::exists(resolved, ec))
        {
            engine->playEffect(resolved);
            return;
        }

        auto fallbackResolved = resolveSound(fallback);
        if (!fallbackResolved.empty() && std::filesystem::exists(fallbackResolved, ec))
        {
            engine->playEffect(fallbackResolved);
        }
    }
}

class $modify(PlayLayer)
{
    CheckpointObject *createCheckpoint()
    {
        auto ret = PlayLayer::createCheckpoint();
        if (!ret)
            return nullptr;

        auto file = Mod::get()->getSettingValue<std::filesystem::path>("checkpoint-sfx-path");
        playCheckpointSound(file, "checkpoint_place_sfx.ogg"_spr);
        return ret;
    }

    void removeCheckpoint(bool p0)
    {
        PlayLayer::removeCheckpoint(p0);

        auto file = Mod::get()->getSettingValue<std::filesystem::path>("checkpoint-remove-sfx-path");
        playCheckpointSound(file, "checkpoint_remove_sfx.ogg"_spr);
    }
};