#include <Geode/Geode.hpp>
#include <Geode/modify/LevelCell.hpp>

using namespace geode::prelude;

namespace
{
    constexpr char kBadgeID[] = "blue-checkmark-pml";
    constexpr float kBadgeGap = 16.0f;
    constexpr float kBadgeOffsetY = 29.5f;
    constexpr float kBadgeOffsetX = 29.1f;
    constexpr float kIconScale = 0.45f;
    constexpr float kTextScale = 0.4f;
    constexpr ccColor3B kProgressColor = {0, 200, 255};
    constexpr ccColor3B kShadowColor = {50, 50, 50};
}

class $modify(MyLevelCell, LevelCell)
{
    void loadFromLevel(GJGameLevel *level)
    {
        if (!level)
        {
            LevelCell::loadFromLevel(level);
            return;
        }

        removeExistingBadge(this);
        if (m_mainLayer)
        {
            removeExistingBadge(m_mainLayer);
        }

        LevelCell::loadFromLevel(level);

        bool isNormalBeaten = (level->m_normalPercent == 100);
        int practicePercent = level->m_practicePercent;

        if (auto glm = GameLevelManager::sharedState())
        {
            if (auto savedLevel = glm->getSavedLevel(level->m_levelID))
            {
                if (savedLevel->m_normalPercent == 100)
                {
                    isNormalBeaten = true;
                }
                if (savedLevel->m_practicePercent > practicePercent)
                {
                    practicePercent = savedLevel->m_practicePercent;
                }
            }
        }

        bool isPracticeBeaten = (practicePercent == 100);

        bool inListView = false;
        for (CCNode *p = this; p; p = p->getParent())
        {
            if (p->getID() == "list-view")
            {
                inListView = true;
                break;
            }
        }

        auto checkmark = geode::cast::typeinfo_cast<CCSprite *>(this->getChildByIDRecursive("completed-icon"));

        if (checkmark)
        {
            if (isNormalBeaten)
            {
                checkmark->setVisible(true);
            }
            else if (isPracticeBeaten && !inListView)
            {
                swapToBlueCheckmark(checkmark);
            }
            return;
        }

        if (isNormalBeaten)
        {
            return;
        }

        CCNode *content = nullptr;

        if (isPracticeBeaten)
        {
            if (auto icon = CCSprite::create("practicecomplete.png"_spr))
            {
                icon->setScale(kIconScale);
                content = icon;
            }
        }
        else if (practicePercent > 0 && practicePercent < 100)
        {
            content = createShadowedPercentLabel(practicePercent);
        }

        if (!content || inListView)
        {
            return;
        }

        auto container = m_mainLayer ? static_cast<CCNode *>(m_mainLayer) : static_cast<CCNode *>(this);

        CCLabelBMFont *nameLabel = nullptr;
        CCLabelBMFont *percentLabel = nullptr;

        if (container)
        {
            nameLabel = geode::cast::typeinfo_cast<CCLabelBMFont *>(container->getChildByIDRecursive("level-name"));
            percentLabel = geode::cast::typeinfo_cast<CCLabelBMFont *>(container->getChildByIDRecursive("percentage-label"));
        }

        CCLabelBMFont *targetLabel = (percentLabel && percentLabel->isVisible()) ? percentLabel : nameLabel;

        content->setID(kBadgeID);
        content->setPosition(computeBadgePosition(this, container, targetLabel));

        container->addChild(content, 10);

        content->setScale(1.0f);
    }

    static void removeExistingBadge(CCNode *root)
    {
        if (auto old = root->getChildByID(kBadgeID))
        {
            old->removeFromParentAndCleanup(true);
        }
    }

    static void swapToBlueCheckmark(CCSprite *checkmark)
    {
        auto newTex = CCSprite::create("practicecomplete.png"_spr);
        if (!newTex)
        {
            return;
        }

        auto const originalSize = checkmark->getContentSize();
        checkmark->setTexture(newTex->getTexture());
        checkmark->setTextureRect(CCRect(0, 0, newTex->getContentSize().width, newTex->getContentSize().height));

        auto const newSize = checkmark->getContentSize();
        if (newSize.width > 0.0f && newSize.height > 0.0f)
        {
            checkmark->setScaleX(checkmark->getScaleX() * originalSize.width / newSize.width);
            checkmark->setScaleY(checkmark->getScaleY() * originalSize.height / newSize.height);
        }

        checkmark->setVisible(true);
    }

    static CCNode *createShadowedPercentLabel(int percent)
    {
        auto text = fmt::format("{}%", percent);

        auto shadow = CCLabelBMFont::create(text.c_str(), "bigFont.fnt");
        auto label = CCLabelBMFont::create(text.c_str(), "bigFont.fnt");
        if (!label)
        {
            return nullptr;
        }

        auto const size = label->getContentSize() * kTextScale;

        auto wrapper = CCNode::create();
        wrapper->setContentSize(size);
        wrapper->setAnchorPoint({.467f, .467f});

        if (shadow)
        {
            shadow->setColor(kShadowColor);
            shadow->setOpacity(160);
            shadow->setScale(kTextScale);
            shadow->setAnchorPoint({.467f, .467f});
            shadow->setPosition(size.width / 2.0f + 1.2f, size.height / 2.0f - 1.2f);
            wrapper->addChild(shadow, 0);
        }

        label->setColor(kProgressColor);
        label->setScale(kTextScale);
        label->setAnchorPoint({.467f, .467f});
        label->setPosition(size.width / 2.0f, size.height / 2.0f);
        wrapper->addChild(label, 1);

        return wrapper;
    }

    static CCPoint computeBadgePosition(CCNode *cell, CCNode *container, CCLabelBMFont *targetLabel)
    {
        bool inListView = false;
        for (CCNode *p = cell; p; p = p->getParent())
        {
            auto id = p->getID();
            if (id == "list-view")
            {
                inListView = true;
                break;
            }
        }

        if (targetLabel)
        {
            auto const box = targetLabel->boundingBox();
            auto const localPoint = ccp(box.getMaxX(), box.getMidY());
            auto const worldPoint = targetLabel->getParent()->convertToWorldSpace(localPoint);
            auto const containerPoint = container->convertToNodeSpace(worldPoint);

            auto const x = containerPoint.x + kBadgeGap + (inListView ? kBadgeOffsetX : 0.0f);
            auto const y = containerPoint.y + (inListView ? kBadgeOffsetY : 0.0f);
            return {x, y};
        }

        auto const containerSize = container->getContentSize();
        auto const baseSize = containerSize.width > 0.0f ? containerSize : cell->getContentSize();
        auto const x = baseSize.width - kBadgeGap + (inListView ? kBadgeOffsetX : 0.0f);
        auto const y = baseSize.height + (inListView ? kBadgeOffsetY : 0.0f);
        return {x, y};
    }
};
