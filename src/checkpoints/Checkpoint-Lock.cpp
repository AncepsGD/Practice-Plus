#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

#include "CheckpointHelpers.hpp"

using namespace geode::prelude;

namespace
{
    constexpr char kLockSpriteId[] = "practice-plus-checkpoint-lock";
    constexpr int kLockZOrder = 1000000;

    struct LockData
    {
        std::unordered_set<CheckpointObject *> locked;
        std::unordered_map<CheckpointObject *, CCSprite *> icons;
    };
}

class $modify(CheckpointLockPlayLayer, PlayLayer)
{
    struct Fields
    {
        LockData lockData;
    };

    bool isLocked(CheckpointObject *checkpoint)
    {
        return checkpoint && m_fields->lockData.locked.contains(checkpoint);
    }

    void removeLockIcon(CheckpointObject *checkpoint)
    {
        auto it = m_fields->lockData.icons.find(checkpoint);
        if (it == m_fields->lockData.icons.end())
            return;

        if (it->second)
            it->second->removeFromParentAndCleanup(true);
        m_fields->lockData.icons.erase(it);
    }

    void clearLocks()
    {
        for (auto const &[checkpoint, icon] : m_fields->lockData.icons)
        {
            if (icon)
                icon->removeFromParentAndCleanup(true);
        }

        m_fields->lockData.icons.clear();
        m_fields->lockData.locked.clear();
    }

    void rebuildLockIcons()
    {
        for (auto const &[checkpoint, icon] : m_fields->lockData.icons)
        {
            if (icon)
                icon->removeFromParentAndCleanup(true);
        }
        m_fields->lockData.icons.clear();

        if (!m_checkpointArray)
            return;

        for (auto *checkpoint : geode::cocos::CCArrayExt<CheckpointObject *>(m_checkpointArray))
        {
            if (isLocked(checkpoint))
                showLockIcon(checkpoint);
        }
    }

    void showLockIcon(CheckpointObject *checkpoint)
    {
        if (!checkpoint || !checkpoint->m_physicalCheckpointObject)
            return;

        auto *physical = checkpoint->m_physicalCheckpointObject;
        auto *parent = m_objectLayer ? static_cast<CCNode *>(m_objectLayer) : physical->getParent();
        if (!parent)
            return;

        auto *icon = CCSprite::create("lock.png"_spr);
        if (!icon)
            return;

        auto const size = physical->getContentSize();
        auto const iconSize = icon->getContentSize();
        auto const scale = std::min(
            size.width / std::max(iconSize.width, 1.0f),
            size.height / std::max(iconSize.height, 1.0f));
        auto scaleFactor = 0.75f;
        if (Mod::get()->getSettingValue<bool>("custom-checkpoints-enabled"))
            scaleFactor = checkpoint_mod::getStyle().scale * 0.85f;

        icon->setID(kLockSpriteId);
        icon->setAnchorPoint({0.5f, 0.5f});
        icon->setScale(std::max(scale * scaleFactor, 0.25f));

        auto position = physical->getPosition();
        if (auto *physicalParent = physical->getParent())
        {
            auto worldPosition = physicalParent->convertToWorldSpace(position);
            position = parent->convertToNodeSpace(worldPosition);
        }

        auto const anchor = physical->getAnchorPoint();
        icon->setPosition({
            position.x + (0.5f - anchor.x) * size.width,
            position.y + (0.5f - anchor.y) * size.height,
        });
        parent->addChild(icon, kLockZOrder);
        m_fields->lockData.icons[checkpoint] = icon;
    }

    void toggleLatestCheckpointLock()
    {
        if (!m_isPracticeMode || !m_checkpointArray || m_checkpointArray->count() == 0)
            return;

        auto *checkpoint = static_cast<CheckpointObject *>(m_checkpointArray->lastObject());
        if (!checkpoint)
            return;

        if (isLocked(checkpoint))
        {
            m_fields->lockData.locked.erase(checkpoint);
            removeLockIcon(checkpoint);
            return;
        }

        m_fields->lockData.locked.insert(checkpoint);
        showLockIcon(checkpoint);
    }

    bool init(GJGameLevel *level, bool useReplay, bool dontCreateObjects)
    {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects))
            return false;

        this->addEventListener(
            KeybindSettingPressedEventV3(Mod::get(), "checkpoint-lock"),
            [this](Keybind const &, bool down, bool repeat, double)
            {
                if (down && !repeat)
                    toggleLatestCheckpointLock();

                return ListenerResult::Propagate;
            });

        return true;
    }

    void removeCheckpoint(bool first)
    {
        if (auto *checkpoint = m_checkpointArray && m_checkpointArray->count()
                ? static_cast<CheckpointObject *>(first ? m_checkpointArray->objectAtIndex(0) : m_checkpointArray->lastObject())
                : nullptr)
        {
            if (isLocked(checkpoint))
                return;

            PlayLayer::removeCheckpoint(first);
            m_fields->lockData.locked.erase(checkpoint);
            removeLockIcon(checkpoint);
            return;
        }

        PlayLayer::removeCheckpoint(first);
    }

    void removeAllCheckpoints()
    {
        if (!m_fields->lockData.locked.empty())
            return;

        PlayLayer::removeAllCheckpoints();
        clearLocks();
    }

    void togglePracticeMode(bool practiceMode)
    {
        if (!practiceMode)
            clearLocks();

        PlayLayer::togglePracticeMode(practiceMode);
    }

    void resetLevel()
    {
        PlayLayer::resetLevel();
        rebuildLockIcons();
    }
};