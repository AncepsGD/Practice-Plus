#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/binding/FMODAudioEngine.hpp>
#include <asp/fs/std.hpp>

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
            return asp::fs::Path::fromStdPath(file).string();
        }

        return (Mod::get()->getResourcesDir() /
                asp::fs::Path::fromStdPath(file)).string();
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

        auto file = asp::fs::Path{
            Mod::get()->getSettingValue<std::string>("checkpoint-sfx-path")
        }.toStdPath().unwrap();
        playCheckpointSound(file, "checkpoint_place_sfx.ogg"_spr);
        return ret;
    }

    void removeCheckpoint(bool p0)
    {
        PlayLayer::removeCheckpoint(p0);

        auto file = asp::fs::Path{
            Mod::get()->getSettingValue<std::string>("checkpoint-remove-sfx-path")
        }.toStdPath().unwrap();
        playCheckpointSound(file, "checkpoint_remove_sfx.ogg"_spr);
    }
};