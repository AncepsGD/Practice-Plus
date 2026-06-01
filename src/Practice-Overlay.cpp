#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>

using namespace geode::prelude;

class $modify(MyPlayLayer, PlayLayer)
{
    struct Fields
    {
        CCLayer *uiLayer = nullptr;
        CCLayerColor *flashZ = nullptr;
        CCLayerColor *flashX = nullptr;
        CCLayerColor *flashC = nullptr;
        int lastCheckpointCount = 0;
    };

    bool init(GJGameLevel *level, bool useReplay, bool dontCreateObjects)
    {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects))
            return false;

        auto f = this->getField<Fields>();
        f->lastCheckpointCount = 0;
        return true;
    }

    void triggerFlash(CCLayerColor *flash)
    {
        if (!flash)
            return;

        flash->stopAllActions();
        flash->setOpacity(0);

        flash->runAction(CCSequence::create(
            CCFadeTo::create(0.05f, 140),
            CCFadeTo::create(0.15f, 0),
            nullptr));
    }

    void createPracticeHUD()
    {
        auto f = this->getField<Fields>();
        if (f->uiLayer)
            return;

        f->uiLayer = CCLayer::create();
        this->addChild(f->uiLayer, 9999);

        float startX = 10.f;
        float yPos = 25.f;
        float spacing = 25.f;

        auto makeFlash = [&](float x)
        {
            auto flash = CCLayerColor::create({255, 255, 0, 0});
            flash->setContentSize({22.f, 22.f});
            flash->setPosition({x, yPos - 12.f});
            f->uiLayer->addChild(flash);
            return flash;
        };

        auto addColumn = [&](const char *key, const char *sprite, int index, CCLayerColor *&out)
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

            out = makeFlash(x + 10.f);
        };

        addColumn("Z", "add_checkpoint.png"_spr, 0, f->flashZ);
        addColumn("X", "remove_checkpoint.png"_spr, 1, f->flashX);
        addColumn("C", "respawn_time_cycle.png"_spr, 2, f->flashC);
    }

    void update(float dt)
    {
        PlayLayer::update(dt);

        auto f = this->getField<Fields>();
        if (!f->uiLayer)
            return;

        int current = m_checkpointArray ? m_checkpointArray->count() : 0;

        if (current != f->lastCheckpointCount)
        {
            if (current > f->lastCheckpointCount)
                triggerFlash(f->flashZ);
            else
                triggerFlash(f->flashX);

            f->lastCheckpointCount = current;
        }
    }

    void resetLevel()
    {
        PlayLayer::resetLevel();

        auto f = this->getField<Fields>();

        if (f->uiLayer)
            triggerFlash(f->flashC);
        else
            createPracticeHUD();

        f->lastCheckpointCount = 0;
    }
};
