#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <algorithm>
#include <chrono>
#include <string>
#include <vector>

using namespace geode::prelude;

int &getActiveRespawnPresetIndexState();

struct OverlayColumn
{
    CCNode *node = nullptr;
    CCSprite *icon = nullptr;
    CCLabelBMFont *label = nullptr;
};

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

        CCLabelBMFont *sessionLabel = nullptr;
        CCNode *sessionColumnNode = nullptr;
        float sessionColumnBaseX = 0.0f;
        std::chrono::steady_clock::time_point lastResumeTime;
        double accumulatedSeconds = 0.0;
        bool sessionRunning = false;
        bool sessionPaused = false;

        CCLabelBMFont *attemptsLabel = nullptr;
        int attemptCount = 0;

        CCLabelBMFont *positionLabel = nullptr;
        std::vector<OverlayColumn> overlayColumns;
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

    void startSessionTimer()
    {
        auto &f = this->m_fields;
        f->lastResumeTime = std::chrono::steady_clock::now();
        f->sessionPaused = false;
    }

    void pauseSessionTimer(bool byDeath = false)
    {
        auto &f = this->m_fields;
        if (!f->sessionRunning)
            return;

        if (!f->sessionPaused)
        {
            auto now = std::chrono::steady_clock::now();
            auto delta = std::chrono::duration<double>(now - f->lastResumeTime).count();
            f->accumulatedSeconds += delta;
            f->sessionPaused = true;
        }
    }

    void resumeSessionTimer(bool byDeath = false)
    {
        auto &f = this->m_fields;
        if (!f->sessionRunning || !f->sessionPaused)
            return;

        startSessionTimer();
    }

    void syncSessionTimerWithPlayerState()
    {
        auto &f = this->m_fields;
        if (!f->sessionRunning)
            return;

        if (m_isPaused && !f->sessionPaused)
            pauseSessionTimer();
        else if (!m_isPaused && f->sessionPaused)
            resumeSessionTimer();
    }

    void updateSessionLabel()
    {
        auto &f = this->m_fields;
        if (!f->sessionRunning || f->sessionPaused || !f->sessionLabel)
            return;

        auto now = std::chrono::steady_clock::now();
        auto liveDelta = std::chrono::duration<double>(now - f->lastResumeTime).count();
        double total = f->accumulatedSeconds + liveDelta;

        int hours = (int)(total / 3600);
        int minutes = ((int)total % 3600) / 60;
        int seconds = (int)total % 60;

        std::string label;
        if (hours > 0)
            label += std::to_string(hours) + "h";
        if (minutes > 0)
        {
            if (!label.empty())
                label += " ";
            label += std::to_string(minutes) + "m";
        }
        if (seconds > 0 || label.empty())
        {
            if (!label.empty())
                label += " ";
            label += std::to_string(seconds) + "s";
        }

        f->sessionLabel->setString(label.c_str());
        updateOverlayLayout();
    }

    void updateAttemptsLabel()
    {
        auto &f = this->m_fields;
        if (!f->attemptsLabel)
            return;

        f->attemptsLabel->setString(std::to_string(f->attemptCount).c_str());
        updateOverlayLayout();
    }

    bool isFrameDisplayEnabled() const
    {
        if (auto *mod = Mod::get())
            return mod->getSettingValue<bool>("practice-overlay-frame");

        return false;
    }

    void updatePositionLabel()
    {
        auto &f = this->m_fields;
        if (!f->positionLabel || !m_player1)
            return;

        int value = isFrameDisplayEnabled()
            ? m_tickIndex
            : static_cast<int>(m_player1->getPositionX());
        f->positionLabel->setString(std::to_string(value).c_str());
        updateOverlayLayout();
    }

    void updateOverlayLayout()
    {
        auto &f = this->m_fields;
        if (f->overlayColumns.empty())
            return;

        auto *director = CCDirector::sharedDirector();
        if (!director)
            return;

        const float leftInset = 10.0f;
        const float spacing = 12.0f;
        const float baseLabelScale = 0.38f;
        const float baseIconScale = 0.45f;
        const float minScale = 0.55f;

        float availableWidth = std::max(120.0f, director->getWinSize().width - leftInset - 10.0f);
        std::vector<float> columnWidths;
        float totalWidth = 0.0f;

        for (auto &column : f->overlayColumns)
        {
            if (!column.node || !column.label || !column.icon)
                continue;

            float labelWidth = column.label->getContentSize().width * baseLabelScale;
            float iconWidth = column.icon->getContentSize().width * baseIconScale;
            float width = std::max(26.0f, std::max(labelWidth, iconWidth) + 10.0f);
            columnWidths.push_back(width);
            totalWidth += width;
        }

        if (columnWidths.size() > 1)
            totalWidth += spacing * (static_cast<float>(columnWidths.size()) - 1.0f);

        float scale = 1.0f;
        if (totalWidth > availableWidth)
            scale = std::max(minScale, availableWidth / totalWidth);

        float currentX = leftInset;
        size_t visibleIndex = 0;
        for (auto &column : f->overlayColumns)
        {
            if (!column.node || !column.label || !column.icon)
                continue;

            float width = columnWidths.empty() ? 26.0f : columnWidths[visibleIndex] * scale;
            column.node->setPositionX(currentX);
            column.label->setScale(baseLabelScale * scale);
            column.icon->setScale(baseIconScale * scale);
            currentX += width + spacing * scale;
            visibleIndex += 1;
        }
    }

    bool init(GJGameLevel *level, bool useReplay, bool dontCreateObjects)
    {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects))
            return false;

        auto &f = this->m_fields;
        f->lastCheckpointCount = m_checkpointArray ? m_checkpointArray->count() : 0;

        f->accumulatedSeconds = 0.0;
        f->sessionRunning = true;
        f->sessionPaused = false;
        f->attemptCount = 0;
        startSessionTimer();

        createPracticeHUD();
        updateOverlayVisibility();
        refreshRespawnLabel(true);
        updateAttemptsLabel();
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

        auto addColumn = [&](const char *key, const char *sprite, float x, CCLabelBMFont **labelOut = nullptr) -> CCNode *
        {
            auto node = CCNode::create();
            node->setPosition({x, yPos});
            f->uiLayer->addChild(node);

            CCSprite *icon = nullptr;
            if (auto spriteNode = CCSprite::create(sprite))
            {
                spriteNode->setScale(0.45f);
                spriteNode->setPosition({10.f, 6.f});
                node->addChild(spriteNode);
                icon = spriteNode;
            }

            auto label = CCLabelBMFont::create(key, "bigFont.fnt");
            label->setScale(0.38f);
            label->setPosition({10.f, -6.f});
            node->addChild(label);

            if (labelOut)
                *labelOut = label;

            f->overlayColumns.push_back({node, icon, label});
            return node;
        };

        addColumn("Z", "add_checkpoint.png"_spr, startX);
        addColumn("X", "remove_checkpoint.png"_spr, startX);
        addColumn("C", "respawn_time_cycle.png"_spr, startX, &f->respawnLabel);
        addColumn("0", "attempt_count.png"_spr, startX, &f->attemptsLabel);
        f->sessionColumnBaseX = startX;
        f->sessionColumnNode = addColumn("0h 0m 0s", "time.png"_spr, startX, &f->sessionLabel);
        addColumn("0", "position_x.png"_spr, startX, &f->positionLabel);

        updateOverlayLayout();
    }

    void updateRespawnLabel(float dt)
    {
        auto &f = this->m_fields;
        if (!f->uiLayer)
            return;

        if (!isPracticeMode())
            return;

        refreshRespawnLabel();
        syncSessionTimerWithPlayerState();
        updateSessionLabel();
        updatePositionLabel();

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

    void destroyPlayer(PlayerObject *player, GameObject *object)
    {
        PlayLayer::destroyPlayer(player, object);
    }

    void pauseGame(bool unfocused)
    {
        pauseSessionTimer();
        PlayLayer::pauseGame(unfocused);
    }

    void resume()
    {
        PlayLayer::resume();
        resumeSessionTimer();
    }

    void levelComplete()
    {
        pauseSessionTimer();
        auto &f = this->m_fields;
        f->sessionRunning = false;
        PlayLayer::levelComplete();
    }

    void onQuit()
    {
        pauseSessionTimer();
        auto &f = this->m_fields;
        f->sessionRunning = false;
        PlayLayer::onQuit();
    }

    void resetLevel()
    {
        PlayLayer::resetLevel();

        auto &f = this->m_fields;
        f->lastCheckpointCount = m_checkpointArray ? m_checkpointArray->count() : 0;
        f->attemptCount += 1;
        updateAttemptsLabel();
        updateOverlayVisibility();
    }
};