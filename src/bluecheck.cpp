#include <Geode/Geode.hpp>
#include <Geode/modify/LevelCell.hpp>

using namespace geode::prelude;

class $modify(MyLevelCell, LevelCell) {
    void loadFromLevel(GJGameLevel* level) {
        // Safe check to ensure check logic is only on levels
        if (!level) {
            LevelCell::loadFromLevel(level);
            return;
        }

        // Clean up previous blue checkmarks/labels
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
            auto savedLevel = glm->getSavedLevel(level->m_levelID.value());
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

        // Search if level has green checkmark
        CCSprite* checkmark = nullptr;
        this->findCheckmarkSprite(this, checkmark);

        if (checkmark) {
            if (isNormalBeaten) {
                // Keep green color if beaten in Normal Mode
                checkmark->setColor({ 255, 255, 255 }); // Clear any color tint
                checkmark->setVisible(true);
            } 
            else if (isPracticeBeaten) {
                // Use custom practice check texture
                auto newCheck = CCSprite::create("practicecomplete.png"_spr);
                if (newCheck) {
                    checkmark->setDisplayFrame(newCheck->displayFrame());
                    checkmark->setColor({ 255, 255, 255 }); // Clear tint so the true colors shine
                    checkmark->setVisible(true);
                }
            }
        } 
        else if (!isNormalBeaten) {
            // Decide whether to spawn a Checkmark (100%), Progress Label (1-99%), or nothing (0%)
            CCNode* pmlNode = nullptr;

            if (isPracticeBeaten) {
                // Game skipped green checkmark because Normal Mode is not 100%
                auto newCheckmark = CCSprite::create("practicecomplete.png"_spr);
                if (newCheckmark) {
                    newCheckmark->setColor({ 255, 255, 255 }); // Raw texture coloring
                    newCheckmark->setScale(0.17f); // Proportional high-resolution scale
                    newCheckmark->setID("blue-checkmark-pml"); // Unique ID for safe recycling sanitization
                    pmlNode = newCheckmark;
                }
            }
            else if (practicePercent > 0 && practicePercent < 100) {
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
                
                // Identify dynamic label components via DevTools
                CCLabelBMFont* nameLabel = nullptr;
                CCLabelBMFont* percentLabel = nullptr;
                
                if (container) {
                    nameLabel = static_cast<CCLabelBMFont*>(container->getChildByID("level-name"));
                    percentLabel = static_cast<CCLabelBMFont*>(container->getChildByID("percentage-label"));
                }
                
                // Look recursively if getChildByID happens to fail on dynamic layouts
                if (!nameLabel) {
                    this->findLabelByID(container, "level-name", nameLabel);
                }
                if (!percentLabel) {
                    this->findLabelByID(container, "percentage-label", percentLabel);
                }

                // Decide which label to align next to
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
                    // Safe fallback if no labels are identified
                    float cellWidth = this->getContentSize().width;
                    float cellHeight = this->getContentSize().height;
                    
                    if (cellWidth < 100.0f) cellWidth = 356.f;
                    if (cellHeight < 20.0f) cellHeight = 56.0f;

                    x = cellWidth - 18.0f; 
                    y = cellHeight;
                }
                
                pmlNode->setPosition({ x, y });
                
                // Use high Z-order to guarantee it renders on top of UI
                container->addChild(pmlNode, 10);
            }
        }
    }

private:
    // Helper function to find the checkmark sprite inside LevelCell children recursively
    void findCheckmarkSprite(cocos2d::CCNode* parent, cocos2d::CCSprite*& checkmarkOut) {
        if (!parent || checkmarkOut) return;

        auto children = parent->getChildren();
        if (!children) return;

        auto targetFrame = CCSpriteFrameCache::sharedSpriteFrameCache()->spriteFrameByName("GJ_completesIcon_001.png");

        for (int i = 0; i < children->count(); ++i) {
            auto child = static_cast<cocos2d::CCNode*>(children->objectAtIndex(i));
            if (!child) continue;

            if (auto sprite = dynamic_cast<CCSprite*>(child)) {
                // Safely compare the sprite's active display frame with the cached checkmark frame
                if (targetFrame && sprite->displayFrame() == targetFrame) {
                    // Avoid confusing with buttons or other UI elements
                    if (!dynamic_cast<CCMenuItem*>(sprite->getParent())) {
                        checkmarkOut = sprite;
                        return;
                    }
                }
            }
            findCheckmarkSprite(child, checkmarkOut);
        }
    }

    // Recursively look for labels matching Geode DevTools IDs
    void findLabelByID(cocos2d::CCNode* parent, const char* labelID, CCLabelBMFont*& labelOut) {
        if (!parent || labelOut) return;

        if (auto found = dynamic_cast<CCLabelBMFont*>(parent->getChildByID(labelID))) {
            labelOut = found;
            return;
        }

        auto children = parent->getChildren();
        if (!children) return;

        for (int i = 0; i < children->count(); ++i) {
            auto child = static_cast<cocos2d::CCNode*>(children->objectAtIndex(i));
            if (!child) continue;

            findLabelByID(child, labelID, labelOut);
        }
    }
};