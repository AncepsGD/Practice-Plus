#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>

using namespace geode::prelude;

class $modify(CheckpointLimitPlayLayer, PlayLayer)
{
    void storeCheckpoint(CheckpointObject* checkpointObject)
    {
        PlayLayer::storeCheckpoint(checkpointObject);

        if (!Mod::get()->getSettingValue<bool>("checkpoint-limit-enabled") || !m_checkpointArray)
        {
            return;
        }

        auto limit = static_cast<unsigned int>(
            std::max<int64_t>(Mod::get()->getSettingValue<int64_t>("checkpoint-limit"), 1)
        );

        while (m_checkpointArray->count() > limit)
        {
            bool isLast = m_checkpointArray->count() == limit + 1;
            PlayLayer::removeCheckpoint(isLast);
        }
    }
};