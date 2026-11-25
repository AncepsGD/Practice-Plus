#include <Geode/Geode.hpp>
#include <Geode/modify/FMODAudioEngine.hpp>
#include <string>
#include <string_view>

using namespace geode::prelude;

namespace {
    bool isDeathSound(const std::string& path) {
        return std::string_view(path.c_str()).find("explode_11.ogg") != std::string_view::npos;
    }

    bool shouldSuppressDeathSound(const std::string& path) {
        auto mod = Mod::get();
        return mod &&
            mod->getSettingValue<bool>("no-death-sound") &&
            isDeathSound(path);
    }
}

class $modify(NoDeathSoundFMODAudioEngine, FMODAudioEngine) {
    int playEffect(std::string path) {
        if (shouldSuppressDeathSound(path)) return -1;
        return FMODAudioEngine::playEffect(path);
    }

    int playEffect(std::string path, float speed, float unknown, float volume) {
        if (shouldSuppressDeathSound(path)) return -1;
        return FMODAudioEngine::playEffect(path, speed, unknown, volume);
    }

    int playEffectAdvanced(
        std::string path, float speed, float unknown, float volume, float pitch,
        bool fastFourierTransform, bool reverb, int startMillis, int endMillis,
        int fadeIn, int fadeOut, bool loopEnabled, int effectID, bool overrideEffect,
        bool noPreload, int channelID, int uniqueID, float minInterval, int sfxGroup
    ) {
        if (shouldSuppressDeathSound(path)) return -1;
        return FMODAudioEngine::playEffectAdvanced(
            path, speed, unknown, volume, pitch, fastFourierTransform, reverb,
            startMillis, endMillis, fadeIn, fadeOut, loopEnabled, effectID,
            overrideEffect, noPreload, channelID, uniqueID, minInterval, sfxGroup
        );
    }
};
