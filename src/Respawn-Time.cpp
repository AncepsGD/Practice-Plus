#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>

using namespace geode::prelude;

class $modify(PracticeRespawnPlayLayer, PlayLayer)
{
    struct Fields
    {
        bool respawnScheduled = false;
    };

    static bool isEnabled()
    {
        if (auto *mod = Mod::get())
            return mod->getSettingValue<bool>("practice-respawn-time-enabled");
        return false;
    }

    static float getRespawnDelay()
    {
        if (auto *mod = Mod::get())
        {
            return std::clamp(
                static_cast<float>(mod->getSettingValue<double>("practice-respawn-time")),
                0.0f,
                120.0f);
        }
        return 1.0f;
    }

    void triggerRespawn(float)
    {
        m_fields->respawnScheduled = false;
        this->delayedResetLevel();
    }

    void destroyPlayer(PlayerObject *player, GameObject *object)
    {
        PlayLayer::destroyPlayer(player, object);

        if (!isEnabled() || !m_isPracticeMode)
            return;

        float delay = getRespawnDelay();

        if (delay <= 0.0f)
        {
            this->delayedResetLevel();
            return;
        }

        if (m_fields->respawnScheduled)
            return;

        m_fields->respawnScheduled = true;

        this->scheduleOnce(
            schedule_selector(PracticeRespawnPlayLayer::triggerRespawn),
            delay);
    }
};