#include <Geode/Geode.hpp>
#include <Geode/modify/LevelCell.hpp>

using namespace geode::prelude;

class $modify(MyLevelCell, LevelCell)
{
    void loadFromLevel(GJGameLevel *level)
    {
        if (!level)
        {
            LevelCell::loadFromLevel(level);
            return;
        }

        if (auto oldCheck = this->getChildByID("blue-checkmark-pml"))
        {
            oldCheck->removeFromParentAndCleanup(true);
        }
        if (m_mainLayer)
        {
            if (auto oldCheck = m_mainLayer->getChildByID("blue-checkmark-pml"))
            {
                oldCheck->removeFromParentAndCleanup(true);
            }
        }

        LevelCell::loadFromLevel(level);

        bool isNormalBeaten = (level->m_normalPercent == 100);
        int practicePercent = level->m_practicePercent;

        auto glm = GameLevelManager::sharedState();
        if (glm)
        {

            auto savedLevel = glm->getSavedLevel(level->m_levelID);
            if (savedLevel)
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

        auto checkmark = typeinfo_cast<CCSprite *>(this->getChildByIDRecursive("completed-icon"));

        if (checkmark)
        {
            if (isNormalBeaten)
            {
                checkmark->setVisible(true);
            }
            else if (isPracticeBeaten)
            {
                auto newCheck = CCSprite::create("practicecomplete.png"_spr);
                if (newCheck)
                {
                    checkmark->setTexture(newCheck->getTexture());
                    checkmark->setTextureRect(CCRect(0, 0, newCheck->getContentSize().width, newCheck->getContentSize().height));
                    checkmark->setVisible(true);
                }
            }
        }
        else if (!isNormalBeaten)
        {
            CCNode *pmlNode = nullptr;

            if (isPracticeBeaten)
            {
                auto newCheckmark = CCSprite::create("practicecomplete.png"_spr);
                if (newCheckmark)
                {
                    newCheckmark->setScale(0.17f);
                    newCheckmark->setID("blue-checkmark-pml");
                    pmlNode = newCheckmark;
                }
            }
            else if (practicePercent > 0 && practicePercent < 100)
            {
                std::string labelText = fmt::format("{}%", practicePercent);
                auto newPercentLabel = CCLabelBMFont::create(labelText.c_str(), "bigFont.fnt");
                if (newPercentLabel)
                {
                    newPercentLabel->setColor({0, 200, 255});
                    newPercentLabel->setScale(0.5f);
                    newPercentLabel->setID("blue-checkmark-pml");
                    pmlNode = newPercentLabel;
                }
            }

            if (pmlNode)
            {
                auto container = m_mainLayer ? static_cast<CCNode *>(m_mainLayer) : static_cast<CCNode *>(this);

                CCLabelBMFont *nameLabel = nullptr;
                CCLabelBMFont *percentLabel = nullptr;

                if (container)
                {
                    nameLabel = typeinfo_cast<CCLabelBMFont *>(container->getChildByIDRecursive("level-name"));
                    percentLabel = typeinfo_cast<CCLabelBMFont *>(container->getChildByIDRecursive("percentage-label"));
                }

                CCLabelBMFont *targetLabel = nullptr;
                if (percentLabel && percentLabel->isVisible())
                {
                    targetLabel = percentLabel;
                }
                else if (nameLabel)
                {
                    targetLabel = nameLabel;
                }

                auto position = [this, targetLabel]() -> CCPoint
                {
                    if (targetLabel)
                    {
                        auto const anchor = targetLabel->getAnchorPoint();
                        auto const size = targetLabel->getContentSize() * targetLabel->getScale();
                        return {
                            targetLabel->getPositionX() + (1.0f - anchor.x) * size.width + 18.0f,
                            targetLabel->getPositionY() + (0.5f - anchor.y) * size.height,
                        };
                    }

                    auto const cellSize = this->getContentSize();
                    auto const fallback = this->getParent() ? this->getParent()->getContentSize() : CCSize{};
                    auto const baseSize = cellSize.width > 0.0f ? cellSize : fallback;
                    return {
                        baseSize.width - 18.0f,
                        baseSize.height,
                    };
                }();

                pmlNode->setPosition(position);
                container->addChild(pmlNode, 10);
            }
        }
    }
};