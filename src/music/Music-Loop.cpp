#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/binding/FMODAudioEngine.hpp>
#include <Geode/binding/GJGameLevel.hpp>
#include <Geode/binding/MusicDownloadManager.hpp>
#include <filesystem>
#include <string>

using namespace geode::prelude;

class PracticeAudioLoop {
public:
    ~PracticeAudioLoop() {
        stop();
    }

    bool isActive() {
        if (!m_channel) return false;
        bool playing = false;
        auto result = m_channel->isPlaying(&playing);
        return result == FMOD_OK && playing;
    }

    bool hasStarted() const {
        return m_started;
    }

    bool start(const std::string& path, float volume, bool loop) {
        stop();
        if (path.empty()) {
            return false;
        }

        auto engine = FMODAudioEngine::sharedEngine();
        if (!engine || !engine->m_system) {
            return false;
        }

        FMOD::Sound* sound = nullptr;
        auto result = engine->m_system->createStream(
            path.c_str(),
            FMOD_DEFAULT | FMOD_CREATESTREAM | (loop ? FMOD_LOOP_NORMAL : FMOD_LOOP_OFF),
            nullptr,
            &sound
        );
        if (result != FMOD_OK || !sound) {
            return false;
        }

        FMOD::Channel* channel = nullptr;
        result = engine->m_system->playSound(sound, nullptr, true, &channel);
        if (result != FMOD_OK || !channel) {
            sound->release();
            return false;
        }

        if (loop) channel->setLoopCount(-1);
        if (channel->setVolume(volume) != FMOD_OK || channel->setPaused(false) != FMOD_OK) {
            channel->stop();
            sound->release();
            return false;
        }

        m_sound = sound;
        m_channel = channel;
        m_started = true;
        return true;
    }

    void stop() {
        if (m_channel) m_channel->stop();
        clearSound();
        m_started = false;
    }

    void setVolume(float volume) {
        if (m_channel) m_channel->setVolume(volume);
    }

    void setPaused(bool paused) {
        if (m_channel) m_channel->setPaused(paused);
    }

private:
    void clearSound() {
        m_channel = nullptr;
        if (m_sound) {
            m_sound->release();
            m_sound = nullptr;
        }
    }

    FMOD::Sound* m_sound = nullptr;
    FMOD::Channel* m_channel = nullptr;
    bool m_started = false;
};

class $modify(PracticeMusicLoopPlayLayer, PlayLayer) {
    struct Fields {
        bool practiceMode = false;
        bool gameplayPaused = false;
        bool practicePausePending = false;
        float restartCooldown = 0.f;
        std::string rawActivePath;
        std::string resolvedAudioPath;
        PracticeAudioLoop practiceAudio;
    };

    static constexpr int kGameplayMusicChannel = 0;

    std::string resolveAudioPath(const std::string& path) {
        if (path.empty() || std::filesystem::exists(path)) return path;

        auto fileUtils = cocos2d::CCFileUtils::sharedFileUtils();
        if (fileUtils && !fileUtils->isAbsolutePath(path.c_str())) {
            auto fullPath = fileUtils->fullPathForFilename(path.c_str(), false);
            if (!fullPath.empty() && std::filesystem::exists(std::string(fullPath))) {
                return std::string(fullPath);
            }
        }
        return path;
    }

    std::string getRawLevelMusicPath(FMODAudioEngine* engine) {
        if (!engine || !m_level) return {};

        if (m_level->m_songID > 0) {
            if (auto manager = MusicDownloadManager::sharedState()) {
                auto downloaded = manager->pathForSong(m_level->m_songID);
                if (!downloaded.empty()) return downloaded;
            }
        }

        return m_level->getAudioFileName();
    }

    void enterPracticeMode() {
        if (m_fields->practiceMode) return;
        m_fields->practiceMode = true;
        m_fields->restartCooldown = 0.f;
        m_fields->rawActivePath.clear();
        m_fields->resolvedAudioPath.clear();
        syncPracticeState(0.f);
    }

