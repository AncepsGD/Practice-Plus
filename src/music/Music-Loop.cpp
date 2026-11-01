#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/binding/FMODAudioEngine.hpp>
#include <Geode/binding/GJGameLevel.hpp>
#include <Geode/binding/MusicDownloadManager.hpp>
#include <Geode/utils/web.hpp>
#include <cctype>
#include <filesystem>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_set>

using namespace geode::prelude;

namespace {
    std::unordered_set<std::string> g_pendingMusicDownloads;
    std::unordered_set<std::string> g_failedMusicDownloads;

    bool levelMusicDisabled() {
        return Mod::get()->getSettingValue<bool>("disable-level-music");
    }

    std::filesystem::path getCachedMusicPath(int songID) {
        return Mod::get()->getSaveDir() / "music-loop-cache" /
            fmt::format("newgrounds-{}.mp3", songID);
    }

    std::string normalizeMusicName(std::string_view name) {
        std::string normalized;
        for (const unsigned char character : name) {
            if (std::isalnum(character)) {
                normalized.push_back(static_cast<char>(std::tolower(character)));
            }
        }
        return normalized;
    }

    void downloadMusic(const std::string& url, const std::filesystem::path& cachePath) {
        if (g_pendingMusicDownloads.contains(url) || g_failedMusicDownloads.contains(url)) {
            return;
        }

        g_pendingMusicDownloads.insert(url);
        auto mod = Mod::get();
        std::thread([url, cachePath, mod] {
            auto response = web::WebRequest().getSync(url, mod);
            bool success = response.ok();

            if (success) {
                std::error_code error;
                std::filesystem::create_directories(cachePath.parent_path(), error);
                if (!error) {
                    auto saved = response.into(cachePath);
                    success = saved.isOk();
                } else {
                    success = false;
                }
            }

            queueInMainThread([url, success] {
                g_pendingMusicDownloads.erase(url);
                if (!success) {
                    g_failedMusicDownloads.insert(url);
                }
            });
        }).detach();
    }
}

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
        if (result != FMOD_OK || !sound) return false;

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
        bool loopSettingState = false;
        bool hasLoggedLoopSetting = false;
        float restartCooldown = 0.f;
        std::string rawActivePath;
        std::string resolvedAudioPath;
        std::unordered_set<int> requestedSongDownloads;
        PracticeAudioLoop practiceAudio;
    };

    static constexpr int kGameplayMusicChannel = 0;

    std::string resolveAudioPath(const std::string& path) {
        if (path.empty() || path.starts_with("http://") || path.starts_with("https://") ||
            std::filesystem::exists(path)) {
            return path;
        }

        auto fileUtils = cocos2d::CCFileUtils::sharedFileUtils();
        if (fileUtils && !fileUtils->isAbsolutePath(path.c_str())) {
            auto fullPath = fileUtils->fullPathForFilename(path.c_str(), false);
            if (!fullPath.empty() && std::filesystem::exists(std::string(fullPath))) {
                return std::string(fullPath);
            }
        }
        return path;
    }

    std::pair<int, const char*> getAlternativeMainLevelSong(GJGameLevel* level) const {
        if (!level) return {0, nullptr};

        auto findAlternate = [](const std::string& name) -> std::pair<int, const char*> {
            if (name == "stereomadness") return {500476, "https://audio.ngfiles.com/500000/500476_Stereo-Madness.mp3?f1355466273"};
            if (name == "backontrack") return {522654, "https://audio.ngfiles.com/522000/522654_Back-On-Track.mp3?f1588003046"};
            if (name == "polargeist") return {523561, "https://audio.ngfiles.com/523000/523561_Polargeist.mp3?f1360958175"};
            if (name == "dryout") return {498543, "https://audio.ngfiles.com/498000/498543_Dry-Out.mp3?f1588002588"};
            if (name == "baseafterbase") return {404997, "https://audio.ngfiles.com/404000/404997_DJ_V.I.___Base_After_Base.mp3?f1588001092"};
            if (name == "cantletgo") return {485351, "https://audio.ngfiles.com/485000/485351_Cant-Let-Go.mp3?f1588002270"};
            if (name == "jumper") return {168734, "https://audio.ngfiles.com/168000/168734_Jumper.mp3?f1220049621"};
            if (name == "cycles") return {529148, "https://audio.ngfiles.com/529000/529148_Cycles.mp3?f1588003188"};
            if (name == "xstep") return {516735, "https://audio.ngfiles.com/516000/516735_xStep.mp3?f1588002941"};
            if (name == "clutterfunk") return {505816, "https://audio.ngfiles.com/505000/505816_-Clutterfunk-.mp3?f1349398561"};
            if (name == "theoryofeverything") return {350290, "https://audio.ngfiles.com/350000/350290_MToE.mp3?f1280376621"};
            if (name == "electromanadventures") return {479319, "https://audio.ngfiles.com/479000/479319_-Electroman-adventures-.mp3?f1333527337"};
            if (name == "clubstep") return {396093, "https://audio.ngfiles.com/396000/396093_naten3e222w.mp3?f1296537308"};
            if (name == "electrodynamix") return {368392, "https://audio.ngfiles.com/368000/368392_electrodynamix.mp3?f1286780109"};
            if (name == "hexagonforce") return {568699, "https://audio.ngfiles.com/568000/568699_-Hexagon-Force-.mp3?f1393692231"};
            if (name == "blastprocessing") return {507560, "https://audio.ngfiles.com/507000/507560_-BlastProcess-.mp3?f1588002767"};
            if (name == "theoryofeverything2") return {790340, "https://audio.ngfiles.com/790000/790340_dj-N-Theory-of-Everything-.mp3?f1518587332"};
            if (name == "geometricaldominator") return {641172, "https://audio.ngfiles.com/641000/641172_-Geometrical-Dominator-.mp3?f1440688001"};
            if (name == "deadlocked") return {503731, "https://audio.ngfiles.com/503000/503731_F-777---Deadlocked-Full.mp3?f1348017408"};
            return {0, nullptr};
        };

        auto alternate = findAlternate(normalizeMusicName(level->m_levelName.c_str()));
        if (alternate.first > 0) return alternate;

        auto audioFile = level->getAudioFileName();
        return findAlternate(
            normalizeMusicName(std::filesystem::path(audioFile.c_str()).stem().string())
        );
    }

    std::string getRawLevelMusicPath(FMODAudioEngine* engine) {
        if (!engine || !m_level) return {};

        const auto [alternativeSongID, alternativeSongURL] =
            getAlternativeMainLevelSong(m_level);
        auto songID = alternativeSongID;
        if (songID == 0) songID = m_level->m_songID;

        if (songID > 0) {
            if (auto manager = MusicDownloadManager::sharedState()) {
                if (manager->isSongDownloaded(songID)) {
                    auto downloaded = manager->pathForSong(songID);
                    if (!downloaded.empty()) {
                        return downloaded;
                    }
                } else if (m_fields->requestedSongDownloads.insert(songID).second) {
                    manager->downloadCustomSong(songID);
                }
                if (alternativeSongID == 0) return m_level->getAudioFileName();
            }
            if (alternativeSongID > 0) {
                const auto cachePath = getCachedMusicPath(alternativeSongID);
                std::error_code error;
                if (std::filesystem::exists(cachePath, error) && !error &&
                    std::filesystem::file_size(cachePath, error) > 0 && !error) {
                    return cachePath.string();
                }
                downloadMusic(alternativeSongURL, cachePath);
                return {};
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
        m_fields->requestedSongDownloads.clear();
        if (m_level) {
            const auto alternativeSong = getAlternativeMainLevelSong(m_level);
            if (alternativeSong.first > 0) {
                g_failedMusicDownloads.erase(std::string(alternativeSong.second));
            }
        }
        syncPracticeState(0.f);
    }

    void exitPracticeMode() {
        if (!m_fields->practiceMode) {
            m_fields->practicePausePending = false;
            m_fields->hasLoggedLoopSetting = false;
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
                if (levelMusicDisabled()) {
                    engine->pauseMusic(kGameplayMusicChannel);
                }
                else {
                    engine->resumeMusic(kGameplayMusicChannel);
                }
            }
            if (engine->m_backgroundMusicChannel) {
                engine->m_backgroundMusicChannel->setMute(false);
            }
        }
    }

    void syncPracticeState(float dt) {
        if (m_isPracticeMode != m_fields->practiceMode) {
            m_fields->practiceMode = m_isPracticeMode;
            m_fields->restartCooldown = 0.f;
            m_fields->rawActivePath.clear();
            m_fields->resolvedAudioPath.clear();
            if (!m_fields->practiceMode) {
                m_fields->practiceAudio.stop();
            }
        }

        if (!m_fields->practiceMode) return;

        auto engine = FMODAudioEngine::sharedEngine();
        if (!engine) return;

        const bool loopMusic = levelMusicDisabled() &&
            Mod::get()->getSettingValue<bool>("music-loop");
        if (!loopMusic) {
            m_fields->practiceAudio.stop();
            if (levelMusicDisabled()) {
                engine->pauseMusic(kGameplayMusicChannel);
            }
            else if (!m_fields->gameplayPaused) {
                engine->resumeMusic(kGameplayMusicChannel);
            }
            if (engine->m_backgroundMusicChannel) {
                engine->m_backgroundMusicChannel->setMute(false);
            }
            return;
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
            engine->pauseMusic(kGameplayMusicChannel);
            if (engine->m_backgroundMusicChannel) {
                engine->m_backgroundMusicChannel->setMute(true);
            }
            return;
        }

        if (m_fields->restartCooldown > 0.f) return;
        m_fields->restartCooldown = 1.f;
        if (!m_fields->resolvedAudioPath.empty()) {
            if (m_fields->practiceAudio.start(
                m_fields->resolvedAudioPath, engine->getBackgroundMusicVolume(), loopMusic)) {
                engine->pauseMusic(kGameplayMusicChannel);
                if (engine->m_backgroundMusicChannel) {
                    engine->m_backgroundMusicChannel->setMute(true);
                }
            }
        }
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