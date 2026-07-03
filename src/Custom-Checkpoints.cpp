#include <algorithm>
#include <filesystem>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>

#include "CheckpointHelpers.hpp"

using namespace geode::prelude;

class $modify(CustomCheckpointsPlayLayer, PlayLayer)
{
    struct Fields
    {
        int placementIndex = 0;
    };

    static inline std::unordered_map<int, std::pair<int, cocos2d::CCNode *>> s_overlayMap;
    static inline int s_nextOverlayId = 1;
    static inline std::unordered_map<CheckpointObject *, int> s_cpToOid;

    struct Settings
    {
        bool enabled = false;
        bool outerColor = false;
        bool innerColor = false;
        bool fade = false;
        bool fadeIn = false;

        GLubyte opacity = 255;
        GLubyte fadeStep = 30;
        GLubyte fadeMin = 20;
        float fadeInDuration = 0.4f;

        std::filesystem::path checkpointImage;
    };

    static Settings getSettings()
    {
        Settings s;

        if (auto *mod = Mod::get())
        {
            s.enabled = mod->getSettingValue<bool>("custom-checkpoints-enabled");
            s.outerColor = mod->getSettingValue<bool>("checkpoint-outer-color-enabled");
            s.innerColor = mod->getSettingValue<bool>("checkpoint-inner-color-enabled");
            s.fade = mod->getSettingValue<bool>("checkpoint-fade-enabled");
            s.fadeIn = mod->getSettingValue<bool>("checkpoint-fadein-enabled");

            s.opacity = static_cast<GLubyte>(
                std::clamp<int64_t>(mod->getSettingValue<int64_t>("checkpoint-opacity"), 0, 255));

            s.fadeStep = static_cast<GLubyte>(
                std::clamp<int64_t>(mod->getSettingValue<int64_t>("checkpoint-fade-step"), 0, 255));

            s.fadeMin = static_cast<GLubyte>(
                std::clamp<int64_t>(mod->getSettingValue<int64_t>("checkpoint-fade-min"), 0, 255));

            s.fadeInDuration = std::max(
                0.05f,
                static_cast<float>(mod->getSettingValue<double>("checkpoint-fadein-duration")));

            s.checkpointImage = mod->getSettingValue<std::filesystem::path>("checkpoint-image");
        }

        return s;
    }

    static unsigned getNodeOpacity(cocos2d::CCNode *node)
    {
        if (!node)
            return 255u;

        if (auto *rgba = typeinfo_cast<cocos2d::CCRGBAProtocol *>(node))
            return static_cast<unsigned>(rgba->getOpacity());

        return 255u;
    }

    static bool isOurOverlayNode(cocos2d::CCNode *node)
    {
        if (!node)
            return false;

        auto const &id = node->getID();
        return id == checkpoint_mod::OuterId || id == checkpoint_mod::InnerId;
    }

    static void hideRenderableNode(cocos2d::CCNode *node)
    {
        if (!node || isOurOverlayNode(node))
            return;

        node->stopAllActions();
        node->unscheduleAllSelectors();

        if (auto *rgba = typeinfo_cast<cocos2d::CCRGBAProtocol *>(node))
            rgba->setOpacity(0);

        node->setVisible(false);
    }

    static void setOpacityAndVisibility(cocos2d::CCNode *node, GLubyte opacity)
    {
        if (!node)
            return;

        if (auto *rgba = typeinfo_cast<cocos2d::CCRGBAProtocol *>(node))
            rgba->setOpacity(opacity);

        node->setVisible(opacity != 0);
    }

    static void updateOverlayOpacity(cocos2d::CCNode *node, GLubyte opacity)
    {
        if (!node)
            return;

        setOpacityAndVisibility(node, opacity);
        if (auto *children = node->getChildren())
        {
            for (size_t i = 0; i < children->count(); ++i)
            {
                auto *child = static_cast<cocos2d::CCNode *>(children->objectAtIndex(i));
                updateOverlayOpacity(child, opacity);
            }
        }
    }

    static void updateNodeVisibilityForOpacity(cocos2d::CCNode *node)
    {
        if (!node)
            return;

        if (auto *rgba = typeinfo_cast<cocos2d::CCRGBAProtocol *>(node))
        {
            auto opacity = rgba->getOpacity();
            node->setVisible(opacity != 0);
        }

        if (auto *children = node->getChildren())
        {
            for (size_t i = 0; i < children->count(); ++i)
            {
                auto *child = static_cast<cocos2d::CCNode *>(children->objectAtIndex(i));
                updateNodeVisibilityForOpacity(child);
            }
        }
    }

