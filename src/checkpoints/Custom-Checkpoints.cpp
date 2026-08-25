#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>

#include "CheckpointHelpers.hpp"

using namespace geode::prelude;

namespace
{
    auto &getOverlayMap()
    {
        static std::unordered_map<int, std::pair<int, cocos2d::CCNode *>> value;
        return value;
    }

    auto &getCheckpointOverlayIds()
    {
        static std::unordered_map<CheckpointObject *, int> value;
        return value;
    }

    int &getNextOverlayId()
    {
        static int value = 1;
        return value;
    }

    bool isPotentiallyValidPointer(void const *ptr)
    {
        if (!ptr)
            return false;

        auto value = reinterpret_cast<uintptr_t>(ptr);
        if (value <= 0x1000)
            return false;

        if (value == std::numeric_limits<uintptr_t>::max() || value == std::numeric_limits<uintptr_t>::max() - 1)
            return false;

        if ((value >> 48) == 0xFFFF)
            return false;

        return true;
    }

    bool isValidCheckpointPhysicalObject(CheckpointObject *checkpoint)
    {
        if (!isPotentiallyValidPointer(checkpoint))
            return false;

        auto *phys = checkpoint->m_physicalCheckpointObject;
        if (!isPotentiallyValidPointer(phys))
            return false;

        if (!phys->getParent() && !checkpoint->getParent())
            return false;

        return true;
    }
}

class $modify(CustomCheckpointsPlayLayer, PlayLayer)
{
    struct CheckpointCommand
    {
        enum Type
        {
            Add,
            Remove,
            Load,
            Reset,
        } type = Add;

        CheckpointObject *checkpoint = nullptr;
        bool isNewPlacement = false;
    };

    struct Fields
    {
        int placementIndex = 0;
        std::vector<CheckpointCommand> checkpointQueue;
        unsigned lastCheckpointCount = 0;
        bool checkpointArrayNeedsReconcile = true;
    };

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

    bool isCheckpointTracked(CheckpointObject *checkpoint) const
    {
        if (!isPotentiallyValidPointer(checkpoint))
            return false;

        if (!m_checkpointArray)
            return false;

        for (unsigned i = 0; i < m_checkpointArray->count(); ++i)
        {
            if (m_checkpointArray->objectAtIndex(i) == checkpoint)
                return true;
        }

        return false;
    }

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

        if (auto *rgba = geode::cast::typeinfo_cast<cocos2d::CCRGBAProtocol *>(node))
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

        if (auto *rgba = geode::cast::typeinfo_cast<cocos2d::CCRGBAProtocol *>(node))
            rgba->setOpacity(0);

