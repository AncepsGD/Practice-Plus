#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <string>

using namespace geode::prelude;

class $modify(MyPlayLayer, PlayLayer)
{
    struct Fields
    {
        CCLayer *uiLayer = nullptr;
        CCLabelBMFont *respawnLabel = nullptr;
        float respawnLabelTimer = 0.0f;
        int lastCheckpointCount = 0;
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

        this->addEventListener(
            KeybindSettingPressedEventV3(Mod::get(), "cycle-respawn-time"),
            [this](Keybind const &, bool down, bool repeat, double)
            {
                if (down && !repeat && isPracticeMode())
                    this->showRespawnLabelForTwoSeconds();

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

    std::string getRespawnLabelText()
    {
        int index = Mod::get()->getSettingValue<int>("respawn-active-index");

        switch (index)
        {
        case 0:
            return "0s";
        case 1:
            return "0.5s";
        default:
            return "0.5s";
        }
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

    void update(float dt)
    {
        PlayLayer::update(dt);

        auto &f = this->m_fields;
        if (!f->uiLayer)
            return;

        updateOverlayVisibility();

        if (!isPracticeMode())
            return;

        if (f->respawnLabelTimer > 0.0f)
        {
            f->respawnLabelTimer -= dt;
            if (f->respawnLabelTimer <= 0.0f && f->respawnLabel)
                f->respawnLabel->setString("C");
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