    static void hideNodeRecursive(cocos2d::CCNode *root)
    {
        if (!root || isOurOverlayNode(root))
            return;

        hideRenderableNode(root);

        auto *children = root->getChildren();
        if (!children)
            return;

        for (size_t i = 0; i < children->count(); ++i)
        {
            auto *node = static_cast<cocos2d::CCNode *>(children->objectAtIndex(i));
            hideNodeRecursive(node);
        }
    }

    static cocos2d::CCSprite *createSpriteFromPathOrFallback(
        std::filesystem::path const &path,
        char const *fallback)
    {
        if (!path.empty())
        {
            auto pathStr = path.string();
            if (auto *sprite = cocos2d::CCSprite::create(pathStr.c_str()))
                return sprite;
        }

        return cocos2d::CCSprite::create(fallback);
    }

    static GLubyte computeFadeOpacity(Settings const &settings, int index, int total)
    {
        int fadeIndex = std::max(0, total - 1 - index);
        int reduced = static_cast<int>(settings.opacity) - fadeIndex * static_cast<int>(settings.fadeStep);
        int floored = std::max(reduced, static_cast<int>(settings.fadeMin));
        return static_cast<GLubyte>(std::clamp(floored, 0, 255));
    }

    void refreshCheckpointOpacity(CheckpointObject *checkpoint, int checkpointIndex, Settings const &settings)
    {
        if (!checkpoint || !settings.fade)
            return;

        auto cpIt = s_cpToOid.find(checkpoint);
        if (cpIt == s_cpToOid.end())
            return;

        auto overlayIt = s_overlayMap.find(cpIt->second);
        if (overlayIt == s_overlayMap.end())
            return;

        auto *outer = overlayIt->second.second;
        if (!outer)
            return;

        int total = m_checkpointArray ? static_cast<int>(m_checkpointArray->count()) : 1;
        auto targetOpacity = computeFadeOpacity(settings, checkpointIndex, total);

        updateOverlayOpacity(outer, targetOpacity);
    }

    void refreshAllCheckpointOpacity(Settings const &settings)
    {
        if (!m_checkpointArray || !settings.fade)
            return;

        for (unsigned i = 0; i < m_checkpointArray->count(); ++i)
        {
            auto *checkpoint = static_cast<CheckpointObject *>(m_checkpointArray->objectAtIndex(i));
            refreshCheckpointOpacity(checkpoint, static_cast<int>(i), settings);
        }
    }

    void removeOverlayNodeByTag(int overlayId, cocos2d::CCNode *node)
    {
        int tag = 0x4F000000 + overlayId;
        if (m_objectLayer)
        {
            m_objectLayer->removeChildByTag(tag, true);
            return;
        }

        if (!node)
            return;

        if (auto *parent = node->getParent())
            parent->removeChildByTag(tag, true);
    }

