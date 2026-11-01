#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/FMODAudioEngine.hpp>

using namespace geode::prelude;

namespace {
    bool musicDisabled() {
        return Mod::get()->getSettingValue<bool>("disable-level-music");
    }

    bool normalModeMusicDisabled() {
        return Mod::get()->getSettingValue<bool>("disable-level-music-normal-mode");
    }

    bool shouldDisableMusic(bool isPracticeMode) {
        return musicDisabled() && (isPracticeMode || normalModeMusicDisabled());
    }

    bool blockMusic() {
        auto playLayer = PlayLayer::get();
        return playLayer && shouldDisableMusic(playLayer->m_isPracticeMode);
    }
}

class $modify(NoMusicPlayLayer, PlayLayer) {
    void startMusic() {
        if (shouldDisableMusic(m_isPracticeMode))
            return;
        PlayLayer::startMusic();
    }
};

class $modify(NoMusicFMOD, FMODAudioEngine) {
    void loadAndPlayMusic(gd::string path, unsigned int time, int musicID) {
        if (blockMusic()) {
            log::debug("blocked loadAndPlayMusic: {}", std::string(path));
            return;
        }
        FMODAudioEngine::loadAndPlayMusic(path, time, musicID);
    }

    void setMusicTimeMS(unsigned int ms, bool finishFade, int channel) {
        if (blockMusic())
            return;
        FMODAudioEngine::setMusicTimeMS(ms, finishFade, channel);
    }

    void playMusic(gd::string path, bool shouldLoop, float fadeInTime, int channel) {
        if (blockMusic()) {
            log::debug("blocked playMusic: {}", std::string(path));
            return;
        }
        FMODAudioEngine::playMusic(path, shouldLoop, fadeInTime, channel);
    }

    void loadMusic(gd::string path, float speed, float unknown, float volume, bool shouldLoop, int musicID, int channelID, bool dontReset) {
        if (blockMusic()) {
            log::debug("blocked loadMusic: {}", std::string(path));
            return;
        }
        FMODAudioEngine::loadMusic(path, speed, unknown, volume, shouldLoop, musicID, channelID, dontReset);
    }

    void preloadMusic(gd::string path, bool fade, int audioID) {
        if (blockMusic()) {
            log::debug("blocked preloadMusic: {}", std::string(path));
            return;
        }
        FMODAudioEngine::preloadMusic(path, fade, audioID);
    }

    void queueStartMusic(
        gd::string path, float pitch, float unknown, float volume, bool loop,
        int start, int end, int fadeIn, int fadeOut, int musicID, bool p10,
        int channelID, bool noPrepare, bool dontReset
    ) {
        if (blockMusic()) {
            log::debug("blocked queueStartMusic: {}", std::string(path));
            return;
        }
        FMODAudioEngine::queueStartMusic(
            path, pitch, unknown, volume, loop, start, end, fadeIn, fadeOut,
            musicID, p10, channelID, noPrepare, dontReset
        );
    }

    void resumeAllMusic() {
        if (blockMusic())
            return;
        FMODAudioEngine::resumeAllMusic();
    }
};