        node->setVisible(false);
    }

    static void setOpacityAndVisibility(cocos2d::CCNode *node, GLubyte opacity)
    {
        if (!node)
            return;

        if (auto *rgba = geode::cast::typeinfo_cast<cocos2d::CCRGBAProtocol *>(node))
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

        if (auto *rgba = geode::cast::typeinfo_cast<cocos2d::CCRGBAProtocol *>(node))
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

    static void hideCheckpointVisuals(CheckpointObject *checkpoint)
    {
        if (!isPotentiallyValidPointer(checkpoint))
            return;

        auto *phys = checkpoint->m_physicalCheckpointObject;

        if (auto *rgba = geode::cast::typeinfo_cast<cocos2d::CCRGBAProtocol *>(checkpoint))
            rgba->setOpacity(0);
        checkpoint->setVisible(false);

        if (!isPotentiallyValidPointer(phys))
            return;

        if (auto *rgba = geode::cast::typeinfo_cast<cocos2d::CCRGBAProtocol *>(phys))
            rgba->setOpacity(0);
        phys->setVisible(false);

        if (auto *parent = phys->getParent())
            phys->makeInvisible();

        if (phys->m_colorSprite)
        {
            phys->m_colorSprite->setVisible(false);
            phys->m_colorSprite->setOpacity(0);
        }

        if (phys->m_mainActionSprite)
            phys->m_mainActionSprite->setVisible(false);

        if (phys->m_detailActionSprite)
            phys->m_detailActionSprite->setVisible(false);
    }

    static cocos2d::CCSprite *createSpriteFromPathOrFallback(
        std::filesystem::path const &path,
        geode::ZStringView fallback)
    {
        if (!path.empty())
        {
            auto pathStr = string::pathToString(path);
            if (auto *sprite = cocos2d::CCSprite::create(pathStr.c_str()))
                return sprite;
        }

        return cocos2d::CCSprite::create(fallback.c_str());
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

        auto cpIt = getCheckpointOverlayIds().find(checkpoint);
        if (cpIt == getCheckpointOverlayIds().end())
            return;

        auto overlayIt = getOverlayMap().find(cpIt->second);
        if (overlayIt == getOverlayMap().end())
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

    bool removeNodeByTagRecursive(cocos2d::CCNode *parent, int tag)
    {
        if (!parent)
            return false;

        if (auto *children = parent->getChildren())
        {
            auto count = static_cast<ptrdiff_t>(children->count());
            for (auto i = count - 1; i >= 0; --i)
            {
                auto *child = static_cast<cocos2d::CCNode *>(children->objectAtIndex(static_cast<size_t>(i)));
                if (child->getTag() == tag)
                {
                    parent->removeChild(child, true);
                    return true;
                }

                if (removeNodeByTagRecursive(child, tag))
                    return true;
            }
        }

        return false;
    }

    void removeOverlayNodeByTag(int overlayId, cocos2d::CCNode *)
    {
        int tag = 0x4F000000 + overlayId;

        if (m_objectLayer && removeNodeByTagRecursive(m_objectLayer, tag))
            return;

        removeNodeByTagRecursive(this, tag);
    }

    void decorateCheckpoint(CheckpointObject *checkpoint, bool isNewPlacement)
    {
        auto settings = getSettings();

        if (!settings.enabled || !isPotentiallyValidPointer(checkpoint))
            return;

        if (!isNewPlacement && !isCheckpointTracked(checkpoint))
            return;

        if (!isValidCheckpointPhysicalObject(checkpoint))
            return;

        auto *phys = checkpoint->m_physicalCheckpointObject;
        if (!isPotentiallyValidPointer(phys))
            return;

        if (!phys->getParent() && !checkpoint->getParent())
            return;

        if (getCheckpointOverlayIds().contains(checkpoint))
            return;

        phys->m_addToNodeContainer = true;
        phys->stopAllActions();
        phys->unscheduleAllSelectors();

        hideCheckpointVisuals(checkpoint);

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
        outer->setTag(0x4F000000 + getNextOverlayId());

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

            int overlayId = getNextOverlayId()++;
            getOverlayMap()[overlayId] = {checkpoint->m_uniqueID, outer};
            getCheckpointOverlayIds()[checkpoint] = overlayId;
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

            int overlayId = getNextOverlayId()++;
            getOverlayMap()[overlayId] = {checkpoint->m_uniqueID, outer};
            getCheckpointOverlayIds()[checkpoint] = overlayId;

            if (settings.fade)
                refreshAllCheckpointOpacity(settings);
        }
    }

    void commitCheckpointQueue()
    {
        auto settings = getSettings();
        if (m_fields->checkpointQueue.empty())
            return;

        for (auto &command : m_fields->checkpointQueue)
        {
            switch (command.type)
            {
            case CheckpointCommand::Add:
                decorateCheckpoint(command.checkpoint, command.isNewPlacement);
                if (!getCheckpointOverlayIds().contains(command.checkpoint))
                    m_fields->checkpointArrayNeedsReconcile = true;
                ++m_fields->placementIndex;
                break;

            case CheckpointCommand::Remove:
                removeOverlayForCheckpoint(command.checkpoint);
                break;

            case CheckpointCommand::Load:
                decorateCheckpoint(command.checkpoint, false);
                if (!getCheckpointOverlayIds().contains(command.checkpoint))
                    m_fields->checkpointArrayNeedsReconcile = true;
                break;

            case CheckpointCommand::Reset:
                clearAllCheckpointOverlays();
                m_fields->placementIndex = 0;
                break;
            }
        }

        cleanupMissingCheckpointOverlays();

        if (settings.fade)
            refreshAllCheckpointOpacity(settings);

        m_fields->checkpointQueue.clear();
    }

    void storeCheckpoint(CheckpointObject *checkpoint)
    {
        PlayLayer::storeCheckpoint(checkpoint);
        m_fields->checkpointQueue.push_back({CheckpointCommand::Add, checkpoint, true});
    }

    void loadFromCheckpoint(CheckpointObject *checkpoint)
    {
        PlayLayer::loadFromCheckpoint(checkpoint);
        m_fields->checkpointQueue.push_back({CheckpointCommand::Load, checkpoint, false});
    }

    void postUpdate(float dt)
    {
        PlayLayer::postUpdate(dt);

        auto settings = getSettings();
        if (!settings.enabled || !m_checkpointArray)
        {
            commitCheckpointQueue();
            return;
        }

        auto checkpointCount = m_checkpointArray->count();
        if (m_fields->checkpointArrayNeedsReconcile ||
            checkpointCount != m_fields->lastCheckpointCount)
        {
            for (unsigned i = 0; i < checkpointCount; ++i)
            {
                auto *checkpoint = static_cast<CheckpointObject *>(m_checkpointArray->objectAtIndex(i));
                if (!checkpoint)
                    continue;

                auto it = getCheckpointOverlayIds().find(checkpoint);
                if (it == getCheckpointOverlayIds().end())
                    m_fields->checkpointQueue.push_back({CheckpointCommand::Load, checkpoint, false});
            }

            m_fields->lastCheckpointCount = checkpointCount;
            m_fields->checkpointArrayNeedsReconcile = false;
        }

        commitCheckpointQueue();
    }

    void clearAllCheckpointOverlays()
    {
        std::vector<int> overlayIds;
        overlayIds.reserve(getOverlayMap().size());
        for (auto const &entry : getOverlayMap())
            overlayIds.push_back(entry.first);

        for (int overlayId : overlayIds)
        {
            auto overlayIt = getOverlayMap().find(overlayId);
            if (overlayIt == getOverlayMap().end())
                continue;

            removeOverlayNodeByTag(overlayId, overlayIt->second.second);
        }

        getOverlayMap().clear();
        getCheckpointOverlayIds().clear();
    }

    void resetLevel()
    {

        clearAllCheckpointOverlays();
        m_fields->placementIndex = 0;
        m_fields->checkpointQueue.clear();
        m_fields->lastCheckpointCount = 0;
        m_fields->checkpointArrayNeedsReconcile = true;

        PlayLayer::resetLevel();
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

        auto it = getCheckpointOverlayIds().find(checkpoint);
        if (it == getCheckpointOverlayIds().end())
            return;

        auto *phys = checkpoint->m_physicalCheckpointObject;
        if (!phys)
        {
            int oid = it->second;
            auto overlayIt = getOverlayMap().find(oid);
            if (overlayIt != getOverlayMap().end())
            {
                removeOverlayNodeByTag(oid, overlayIt->second.second);
                getOverlayMap().erase(overlayIt);
            }

            getCheckpointOverlayIds().erase(it);
            return;
        }

        int oid = it->second;
        auto overlayIt = getOverlayMap().find(oid);
        if (overlayIt != getOverlayMap().end())
        {
            removeOverlayNodeByTag(oid, overlayIt->second.second);
            getOverlayMap().erase(overlayIt);
        }

        getCheckpointOverlayIds().erase(it);
    }

    void cleanupMissingCheckpointOverlays()
    {
        if (!m_checkpointArray || m_checkpointArray->count() == 0)
        {
            std::vector<int> overlayIds;
            overlayIds.reserve(getOverlayMap().size());
            for (auto const &entry : getOverlayMap())
                overlayIds.push_back(entry.first);

            for (int overlayId : overlayIds)
            {
                auto overlayIt = getOverlayMap().find(overlayId);
                if (overlayIt == getOverlayMap().end())
                    continue;

                removeOverlayNodeByTag(overlayId, overlayIt->second.second);
            }

            getOverlayMap().clear();
            getCheckpointOverlayIds().clear();
            return;
        }

        std::vector<std::pair<CheckpointObject *, int>> staleEntries;
        staleEntries.reserve(getCheckpointOverlayIds().size());

        for (auto it = getCheckpointOverlayIds().begin(); it != getCheckpointOverlayIds().end(); ++it)
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
            auto overlayIt = getOverlayMap().find(oid);
            if (overlayIt != getOverlayMap().end())
            {
                removeOverlayNodeByTag(oid, overlayIt->second.second);
                getOverlayMap().erase(overlayIt);
            }

            getCheckpointOverlayIds().erase(cp);
        }
    }

    void removeAllCheckpoints()
    {
        PlayLayer::removeAllCheckpoints();
        m_fields->checkpointQueue.push_back({CheckpointCommand::Reset, nullptr, false});
        m_fields->checkpointArrayNeedsReconcile = true;
    }

    void togglePracticeMode(bool practiceMode)
    {
        PlayLayer::togglePracticeMode(practiceMode);

        if (!practiceMode)
        {
            m_fields->checkpointQueue.push_back({CheckpointCommand::Reset, nullptr, false});
            m_fields->checkpointArrayNeedsReconcile = true;
            return;
        }

        if (!m_checkpointArray)
            return;

        for (unsigned i = 0; i < m_checkpointArray->count(); ++i)
        {
            auto *checkpoint = static_cast<CheckpointObject *>(m_checkpointArray->objectAtIndex(i));
            if (!checkpoint)
                continue;

            m_fields->checkpointQueue.push_back({CheckpointCommand::Load, checkpoint, false});
        }
    }

    void removeCheckpoint(bool p0)
    {
        auto *checkpoint = getCheckpointToRemove(p0);
        PlayLayer::removeCheckpoint(p0);
        m_fields->checkpointQueue.push_back({CheckpointCommand::Remove, checkpoint, false});
        m_fields->checkpointArrayNeedsReconcile = true;
    }
};

class $modify(SafePlayerObject, PlayerObject)
{
    void removePendingCheckpoint()
    {
        auto *checkpoint = this->m_pendingCheckpoint;
        if (checkpoint && !isValidCheckpointPhysicalObject(checkpoint))
        {
            this->m_pendingCheckpoint = nullptr;
            return;
        }

        PlayerObject::removePendingCheckpoint();
    }
};