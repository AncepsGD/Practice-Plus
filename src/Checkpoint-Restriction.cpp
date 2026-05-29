#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>

using namespace geode::prelude;

class $modify(StackableCheckpoints, PlayLayer) {
    void markCheckpoint() {
        m_tryPlaceCheckpoint = false;
        PlayLayer::markCheckpoint();
    }
};