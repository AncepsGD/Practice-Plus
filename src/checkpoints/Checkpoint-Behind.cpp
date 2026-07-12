#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>

#include <cstdint>
#include <limits>

using namespace geode::prelude;

static constexpr int kCheckpointZ = 58;

namespace
{
    bool isPointerUsable(void const *ptr)
    {
        if (!ptr)
            return false;

        auto value = reinterpret_cast<std::uintptr_t>(ptr);
        return value != std::numeric_limits<std::uintptr_t>::max();
    }

    bool isCheckpointBehindEnabled()
    {
        if (auto *mod = Mod::get())
            return mod->getSettingValue<bool>("checkpoint-behind-player");
        return false;
    }

    CCNode *getTopNodeUnderParent(CCNode *node, CCNode *parent)
    {
        if (!node || !parent)
            return nullptr;

        CCNode *current = node;
        while (current->getParent() && current->getParent() != parent)
        {
            current = current->getParent();
        }

        return current->getParent() == parent ? current : nullptr;
    }

    void applyZOrder(CheckpointObject *checkpoint, PlayLayer *layer)
    {
        if (!isPointerUsable(checkpoint) || !layer)
            return;

        auto *phys = checkpoint->m_physicalCheckpointObject;
        if (!isPointerUsable(phys))
            return;

        auto *objectLayer = layer->m_objectLayer;
        if (!objectLayer)
            return;

        auto *topNode = getTopNodeUnderParent(phys, objectLayer);
        if (!topNode)
            return;

        objectLayer->reorderChild(topNode, kCheckpointZ);
    }
}

class $modify(CheckpointZOrderPlayLayer, PlayLayer)
{
    void applyAllCheckpointZOrders()
    {
        if (!m_checkpointArray)
            return;

        for (auto *checkpoint : geode::cocos::CCArrayExt<CheckpointObject *>(m_checkpointArray))
        {
            applyZOrder(checkpoint, this);
        }
    }

    void storeCheckpoint(CheckpointObject *checkpoint)
    {
        PlayLayer::storeCheckpoint(checkpoint);
        if (isCheckpointBehindEnabled())
            applyZOrder(checkpoint, this);
    }

    void loadFromCheckpoint(CheckpointObject *checkpoint)
    {
        PlayLayer::loadFromCheckpoint(checkpoint);
        if (isCheckpointBehindEnabled())
            applyZOrder(checkpoint, this);
    }

    void resetLevel()
    {
        PlayLayer::resetLevel();
        if (isCheckpointBehindEnabled())
            applyAllCheckpointZOrders();
    }
};