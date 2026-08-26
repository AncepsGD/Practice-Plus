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

static void forcePlayerVisible(PlayerObject* player)
{
    if (!player || !player->m_isDead)
        return;
    player->stopAllActions();
    player->setVisible(true);
    player->setOpacity(255);
    player->m_isHidden = false;
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
        if (isNoDeathEffectEnabled()) {
            if (m_player1)
                m_player1->m_practiceDeathEffect = false;
            if (m_player2)
                m_player2->m_practiceDeathEffect = false;
        }

        PlayLayer::destroyPlayer(player, object);

        if (isNoDeathEffectEnabled()) {
            forcePlayerVisible(player);
        }
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