#include <algorithm>

#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>

#include "CheckpointHelpers.hpp"

using namespace geode::prelude;

class $modify(CustomCheckpointsPlayLayer, PlayLayer)
{
    struct Settings
    {
        bool enabled = false;
        bool outerColor = false;
        bool innerColor = false;
        bool rainbow = false;

        float rainbowSpeed = 1.f;
        GLubyte opacity = 255;
    };

    static Settings getSettings()
    {
        Settings s;

        if (auto *mod = Mod::get())
        {
            s.enabled = mod->getSettingValue<bool>("custom-checkpoints-enabled");
            s.outerColor = mod->getSettingValue<bool>("checkpoint-outer-color-enabled");
            s.innerColor = mod->getSettingValue<bool>("checkpoint-inner-color-enabled");
            s.rainbow = mod->getSettingValue<bool>("checkpoint-rainbow-enabled");

            s.rainbowSpeed = std::max(
                0.01f,
                static_cast<float>(
                    mod->getSettingValue<double>("checkpoint-rainbow-speed")));

            s.opacity = static_cast<GLubyte>(
                std::clamp<int64_t>(
                    mod->getSettingValue<int64_t>("checkpoint-opacity"),
                    0,
                    255));
        }

        return s;
    }

    static CCAction *rainbowAction(float speed)
    {
        return CCRepeatForever::create(
            CCSequence::create(
                CCTintTo::create(1.f / speed, 255, 0, 0),
                CCTintTo::create(1.f / speed, 255, 255, 0),
                CCTintTo::create(1.f / speed, 0, 255, 0),
                CCTintTo::create(1.f / speed, 0, 255, 255),
                CCTintTo::create(1.f / speed, 0, 0, 255),
                CCTintTo::create(1.f / speed, 255, 0, 255),
                nullptr));
    }

    static unsigned getNodeOpacity(cocos2d::CCNode *node)
    {
        if (!node)
            return 255u;

        if (auto *rgba = typeinfo_cast<cocos2d::CCRGBAProtocol *>(node))
            return static_cast<unsigned>(rgba->getOpacity());

        return 255u;
    }

#define CCLOG(...)                                 \
    do                                             \
    {                                              \
        char _buf[1024];                           \
        snprintf(_buf, sizeof(_buf), __VA_ARGS__); \
        geode::log::info("{}", _buf);              \
    } while (0)

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

        node->stopAllActions();
        node->unscheduleAllSelectors();

        if (auto *rgba = typeinfo_cast<cocos2d::CCRGBAProtocol *>(node))
            rgba->setOpacity(0);

        node->setVisible(false);
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

    static void logNodeState(const char *label, cocos2d::CCNode *node)
    {
        if (!node)
        {
            CCLOG("[%s] node=null", label);
            return;
        }

        CCLOG(
            "[%s] node=%p id=%s class=%s visible=%d opacity=%u parent=%p children=%u",
            label,
            node,
            node->getID().c_str(),
            typeid(*node).name(),
            node->isVisible(),
            getNodeOpacity(node),
            node->getParent(),
            node->getChildren() ? static_cast<unsigned>(node->getChildren()->count()) : 0u);
    }

    static void dumpNodeTree(cocos2d::CCNode *node, int depth = 0)
    {
        if (!node)
            return;

        std::string indent(depth * 2, ' ');

        CCLOG(
            "%snode=%p class=%s id=%s visible=%d opacity=%u children=%u",
            indent.c_str(),
            node,
            typeid(*node).name(),
            node->getID().c_str(),
            node->isVisible(),
            getNodeOpacity(node),
            node->getChildren() ? static_cast<unsigned>(node->getChildren()->count()) : 0u);

        auto *children = node->getChildren();
        if (!children)
            return;

        for (unsigned i = 0; i < children->count(); ++i)
        {
            dumpNodeTree(
                static_cast<cocos2d::CCNode *>(children->objectAtIndex(i)),
                depth + 1);
        }
    }

    static void decorateCheckpoint(CheckpointObject *checkpoint)
    {
        auto settings = getSettings();

        if (!settings.enabled || !checkpoint)
            return;

        auto *phys = checkpoint->m_physicalCheckpointObject;
        if (!phys)
            return;

        if (phys->getChildByID(checkpoint_mod::OuterId))
            return;

        phys->m_addToNodeContainer = true;
        phys->stopAllActions();
        phys->unscheduleAllSelectors();

        logNodeState("before checkpoint", checkpoint);
        logNodeState("before phys", phys);

        hideNodeRecursive(checkpoint);
        hideNodeRecursive(phys);

        checkpoint->setVisible(false);
        phys->setVisible(false);

        if (auto *rgba = typeinfo_cast<cocos2d::CCRGBAProtocol *>(checkpoint))
            rgba->setOpacity(0);

        if (auto *rgba = typeinfo_cast<cocos2d::CCRGBAProtocol *>(phys))
            rgba->setOpacity(0);

        logNodeState("after checkpoint", checkpoint);
        logNodeState("after phys", phys);

        auto style = checkpoint_mod::getStyle();
        auto const &pack = checkpoint_mod::getPack(style.shape);

        auto *outer = cocos2d::CCSprite::create(pack.outerName);
        auto *inner = cocos2d::CCSprite::create(pack.innerName);

        if (!outer || !inner)
            return;

        outer->setID(checkpoint_mod::OuterId);
        inner->setID(checkpoint_mod::InnerId);

        outer->setCascadeOpacityEnabled(true);
        outer->setCascadeColorEnabled(true);
        inner->setCascadeOpacityEnabled(true);
        inner->setCascadeColorEnabled(true);

        if (settings.outerColor)
            outer->setColor(style.outerColor);

        if (settings.innerColor)
            inner->setColor(style.innerColor);

        outer->setOpacity(settings.opacity);
        inner->setOpacity(settings.opacity);

        if (settings.rainbow)
        {
            outer->runAction(rainbowAction(settings.rainbowSpeed));
            inner->runAction(rainbowAction(settings.rainbowSpeed));
        }

        auto nodeSize = phys->getContentSize();
        auto outerSize = outer->getContentSize();

        float scale = std::min(
                          nodeSize.width / std::max(outerSize.width, 1.f),
                          nodeSize.height / std::max(outerSize.height, 1.f)) *
                      style.scale;

        outer->setAnchorPoint({0.5f, 0.5f});
        inner->setAnchorPoint({0.5f, 0.5f});

        outer->setScale(scale);
        outer->setPosition({nodeSize.width * 0.5f, nodeSize.height * 0.5f});
        inner->setPosition({outerSize.width * 0.5f, outerSize.height * 0.5f});

        outer->addChild(inner);
        phys->addChild(outer, 1);

        CCLOG("[Practice Plus]: checkpoint decorated");
    }
    void storeCheckpoint(CheckpointObject *checkpoint)
    {
        PlayLayer::storeCheckpoint(checkpoint);
        decorateCheckpoint(checkpoint);
    }

    void loadFromCheckpoint(CheckpointObject *checkpoint)
    {
        PlayLayer::loadFromCheckpoint(checkpoint);
        decorateCheckpoint(checkpoint);
    }

    void resetLevel()
    {
        PlayLayer::resetLevel();

        if (m_checkpointArray && m_checkpointArray->count())
        {
            decorateCheckpoint(
                static_cast<CheckpointObject *>(
                    m_checkpointArray->lastObject()));
        }
    }
};