#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/loader/SettingV3.hpp>
#include <array>
#include <algorithm>

using namespace geode::prelude;

class $modify(PracticeRespawnPlayLayer, PlayLayer)
{
    struct Fields
    {
        bool respawnScheduled = false;
    };

    enum class RespawnPreset : int
    {
        Zero = 0,
        PointOne,
        Half,
        One,
        Infinite
    };

    RespawnPreset getPreset()
    {
        int index = Mod::get()->getSettingValue<int>("respawn-active-index");
        index = std::clamp(index, 0, 4);
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
        case RespawnPreset::Zero:
            return 0.0f;
        case RespawnPreset::PointOne:
            return 0.1f;
        case RespawnPreset::Half:
            return 0.5f;
        case RespawnPreset::One:
            return 1.0f;
        case RespawnPreset::Infinite:
            return 32767.0f;
        }

        return 0.0f;
    }

    const char *getRespawnUILabel()
    {
        switch (getPreset())
        {
        case RespawnPreset::Zero:
            return "0s";
        case RespawnPreset::PointOne:
            return "0.1s";
        case RespawnPreset::Half:
            return "0.5s";
        case RespawnPreset::One:
            return "1s";
        case RespawnPreset::Infinite:
            return "INF";
        }

        return "0.5s";
    }

    void cycleRespawnTime()
    {
        int index = Mod::get()->getSettingValue<int>("respawn-active-index");
        index = (index + 1) % 5;

        Mod::get()->setSettingValue<int>("respawn-active-index", index);
        Mod::get()->setSettingValue<bool>("practice-respawn-time-enabled", true);
    }

    void scheduleRespawn(float delay)
    {
        if (m_fields->respawnScheduled)
            return;

        m_fields->respawnScheduled = true;

        this->scheduleOnce(
            schedule_selector(PracticeRespawnPlayLayer::triggerRespawn),
            delay);
    }

    void triggerRespawn(float)
    {
        m_fields->respawnScheduled = false;
        this->delayedResetLevel();
    }

    void destroyPlayer(PlayerObject *player, GameObject *object)
    {
        PlayLayer::destroyPlayer(player, object);

        if (!m_isPracticeMode)
            return;

        float delay = getRespawnTime();

        if (delay <= 0.0f)
        {
            this->delayedResetLevel();
            return;
        }

        scheduleRespawn(delay);
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
