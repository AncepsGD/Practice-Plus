#include <Geode/Geode.hpp>
#include <Geode/modify/LevelCell.hpp>

using namespace geode::prelude;

class $modify(MyLevelCell, LevelCell) {
    void loadFromLevel(GJGameLevel* level) {
        // Safe check to avoid operating on null levels
        if (!level) {
            LevelCell::loadFromLevel(level);
            return;
        }

        // Clean up any previously manually added blue checkmarks/labels to prevent TableView cell recycling issues
        if (auto oldCheck = this->getChildByID("blue-checkmark-pml")) {
            oldCheck->removeFromParentAndCleanup(true);
        }
        if (m_mainLayer) {
            if (auto oldCheck = m_mainLayer->getChildByID("blue-checkmark-pml")) {
                oldCheck->removeFromParentAndCleanup(true);
            }
        }

        // Invoke the base implementation to let the game build normal cells
        LevelCell::loadFromLevel(level);

        // NATIVE CHECK FOR PRACTICE COMPLETION:
        // We query the local level database to get the fully populated save state of the level.
        // This is 100% reliable even for search lists and online browser lists!
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

        // Search if the game natively initialized a green checkmark
        CCSprite* checkmark = nullptr;
        this->findCheckmarkSprite(this, checkmark);

        if (checkmark) {
            if (isNormalBeaten) {
                // Keep the default green color if beaten in Normal Mode
                checkmark->setColor({ 255, 255, 255 }); // Clear any color tint
                checkmark->setVisible(true);
            } 
            else if (isPracticeBeaten) {
                // Swap out the texture using the direct Geode raw resource literal loader
                auto newCheck = CCSprite::create("practicecomplete.png"_spr);
                if (newCheck) {
                    checkmark->setDisplayFrame(newCheck->displayFrame());
                    checkmark->setColor({ 255, 255, 255 }); // Clear tint so the true colors shine
                    checkmark->setVisible(true);
                }
            }
        } 
        else if (!isNormalBeaten) {
            // PML NODE CREATION: Decides whether to spawn a Checkmark (100%), a Progress Label (1-99%), or nothing (0%)
            CCNode* pmlNode = nullptr;

            if (isPracticeBeaten) {
                // NATIVE BYPASS: The game skipped checkmark creation because Normal Mode is not 100%.
                // We manually instantiate and draw your custom checkmark using the direct Geode raw resource literal loader!
                auto newCheckmark = CCSprite::create("practicecomplete.png"_spr);
                if (newCheckmark) {
                    newCheckmark->setColor({ 255, 255, 255 }); // Raw texture coloring
                    newCheckmark->setScale(0.17f); // Proportional high-resolution scale
                    newCheckmark->setID("blue-checkmark-pml"); // Unique ID for safe recycling sanitization
                    pmlNode = newCheckmark;
                }
            }
            else if (practicePercent > 0 && practicePercent < 100) {
                // PRACTICE IN PROGRESS: Render a premium gold percentage label next to the title details
                std::string labelText = fmt::format("{}%", practicePercent);
                auto newPercentLabel = CCLabelBMFont::create(labelText.c_str(), "bigFont.fnt");
                if (newPercentLabel) {
                    newPercentLabel->setColor({ 0, 200, 255 }); // Deep premium soft blue color matching checkmark
                    newPercentLabel->setScale(0.5f); // Clean, compact scale
                    newPercentLabel->setID("blue-checkmark-pml"); // Shared recycling cleanup ID
                    pmlNode = newPercentLabel;
                }
            }

            // If we have a node to render (either the checkmark or the percentage progress label)
            if (pmlNode) {
                // Find a safe container inside the cell
                auto container = m_mainLayer ? static_cast<CCNode*>(m_mainLayer) : static_cast<CCNode*>(this);
                
                // Identify dynamic label components via DevTools IDs
                CCLabelBMFont* nameLabel = nullptr;
                CCLabelBMFont* percentLabel = nullptr;
                
                if (container) {
                    nameLabel = static_cast<CCLabelBMFont*>(container->getChildByID("level-name"));
                    percentLabel = static_cast<CCLabelBMFont*>(container->getChildByID("percentage-label"));
                }
                
                // Secondary check: look recursively if getChildByID fails on dynamic layouts
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
                    // Safe calculation using the label's anchor point settings
                    float anchorX = targetLabel->getAnchorPoint().x;
                    float anchorY = targetLabel->getAnchorPoint().y;
                    
                    float labelWidth = targetLabel->getContentSize().width * targetLabel->getScale();
                    float labelHeight = targetLabel->getContentSize().height * targetLabel->getScale();

                    // Position exactly to the right of the visible text boundary + dynamic spacing offset (18.0f)
                    x = targetLabel->getPositionX() + (1.0f - anchorX) * labelWidth + 18.0f;
                    
                    // Align vertically center with the target label
                    y = targetLabel->getPositionY() + (0.5f - anchorY) * labelHeight;
                } else {
                    // Safe top-right corner fallback if no labels are identified
                    float cellWidth = this->getContentSize().width;
                    float cellHeight = this->getContentSize().height;
                    
                    if (cellWidth < 100.0f) cellWidth = 356.f;
                    if (cellHeight < 20.0f) cellHeight = 56.0f;

                    x = cellWidth - 18.0f; 
                    y = cellHeight;
                }
                
                pmlNode->setPosition({ x, y });
                
                // Add the node with a high Z-order to guarantee it renders on top of the backgrounds
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

        // Fetch the true sprite frame reference from the cache
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
            // Recurse down the tree
            findCheckmarkSprite(child, checkmarkOut);
        }
    }

    // Recursively look for labels matching specified Geode DevTools ID keys
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