    void decorateCheckpoint(CheckpointObject *checkpoint, bool isNewPlacement)
    {
        auto settings = getSettings();

        if (!settings.enabled || !checkpoint)
            return;

        auto *phys = checkpoint->m_physicalCheckpointObject;
        if (!phys)
            return;

        if (phys->getChildByID(checkpoint_mod::OuterId))
            return;

        phys->m_addToNodeContainer = true;
        phys->stopAllActions();
        phys->unscheduleAllSelectors();

        hideNodeRecursive(checkpoint);
        hideNodeRecursive(phys);

        checkpoint->setVisible(false);
        phys->setVisible(false);
        phys->makeInvisible();

        if (auto *rgba = typeinfo_cast<cocos2d::CCRGBAProtocol *>(checkpoint))
            rgba->setOpacity(0);

        if (auto *rgba = typeinfo_cast<cocos2d::CCRGBAProtocol *>(phys))
            rgba->setOpacity(0);

        auto style = checkpoint_mod::getStyle();
        auto const &pack = checkpoint_mod::getPack(style.shape);

        bool useCustomImage = !settings.checkpointImage.empty();

        cocos2d::CCSprite *outer = nullptr;
        cocos2d::CCSprite *inner = nullptr;

        if (useCustomImage)
        {
            outer = createSpriteFromPathOrFallback(settings.checkpointImage, pack.outerName);
        }
        else
        {
            outer = createSpriteFromPathOrFallback({}, pack.outerName);
            inner = createSpriteFromPathOrFallback({}, pack.innerName);
        }

        if (!outer || (!useCustomImage && !inner))
            return;

        outer->setID(checkpoint_mod::OuterId);
        outer->setTag(0x4F000000 + s_nextOverlayId);

        outer->setCascadeOpacityEnabled(false);
        outer->setCascadeColorEnabled(false);

        if (!useCustomImage)
        {
            inner->setID(checkpoint_mod::InnerId);
            inner->setCascadeOpacityEnabled(false);
            inner->setCascadeColorEnabled(false);
            inner->setTag(outer->getTag());

            if (settings.outerColor)
                outer->setColor(style.outerColor);

            if (settings.innerColor)
                inner->setColor(style.innerColor);
        }

        int idx = m_fields->placementIndex;

        GLubyte targetOpacity = settings.opacity;
        if (settings.fade)
        {
            int total = std::max(1, idx + 1);
            targetOpacity = computeFadeOpacity(settings, idx, total);
        }

        if (settings.fadeIn && isNewPlacement)
        {
            if (targetOpacity == 0)
            {
                outer->setOpacity(0);
                outer->setVisible(false);
            }
            else
            {
                outer->setOpacity(0);
                outer->setVisible(true);
                outer->runAction(CCFadeTo::create(settings.fadeInDuration, targetOpacity));
            }
        }
        else
        {
            outer->setOpacity(targetOpacity);
            outer->setVisible(targetOpacity != 0);
        }

        auto nodeSize = phys->getContentSize();
        auto outerSize = outer->getContentSize();

        float scale = std::min(
                          nodeSize.width / std::max(outerSize.width, 1.f),
                          nodeSize.height / std::max(outerSize.height, 1.f)) *
                      style.scale;

        outer->setAnchorPoint({0.5f, 0.5f});
        outer->setScale(scale);

        cocos2d::CCNode *overlayParent = m_objectLayer;
        if (!overlayParent)
            overlayParent = phys->getParent();

        if (overlayParent)
        {
            auto physAnchor = phys->getAnchorPoint();
            auto physPos = phys->getPosition();

            if (auto *physParent = phys->getParent())
            {
                auto worldPos = physParent->convertToWorldSpace(physPos);
                physPos = overlayParent->convertToNodeSpace(worldPos);
            }

            outer->setPosition({
                physPos.x + (0.5f - physAnchor.x) * nodeSize.width,
                physPos.y + (0.5f - physAnchor.y) * nodeSize.height,
            });

            if (!useCustomImage && inner)
            {
                inner->setAnchorPoint({0.5f, 0.5f});
                inner->setPosition({outerSize.width * 0.5f, outerSize.height * 0.5f});
                outer->addChild(inner);
            }

            overlayParent->addChild(outer, phys->getZOrder());

            int overlayId = s_nextOverlayId++;
            s_overlayMap[overlayId] = {checkpoint->m_uniqueID, outer};
            s_cpToOid[checkpoint] = overlayId;
        }
        else
        {
            outer->setPosition({nodeSize.width * 0.5f, nodeSize.height * 0.5f});

            if (!useCustomImage && inner)
            {
                inner->setAnchorPoint({0.5f, 0.5f});
                inner->setPosition({outerSize.width * 0.5f, outerSize.height * 0.5f});
                outer->addChild(inner);
            }

            phys->addChild(outer, 1);

            int overlayId = s_nextOverlayId++;
            s_overlayMap[overlayId] = {checkpoint->m_uniqueID, outer};
            s_cpToOid[checkpoint] = overlayId;

            if (settings.fade)
                refreshAllCheckpointOpacity(settings);
        }
    }

    void storeCheckpoint(CheckpointObject *checkpoint)
    {
        PlayLayer::storeCheckpoint(checkpoint);
        decorateCheckpoint(checkpoint, true);
        ++m_fields->placementIndex;

        auto settings = getSettings();
        if (settings.fade)
            refreshAllCheckpointOpacity(settings);
    }

    void loadFromCheckpoint(CheckpointObject *checkpoint)
    {
        PlayLayer::loadFromCheckpoint(checkpoint);
        decorateCheckpoint(checkpoint, false);

        auto settings = getSettings();
        if (settings.fade)
            refreshAllCheckpointOpacity(settings);
    }

    void clearAllCheckpointOverlays()
    {
        std::vector<int> overlayIds;
        overlayIds.reserve(s_overlayMap.size());
        for (auto const &entry : s_overlayMap)
            overlayIds.push_back(entry.first);

        for (int overlayId : overlayIds)
        {
            auto overlayIt = s_overlayMap.find(overlayId);
            if (overlayIt == s_overlayMap.end())
                continue;

            removeOverlayNodeByTag(overlayId, overlayIt->second.second);
        }

        s_overlayMap.clear();
        s_cpToOid.clear();
    }

