#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>

using namespace geode::prelude;

static constexpr int kCheckpointZ = 58;

class $modify(CheckpointZOrderPlayLayer, PlayLayer)
{
    static bool isEnabled()
    {
        if (auto *mod = Mod::get())
            return mod->getSettingValue<bool>("checkpoint-behind-player");
        return false;
    }

    static CCNode *getTopNodeUnderParent(CCNode *node, CCNode *parent)
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

    static void applyZOrder(CheckpointObject *checkpoint, PlayLayer *layer)
    {
        if (!checkpoint || !layer)
            return;

        auto *phys = checkpoint->m_physicalCheckpointObject;
        if (!phys)
            return;

        auto *objectLayer = layer->m_objectLayer;
        if (!objectLayer)
            return;

        auto *topNode = getTopNodeUnderParent(phys, objectLayer);
        if (!topNode)
            return;

        objectLayer->reorderChild(topNode, kCheckpointZ);
    }

    void applyAllCheckpointZOrders()
    {
        if (!m_checkpointArray)
            return;

        for (unsigned i = 0; i < m_checkpointArray->count(); ++i)
        {
            applyZOrder(
                static_cast<CheckpointObject *>(m_checkpointArray->objectAtIndex(i)),
                this);
        }
    }

    void storeCheckpoint(CheckpointObject *checkpoint)
    {
        PlayLayer::storeCheckpoint(checkpoint);
        if (isEnabled())
            applyZOrder(checkpoint, this);
    }

    void loadFromCheckpoint(CheckpointObject *checkpoint)
    {
        PlayLayer::loadFromCheckpoint(checkpoint);
        if (isEnabled())
            applyZOrder(checkpoint, this);
    }

    void resetLevel()
    {
        PlayLayer::resetLevel();
        if (isEnabled())
            applyAllCheckpointZOrders();
    }

    void postUpdate(float dt)
    {
        PlayLayer::postUpdate(dt);
        if (isEnabled())
            applyAllCheckpointZOrders();
    }
};