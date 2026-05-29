#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>

using namespace geode::prelude;

class $modify(PracticeCoinsGJBGL, GJBaseGameLayer)
{
    void collisionCheckObjects(
        PlayerObject *player,
        gd::vector<GameObject *> *sectionObjects,
        int objectCount,
        float dt)
    {
        if (!m_isPracticeMode || !Mod::get()->getSettingValue<bool>("practice-coins-enabled"))
        {
            return GJBaseGameLayer::collisionCheckObjects(
                player,
                sectionObjects,
                objectCount,
                dt);
        }

        auto playerRect = player->getObjectRect();

        for (int i = 0; i < objectCount; i++)
        {
            auto *obj = sectionObjects->at(i);

            if (
                obj->m_objectType != GameObjectType::SecretCoin &&
                obj->m_objectType != GameObjectType::UserCoin)
            {
                continue;
            }

            auto *effectObject = geode::cast::typeinfo_cast<EffectGameObject *>(obj);
            if (!effectObject)
            {
                continue;
            }

            if (effectObject->getOpacity() == 0)
            {
                continue;
            }

            auto objectRect = effectObject->getObjectRect();
            if (!playerRect.intersectsRect(objectRect))
            {
                continue;
            }

            effectObject->EffectGameObject::triggerObject(
                this,
                player->m_uniqueID,
                nullptr);

            GJBaseGameLayer::destroyObject(effectObject);

            GJBaseGameLayer::gameEventTriggered(
                GJGameEvent::UserCoin,
                0,
                0);
        }

        GJBaseGameLayer::collisionCheckObjects(
            player,
            sectionObjects,
            objectCount,
            dt);
    }
};