    void exitPracticeMode() {
        if (!m_fields->practiceMode) {
            m_fields->practicePausePending = false;
            return;
        }
        m_fields->practiceMode = false;
        m_fields->practicePausePending = false;
        m_fields->practiceAudio.stop();
        m_fields->rawActivePath.clear();
        m_fields->resolvedAudioPath.clear();
        m_fields->restartCooldown = 0.f;

        if (auto engine = FMODAudioEngine::sharedEngine()) {
            if (m_fields->gameplayPaused) {
                m_fields->gameplayPaused = false;
                engine->resumeMusic(kGameplayMusicChannel);
            }
            if (engine->m_backgroundMusicChannel) {
                engine->m_backgroundMusicChannel->setMute(false);
            }
        }
    }

    void syncPracticeState(float dt) {
        if (!m_fields->practiceMode) return;

        auto engine = FMODAudioEngine::sharedEngine();
        if (!engine) return;

        engine->pauseMusic(kGameplayMusicChannel);
        if (engine->m_backgroundMusicChannel) {
            engine->m_backgroundMusicChannel->setMute(true);
        }

        if (m_fields->gameplayPaused) {
            m_fields->practiceAudio.setPaused(true);
            return;
        }

        if (m_fields->restartCooldown > 0.f) m_fields->restartCooldown -= dt;

        auto rawPath = getRawLevelMusicPath(engine);
        if (rawPath != m_fields->rawActivePath) {
            m_fields->practiceAudio.stop();
            m_fields->rawActivePath = rawPath;
            m_fields->resolvedAudioPath = resolveAudioPath(rawPath);
            m_fields->restartCooldown = 0.f;
        }

        if (m_fields->practiceAudio.isActive()) {
            m_fields->practiceAudio.setVolume(engine->getBackgroundMusicVolume());
            return;
        }

        if (m_fields->restartCooldown > 0.f) return;
        const bool loopMusic = Mod::get()->getSettingValue<bool>("music-loop");
        m_fields->practiceAudio.start(m_fields->resolvedAudioPath, engine->getBackgroundMusicVolume(), loopMusic);
        m_fields->restartCooldown = 1.f;
    }

    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;
        m_fields->practiceAudio.stop();
        m_fields->practiceMode = false;
        m_fields->gameplayPaused = false;
        m_fields->practicePausePending = false;
        m_fields->restartCooldown = 0.f;
        m_fields->rawActivePath.clear();
        m_fields->resolvedAudioPath.clear();
        return true;
    }

    void togglePracticeMode(bool practiceMode) {
        PlayLayer::togglePracticeMode(practiceMode);
        if (practiceMode) enterPracticeMode();
        else exitPracticeMode();
    }

    void destroyPlayer(PlayerObject* player, GameObject* object) {
        PlayLayer::destroyPlayer(player, object);
        syncPracticeState(0.f);
    }

    void resetLevel() {
        PlayLayer::resetLevel();
        syncPracticeState(0.f);
    }

    void startMusic() {
        PlayLayer::startMusic();
        syncPracticeState(0.f);
    }

    void update(float dt) {
        PlayLayer::update(dt);
        syncPracticeState(dt);
    }

    void levelComplete() {
        PlayLayer::levelComplete();
        exitPracticeMode();
    }

    void onQuit() {
        exitPracticeMode();
        PlayLayer::onQuit();
    }

    void onExit() {
        if (!m_fields->gameplayPaused) {
            exitPracticeMode();
        }
        PlayLayer::onExit();
    }

    void pauseGame(bool unfocused) {
        const bool practiceWasActive = m_fields->practiceMode;
        if (practiceWasActive) {
            m_fields->practicePausePending = true;
            m_fields->gameplayPaused = true;
            m_fields->practiceAudio.setPaused(true);
        }
        PlayLayer::pauseGame(unfocused);
        if (practiceWasActive) {
            if (auto engine = FMODAudioEngine::sharedEngine()) {
                engine->pauseMusic(kGameplayMusicChannel);
                if (engine->m_backgroundMusicChannel) {
                    engine->m_backgroundMusicChannel->setMute(true);
                }
            }
        }
    }

    void resume() {
        const bool restorePracticeAudio = m_fields->practiceMode || m_fields->practicePausePending;
        PlayLayer::resume();
        if (restorePracticeAudio) {
            m_fields->practiceMode = true;
            m_fields->practicePausePending = false;
            m_fields->gameplayPaused = false;
            m_fields->practiceAudio.setPaused(false);
            if (!m_fields->practiceAudio.isActive()) {
                m_fields->practiceAudio.stop();
                m_fields->restartCooldown = 0.f;
            }
            syncPracticeState(0.f);
        }
    }
};