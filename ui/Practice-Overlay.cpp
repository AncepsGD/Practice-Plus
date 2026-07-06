#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <string>

using namespace geode::prelude;

int &getActiveRespawnPresetIndexState();

class $modify(MyPlayLayer, PlayLayer)
{
    struct Fields
    {
        CCLayer *uiLayer = nullptr;
        CCLabelBMFont *respawnLabel = nullptr;
        float respawnLabelTimer = 0.0f;
        int lastCheckpointCount = 0;
        int lastRespawnPresetIndex = -1;
        bool lastRespawnEnabled = false;
    };

    bool isPracticeMode() const
    {
        return m_isPracticeMode;
    }

    bool isOverlayEnabled() const
    {
        if (auto *mod = Mod::get())
            return mod->getSettingValue<bool>("practice-overlay-enabled");

        return true;
    }

    bool init(GJGameLevel *level, bool useReplay, bool dontCreateObjects)
    {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects))
            return false;

        auto &f = this->m_fields;
        f->lastCheckpointCount = m_checkpointArray ? m_checkpointArray->count() : 0;

        createPracticeHUD();
        updateOverlayVisibility();
        refreshRespawnLabel(true);
        this->schedule(schedule_selector(MyPlayLayer::updateRespawnLabel), 0.0f);

        this->addEventListener(
            KeybindSettingPressedEventV3(Mod::get(), "cycle-respawn-time"),
            [this](Keybind const &, bool down, bool repeat, double)
            {
                if (down && !repeat && isPracticeMode())
                {
                    this->refreshRespawnLabel(true);
                    this->showRespawnLabelForTwoSeconds();
                }

                return ListenerResult::Propagate;
            });

        return true;
    }

    void togglePracticeMode(bool practiceMode)
    {
        PlayLayer::togglePracticeMode(practiceMode);

        auto &f = this->m_fields;
        f->lastCheckpointCount = m_checkpointArray ? m_checkpointArray->count() : 0;

        updateOverlayVisibility();
    }

    void updateOverlayVisibility()
    {
        auto &f = this->m_fields;
        if (!f->uiLayer)
            return;

        bool shouldShow = isPracticeMode() && isOverlayEnabled();
        if (f->uiLayer->isVisible() != shouldShow)
            f->uiLayer->setVisible(shouldShow);
    }

    enum class RespawnPreset : int
    {
        Default = 0,
        Instant = 1,
        Infinite = 2
    };

    bool isRespawnPresetEnabled(RespawnPreset preset) const
    {
        auto *mod = Mod::get();
        if (!mod)
            return true;

        switch (preset)
        {
        case RespawnPreset::Default:
            return mod->getSettingValue<bool>("respawn-preset-def-enabled");
        case RespawnPreset::Instant:
            return mod->getSettingValue<bool>("respawn-preset-zero-enabled");
        case RespawnPreset::Infinite:
            return mod->getSettingValue<bool>("respawn-preset-inf-enabled");
        }

        return true;
    }

    int getActiveRespawnPresetIndex() const
    {
        auto *mod = Mod::get();
        if (!mod)
            return 0;

        int index = getActiveRespawnPresetIndexState();
        if (index < 0 || index > 2 || !isRespawnPresetEnabled(static_cast<RespawnPreset>(index)))
        {
            for (int i = 0; i < 3; ++i)
            {
                if (isRespawnPresetEnabled(static_cast<RespawnPreset>(i)))
                    return i;
            }

            return 0;
        }

        return index;
    }

    std::string getRespawnLabelText() const
    {
        auto *mod = Mod::get();
        if (!mod || !mod->getSettingValue<bool>("practice-respawn-time-enabled"))
            return "OFF";

        switch (getActiveRespawnPresetIndex())
        {
        case static_cast<int>(RespawnPreset::Default):
            return "DEF";
        case static_cast<int>(RespawnPreset::Instant):
            return "0";
        case static_cast<int>(RespawnPreset::Infinite):
            return "INF";
        default:
            return "DEF";
        }
    }

    void refreshRespawnLabel(bool force = false)
    {
        auto &f = this->m_fields;
        if (!f->respawnLabel)
            return;

        auto *mod = Mod::get();
        bool enabled = mod && mod->getSettingValue<bool>("practice-respawn-time-enabled");
        int presetIndex = enabled ? getActiveRespawnPresetIndex() : -1;

        if (!force && f->lastRespawnPresetIndex == presetIndex && f->lastRespawnEnabled == enabled)
            return;

        f->lastRespawnPresetIndex = presetIndex;
        f->lastRespawnEnabled = enabled;
        f->respawnLabel->setString(getRespawnLabelText().c_str());
    }

    void showRespawnLabelForTwoSeconds()
    {
        auto &f = this->m_fields;
        if (!f->respawnLabel)
            return;

        f->respawnLabel->setString(getRespawnLabelText().c_str());
        f->respawnLabelTimer = 2.0f;
    }

    void createPracticeHUD()
    {
        auto &f = this->m_fields;
        if (f->uiLayer)
            return;

        f->uiLayer = CCLayer::create();
        f->uiLayer->setVisible(false);
        this->addChild(f->uiLayer, 9999);

        float startX = 10.f;
        float yPos = 25.f;
        float spacing = 25.f;

        auto addColumn = [&](const char *key, const char *sprite, int index, CCLabelBMFont **labelOut = nullptr)
        {
            float x = startX + spacing * index;

            auto node = CCNode::create();
            node->setPosition({x, yPos});
            f->uiLayer->addChild(node);

            if (auto icon = CCSprite::create(sprite))
            {
                icon->setScale(0.45f);
                icon->setPosition({10.f, 6.f});
                node->addChild(icon);
            }

            auto label = CCLabelBMFont::create(key, "bigFont.fnt");
            label->setScale(0.38f);
            label->setPosition({10.f, -6.f});
            node->addChild(label);

            if (labelOut)
                *labelOut = label;
        };

        addColumn("Z", "add_checkpoint.png"_spr, 0);
        addColumn("X", "remove_checkpoint.png"_spr, 1);
        addColumn("C", "respawn_time_cycle.png"_spr, 2, &f->respawnLabel);
    }

    void updateRespawnLabel(float dt)
    {
        auto &f = this->m_fields;
        if (!f->uiLayer)
            return;

        if (!isPracticeMode())
            return;

        refreshRespawnLabel();

        if (f->respawnLabelTimer > 0.0f)
        {
            f->respawnLabelTimer -= dt;
            if (f->respawnLabelTimer <= 0.0f && f->respawnLabel)
                f->respawnLabel->setString(getRespawnLabelText().c_str());
        }
        else if (f->respawnLabel)
        {
            f->respawnLabel->setString(getRespawnLabelText().c_str());
        }

        int current = m_checkpointArray ? m_checkpointArray->count() : 0;

        if (current != f->lastCheckpointCount)
            f->lastCheckpointCount = current;
    }

    void resetLevel()
    {
        PlayLayer::resetLevel();

        auto &f = this->m_fields;
        f->lastCheckpointCount = m_checkpointArray ? m_checkpointArray->count() : 0;
        updateOverlayVisibility();
    }
};