    void resetLevel()
    {
        PlayLayer::resetLevel();
        clearAllCheckpointOverlays();
        m_fields->placementIndex = 0;

        if (m_checkpointArray && m_checkpointArray->count())
        {
            decorateCheckpoint(
                static_cast<CheckpointObject *>(m_checkpointArray->lastObject()),
                false);

            auto settings = getSettings();
            if (settings.fade)
                refreshAllCheckpointOpacity(settings);
        }
    }

    CheckpointObject *getCheckpointToRemove(bool first)
    {
        if (!m_checkpointArray || m_checkpointArray->count() == 0)
            return nullptr;

        return static_cast<CheckpointObject *>(
            first ? m_checkpointArray->objectAtIndex(0)
                  : m_checkpointArray->lastObject());
    }

    void removeOverlayForCheckpoint(CheckpointObject *checkpoint)
    {
        if (!checkpoint)
            return;

        auto it = s_cpToOid.find(checkpoint);
        if (it == s_cpToOid.end())
            return;

        int oid = it->second;
        auto overlayIt = s_overlayMap.find(oid);
        if (overlayIt != s_overlayMap.end())
        {
            removeOverlayNodeByTag(oid, overlayIt->second.second);
            s_overlayMap.erase(overlayIt);
        }

        s_cpToOid.erase(it);
    }

    void cleanupMissingCheckpointOverlays()
    {
        if (!m_checkpointArray || m_checkpointArray->count() == 0)
        {
            std::vector<int> overlayIds;
            overlayIds.reserve(s_overlayMap.size());
            for (auto const &entry : s_overlayMap)
                overlayIds.push_back(entry.first);

            for (int overlayId : overlayIds)
            {
                auto overlayIt = s_overlayMap.find(overlayId);
                if (overlayIt == s_overlayMap.end())
                    continue;

                removeOverlayNodeByTag(overlayId, overlayIt->second.second);
            }

            s_overlayMap.clear();
            s_cpToOid.clear();
            return;
        }

        std::vector<std::pair<CheckpointObject *, int>> staleEntries;
        staleEntries.reserve(s_cpToOid.size());

        for (auto it = s_cpToOid.begin(); it != s_cpToOid.end(); ++it)
        {
            CheckpointObject *cp = it->first;
            int oid = it->second;
            bool cpRemoved = true;

            for (unsigned i = 0; i < m_checkpointArray->count(); ++i)
            {
                if (m_checkpointArray->objectAtIndex(i) == cp)
                {
                    cpRemoved = false;
                    break;
                }
            }

            if (cpRemoved)
            {
                staleEntries.emplace_back(cp, oid);
            }
        }

        for (auto const &[cp, oid] : staleEntries)
        {
            auto overlayIt = s_overlayMap.find(oid);
            if (overlayIt != s_overlayMap.end())
            {
                removeOverlayNodeByTag(oid, overlayIt->second.second);
                s_overlayMap.erase(overlayIt);
            }

            s_cpToOid.erase(cp);
        }
    }

    void removeAllCheckpoints()
    {
        clearAllCheckpointOverlays();
        PlayLayer::removeAllCheckpoints();
        m_fields->placementIndex = 0;

        auto settings = getSettings();
        if (settings.fade)
            refreshAllCheckpointOpacity(settings);
    }

    void togglePracticeMode(bool practiceMode)
    {
        PlayLayer::togglePracticeMode(practiceMode);

        if (!practiceMode)
        {
            clearAllCheckpointOverlays();
            m_fields->placementIndex = 0;
            return;
        }

        m_fields->placementIndex = 0;
        if (!m_checkpointArray)
            return;

        for (unsigned i = 0; i < m_checkpointArray->count(); ++i)
        {
            auto *checkpoint = static_cast<CheckpointObject *>(m_checkpointArray->objectAtIndex(i));
            decorateCheckpoint(checkpoint, false);
            ++m_fields->placementIndex;
        }

        auto settings = getSettings();
        if (settings.fade)
            refreshAllCheckpointOpacity(settings);
    }

    void removeCheckpoint(bool p0)
    {
        auto *checkpoint = getCheckpointToRemove(p0);
        removeOverlayForCheckpoint(checkpoint);

        PlayLayer::removeCheckpoint(p0);
        cleanupMissingCheckpointOverlays();

        auto settings = getSettings();
        if (settings.fade)
            refreshAllCheckpointOpacity(settings);
    }
};