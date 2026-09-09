#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>
#include <Geode/modify/PlayLayer.hpp>

using namespace geode::prelude;

static bool isNoDeathEffectEnabled()
{
    auto mod = Mod::get();
    return mod && mod->getSettingValue<bool>("no-death-effect");
}

class $modify(NoDeathEffectPlayerObject, PlayerObject) {
    void playDeathEffect() {
        if (!isNoDeathEffectEnabled()) {
            PlayerObject::playDeathEffect();
            return;
        }

        m_practiceDeathEffect = false;
    }

    static void onModify(auto& self) {
        (void)self.setHookPriorityPost("PlayerObject::playDeathEffect", Priority::Last);
    }
};

class $modify(NoDeathEffectPlayLayer, PlayLayer) {
    void destroyPlayer(PlayerObject* player, GameObject* object) {
        if (isNoDeathEffectEnabled() && player)
            player->m_practiceDeathEffect = false;

        PlayLayer::destroyPlayer(player, object);
    }
};

class $modify(NoDeathEffectBaseGameLayer, GJBaseGameLayer) {
    void playExitDualEffect(PlayerObject* player) {
        if (!isNoDeathEffectEnabled())
            GJBaseGameLayer::playExitDualEffect(player);
    }

    static void onModify(auto& self) {
        (void)self.setHookPriorityPost("GJBaseGameLayer::playExitDualEffect", Priority::Last);
    }
};