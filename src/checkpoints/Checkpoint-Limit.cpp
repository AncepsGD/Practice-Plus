#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>

using namespace geode::prelude;

class $modify(CheckpointLimitPlayLayer, PlayLayer)
{
    void storeCheckpoint(CheckpointObject *checkpointObject)
    {
        if (!Mod::get()->getSettingValue<bool>("checkpoint-limit-enabled"))
        {
            PlayLayer::storeCheckpoint(checkpointObject);
            return;
        }

        PlayLayer::storeCheckpoint(checkpointObject);

        int limit = static_cast<int>(Mod::get()->getSettingValue<int64_t>("checkpoint-limit"));
        limit = std::max(limit, 1);

        while (m_checkpointArray && m_checkpointArray->count() > static_cast<unsigned>(limit))
        {
            PlayLayer::removeCheckpoint(true);
        }
    }
};