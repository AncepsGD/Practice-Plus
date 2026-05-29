#include <Geode/Geode.hpp>
#include <Geode/modify/LevelCell.hpp>

using namespace geode::prelude;

class $modify(MyLevelCell, LevelCell) {
    void loadFromLevel(GJGameLevel* level) {
        if (!level) {
            LevelCell::loadFromLevel(level);
            return;
        }

        if (auto oldCheck = this->getChildByID("blue-checkmark-pml")) {
            oldCheck->removeFromParentAndCleanup(true);
        }
        if (m_mainLayer) {
            if (auto oldCheck = m_mainLayer->getChildByID("blue-checkmark-pml")) {
                oldCheck->removeFromParentAndCleanup(true);
            }
        }

        LevelCell::loadFromLevel(level);

        bool isNormalBeaten = (level->m_normalPercent == 100);
        int practicePercent = level->m_practicePercent;
        
        auto glm = GameLevelManager::sharedState();
        if (glm) {

            auto savedLevel = glm->getSavedLevel(level->m_levelID);
            if (savedLevel) {
                if (savedLevel->m_normalPercent == 100) {
                    isNormalBeaten = true;
                }
                if (savedLevel->m_practicePercent > practicePercent) {
                    practicePercent = savedLevel->m_practicePercent;
                }
            }
        }

        bool isPracticeBeaten = (practicePercent == 100);

        auto checkmark = typeinfo_cast<CCSprite*>(this->getChildByIDRecursive("completed-icon"));

        if (checkmark) {
            if (isNormalBeaten) {
                checkmark->setVisible(true);
            } else if (isPracticeBeaten) {
                auto newCheck = CCSprite::create("practicecomplete.png"_spr);
                if (newCheck) {
                    checkmark->setTexture(newCheck->getTexture());
                    checkmark->setTextureRect(CCRect(0, 0, newCheck->getContentSize().width, newCheck->getContentSize().height));
                    checkmark->setVisible(true);
                }
            }
        } else if (!isNormalBeaten) {
            CCNode* pmlNode = nullptr;

            if (isPracticeBeaten) {
                auto newCheckmark = CCSprite::create("practicecomplete.png"_spr);
                if (newCheckmark) {
                    newCheckmark->setScale(0.17f);
                    newCheckmark->setID("blue-checkmark-pml");
                    pmlNode = newCheckmark;
                }
            } else if (practicePercent > 0 && practicePercent < 100) {
                std::string labelText = fmt::format("{}%", practicePercent);
                auto newPercentLabel = CCLabelBMFont::create(labelText.c_str(), "bigFont.fnt");
                if (newPercentLabel) {
                    newPercentLabel->setColor({ 0, 200, 255 });
                    newPercentLabel->setScale(0.5f);
                    newPercentLabel->setID("blue-checkmark-pml");
                    pmlNode = newPercentLabel;
                }
            }

            if (pmlNode) {
                auto container = m_mainLayer ? static_cast<CCNode*>(m_mainLayer) : static_cast<CCNode*>(this);
                
                CCLabelBMFont* nameLabel = nullptr;
                CCLabelBMFont* percentLabel = nullptr;

                if (container) {
                    nameLabel = typeinfo_cast<CCLabelBMFont*>(container->getChildByIDRecursive("level-name"));
                    percentLabel = typeinfo_cast<CCLabelBMFont*>(container->getChildByIDRecursive("percentage-label"));
                }

                CCLabelBMFont* targetLabel = nullptr;
                if (percentLabel && percentLabel->isVisible()) {
                    targetLabel = percentLabel;
                } else if (nameLabel) {
                    targetLabel = nameLabel;
                }

                float x = 0.0f;
                float y = 0.0f;

                if (targetLabel) {
                    float anchorX = targetLabel->getAnchorPoint().x;
                    float anchorY = targetLabel->getAnchorPoint().y;
                    float labelWidth = targetLabel->getContentSize().width * targetLabel->getScale();
                    float labelHeight = targetLabel->getContentSize().height * targetLabel->getScale();
                    
                    x = targetLabel->getPositionX() + (1.0f - anchorX) * labelWidth + 18.0f;
                    y = targetLabel->getPositionY() + (0.5f - anchorY) * labelHeight;
                } else {
                    float cellWidth = this->getContentSize().width;
                    float cellHeight = this->getContentSize().height;
                    if (cellWidth < 100.0f) cellWidth = 356.f;
                    if (cellHeight < 20.0f) cellHeight = 56.0f;
                    x = cellWidth - 18.0f;
                    y = cellHeight;
                }

                pmlNode->setPosition({ x, y });
                container->addChild(pmlNode, 10);
            }
        }
    }
};