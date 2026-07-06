#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/loader/SettingV3.hpp>
#include <array>
#include <algorithm>

using namespace geode::prelude;

namespace
{
    int g_activeRespawnPresetIndex = 0;
}

int& getActiveRespawnPresetIndexState()
{
    return g_activeRespawnPresetIndex;
}

class $modify(PracticeRespawnPlayLayer, PlayLayer)
{
    struct Fields
    {
        bool respawnScheduled = false;
        int activePresetIndex = 0;
    };

    enum class RespawnPreset : int
    {
        Default = 0,
        Instant,
        Infinite
    };

    bool isPresetEnabled(RespawnPreset preset)
    {
        switch (preset)
        {
        case RespawnPreset::Default:
            return Mod::get()->getSettingValue<bool>("respawn-preset-def-enabled");
        case RespawnPreset::Instant:
            return Mod::get()->getSettingValue<bool>("respawn-preset-zero-enabled");
        case RespawnPreset::Infinite:
            return Mod::get()->getSettingValue<bool>("respawn-preset-inf-enabled");
        }

        return true;
    }

    RespawnPreset getPreset()
    {
        int index = getActiveRespawnPresetIndexState();
        if (index < 0 || index > 2 || !isPresetEnabled(static_cast<RespawnPreset>(index)))
        {
            for (int i = 0; i < 3; ++i)
            {
                auto preset = static_cast<RespawnPreset>(i);
                if (isPresetEnabled(preset))
                {
                    getActiveRespawnPresetIndexState() = i;
                    m_fields->activePresetIndex = i;
                    return preset;
                }
            }

            getActiveRespawnPresetIndexState() = 0;
            m_fields->activePresetIndex = 0;
            return RespawnPreset::Default;
        }

        m_fields->activePresetIndex = index;
        return static_cast<RespawnPreset>(index);
    }

    float getRespawnTime()
    {
        if (!m_isPracticeMode)
            return 0.0f;

        if (!Mod::get()->getSettingValue<bool>("practice-respawn-time-enabled"))
            return 0.0f;

        switch (getPreset())
        {
        case RespawnPreset::Default:
            return 0.0f;
        case RespawnPreset::Instant:
            return 0.0f;
        case RespawnPreset::Infinite:
            return -1.0f;
        }

        return 0.0f;
    }

    const char *getRespawnUILabel()
    {
        switch (getPreset())
        {
        case RespawnPreset::Default:
            return "DEF";
        case RespawnPreset::Instant:
            return "0";
        case RespawnPreset::Infinite:
            return "INF";
        }

        return "DEF";
    }

    void cycleRespawnTime()
    {
        this->unschedule(schedule_selector(PracticeRespawnPlayLayer::triggerRespawn));
        m_fields->respawnScheduled = false;

        int startIndex = m_fields->activePresetIndex;
        for (int attempt = 0; attempt < 3; ++attempt)
        {
            int index = (startIndex + 1 + attempt) % 3;
            auto preset = static_cast<RespawnPreset>(index);
            if (isPresetEnabled(preset))
            {
                getActiveRespawnPresetIndexState() = index;
                m_fields->activePresetIndex = index;
                Mod::get()->setSettingValue<int>("respawn-active-index", index);
                Mod::get()->setSettingValue<bool>("practice-respawn-time-enabled", true);
                return;
            }
        }

        getActiveRespawnPresetIndexState() = 0;
        m_fields->activePresetIndex = 0;
        Mod::get()->setSettingValue<int>("respawn-active-index", 0);
        Mod::get()->setSettingValue<bool>("practice-respawn-time-enabled", true);
    }

    void scheduleRespawn(float delay)
    {
        if (m_fields->respawnScheduled)
            return;

        m_fields->respawnScheduled = true;

        this->scheduleOnce(
            schedule_selector(PracticeRespawnPlayLayer::triggerRespawn),
            std::max(delay, 0.0f));
    }

    void triggerRespawn(float)
    {
        m_fields->respawnScheduled = false;
        if (m_isPracticeMode && getPreset() == RespawnPreset::Infinite)
            return;

        PlayLayer::delayedResetLevel();
    }

    void delayedResetLevel()
    {
        if (m_isPracticeMode && getPreset() == RespawnPreset::Infinite)
            return;

        PlayLayer::delayedResetLevel();
    }

    void fullReset()
    {
        if (m_isPracticeMode && getPreset() == RespawnPreset::Infinite)
            return;

        PlayLayer::fullReset();
    }

    void resetLevel()
    {
        this->unschedule(schedule_selector(PracticeRespawnPlayLayer::triggerRespawn));
        m_fields->respawnScheduled = false;
        PlayLayer::resetLevel();
    }

    void destroyPlayer(PlayerObject *player, GameObject *object)
    {
        bool respawnFeatureActive = m_isPracticeMode &&
                                    Mod::get()->getSettingValue<bool>("practice-respawn-time-enabled");

        if (!respawnFeatureActive)
        {
            PlayLayer::destroyPlayer(player, object);
            return;
        }

        if (getPreset() == RespawnPreset::Default)
        {
            this->unschedule(schedule_selector(PracticeRespawnPlayLayer::triggerRespawn));
            m_fields->respawnScheduled = false;
            PlayLayer::destroyPlayer(player, object);
            return;
        }

        if (getPreset() == RespawnPreset::Infinite)
        {
            this->unschedule(schedule_selector(PracticeRespawnPlayLayer::triggerRespawn));
            m_fields->respawnScheduled = false;
            PlayLayer::destroyPlayer(player, object);
            return;
        }

        PlayLayer::destroyPlayer(player, object);
        scheduleRespawn(getRespawnTime());
    }

    bool init(GJGameLevel *level, bool useReplay, bool dontCreateObjects)
    {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects))
            return false;

        this->addEventListener(
            KeybindSettingPressedEventV3(Mod::get(), "cycle-respawn-time"),
            [this](Keybind const &, bool down, bool repeat, double)
            {
                if (down && !repeat && m_isPracticeMode)
                    cycleRespawnTime();

                return ListenerResult::Propagate;
            });

        return true;
    }
};