#include <algorithm>
#include <filesystem>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>

#include "CheckpointHelpers.hpp"

using namespace geode::prelude;

class $modify(CustomCheckpointsPlayLayer, PlayLayer)
{
    struct Fields
    {
        int placementIndex = 0;
    };

    static inline std::unordered_map<int, std::pair<int, cocos2d::CCNode *>> s_overlayMap;
    static inline int s_nextOverlayId = 1;
    static inline std::unordered_map<CheckpointObject *, int> s_cpToOid;

    struct Settings
    {
        bool enabled = false;
        bool outerColor = false;
        bool innerColor = false;
        bool rainbow = false;
        bool fade = false;
        bool fadeIn = false;

        float rainbowSpeed = 1.f;
        GLubyte opacity = 255;
        GLubyte fadeStep = 30;
        GLubyte fadeMin = 20;
        float fadeInDuration = 0.4f;

        std::filesystem::path checkpointImage;
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
            s.fade = mod->getSettingValue<bool>("checkpoint-fade-enabled");
            s.fadeIn = mod->getSettingValue<bool>("checkpoint-fadein-enabled");

            s.rainbowSpeed = std::max(
                0.01f,
                static_cast<float>(mod->getSettingValue<double>("checkpoint-rainbow-speed")));

            s.opacity = static_cast<GLubyte>(
                std::clamp<int64_t>(mod->getSettingValue<int64_t>("checkpoint-opacity"), 0, 255));

            s.fadeStep = static_cast<GLubyte>(
                std::clamp<int64_t>(mod->getSettingValue<int64_t>("checkpoint-fade-step"), 0, 255));

            s.fadeMin = static_cast<GLubyte>(
                std::clamp<int64_t>(mod->getSettingValue<int64_t>("checkpoint-fade-min"), 0, 255));

            s.fadeInDuration = std::max(
                0.05f,
                static_cast<float>(mod->getSettingValue<double>("checkpoint-fadein-duration")));

            s.checkpointImage = mod->getSettingValue<std::filesystem::path>("checkpoint-image");
        }

        return s;
    }

    static ccColor3B rainbowColorForIndex(int index)
    {
        static constexpr ccColor3B kRainbow[6] = {
            {255, 0, 0},
            {255, 255, 0},
            {0, 255, 0},
            {0, 255, 255},
            {0, 0, 255},
            {255, 0, 255},
        };
        return kRainbow[((index % 6) + 6) % 6];
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

    static cocos2d::CCSprite *createSpriteFromPathOrFallback(
        std::filesystem::path const &path,
        char const *fallback)
    {
        if (!path.empty())
        {
            auto pathStr = path.string();
            if (auto *sprite = cocos2d::CCSprite::create(pathStr.c_str()))
                return sprite;

            CCLOG("[Practice Plus]: failed to load custom checkpoint image: %s", pathStr.c_str());
        }

        return cocos2d::CCSprite::create(fallback);
    }

    void decorateCheckpoint(CheckpointObject *checkpoint, bool isNewPlacement)
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

        hideNodeRecursive(checkpoint);
        hideNodeRecursive(phys);

        checkpoint->setVisible(false);
        phys->setVisible(false);
        phys->makeInvisible();

        if (auto *rgba = typeinfo_cast<cocos2d::CCRGBAProtocol *>(checkpoint))
            rgba->setOpacity(0);

        if (auto *rgba = typeinfo_cast<cocos2d::CCRGBAProtocol *>(phys))
            rgba->setOpacity(0);

        auto style = checkpoint_mod::getStyle();
        auto const &pack = checkpoint_mod::getPack(style.shape);

        bool useCustomImage = !settings.checkpointImage.empty();

        cocos2d::CCSprite *outer = nullptr;
        cocos2d::CCSprite *inner = nullptr;

        if (useCustomImage)
        {
            outer = createSpriteFromPathOrFallback(settings.checkpointImage, pack.outerName);
        }
        else
        {
            outer = createSpriteFromPathOrFallback({}, pack.outerName);
            inner = createSpriteFromPathOrFallback({}, pack.innerName);
        }

        if (!outer || (!useCustomImage && !inner))
            return;

        outer->setID(checkpoint_mod::OuterId);
        outer->setTag(0x4F000000 + s_nextOverlayId);

        outer->setCascadeOpacityEnabled(true);
        outer->setCascadeColorEnabled(true);

        if (!useCustomImage)
        {
            inner->setID(checkpoint_mod::InnerId);
            inner->setCascadeOpacityEnabled(true);
            inner->setCascadeColorEnabled(true);
            inner->setTag(outer->getTag());

            int idx = m_fields->placementIndex;

            if (settings.rainbow)
            {
                auto color = rainbowColorForIndex(idx);
                outer->setColor(color);
                inner->setColor(color);
            }
            else
            {
                if (settings.outerColor)
                    outer->setColor(style.outerColor);

                if (settings.innerColor)
                    inner->setColor(style.innerColor);
            }
        }

        int idx = m_fields->placementIndex;

        GLubyte targetOpacity = settings.opacity;
        if (settings.fade)
        {
            int reduced = static_cast<int>(settings.opacity) - idx * static_cast<int>(settings.fadeStep);
            int floored = std::max(reduced, static_cast<int>(settings.fadeMin));
            targetOpacity = static_cast<GLubyte>(std::clamp(floored, 0, 255));
        }

        if (settings.fadeIn && isNewPlacement)
        {
            outer->setOpacity(0);
            outer->runAction(CCFadeTo::create(settings.fadeInDuration, targetOpacity));
        }
        else
        {
            outer->setOpacity(targetOpacity);
        }

        auto nodeSize = phys->getContentSize();
        auto outerSize = outer->getContentSize();

        float scale = std::min(
                          nodeSize.width / std::max(outerSize.width, 1.f),
                          nodeSize.height / std::max(outerSize.height, 1.f)) *
                      style.scale;

        outer->setAnchorPoint({0.5f, 0.5f});
        outer->setScale(scale);

        auto *overlayParent = phys->getParent();
        if (!overlayParent)
            overlayParent = m_objectLayer;

        if (overlayParent)
        {
            auto physAnchor = phys->getAnchorPoint();
            auto physPos = phys->getPosition();
            outer->setPosition({
                physPos.x + (0.5f - physAnchor.x) * nodeSize.width,
                physPos.y + (0.5f - physAnchor.y) * nodeSize.height,
            });

            if (!useCustomImage && inner)
            {
                inner->setAnchorPoint({0.5f, 0.5f});
                inner->setPosition({outerSize.width * 0.5f, outerSize.height * 0.5f});
                outer->addChild(inner);
            }

            overlayParent->addChild(outer, phys->getZOrder());

            int overlayId = s_nextOverlayId++;
            s_overlayMap[overlayId] = {checkpoint->m_uniqueID, outer};
            s_cpToOid[checkpoint] = overlayId;
        }
        else
        {
            outer->setPosition({nodeSize.width * 0.5f, nodeSize.height * 0.5f});

            if (!useCustomImage && inner)
            {
                inner->setAnchorPoint({0.5f, 0.5f});
                inner->setPosition({outerSize.width * 0.5f, outerSize.height * 0.5f});
                outer->addChild(inner);
            }

            phys->addChild(outer, 1);

            int overlayId = s_nextOverlayId++;
            s_overlayMap[overlayId] = {checkpoint->m_uniqueID, outer};
            s_cpToOid[checkpoint] = overlayId;
        }
    }

    void storeCheckpoint(CheckpointObject *checkpoint)
    {
        PlayLayer::storeCheckpoint(checkpoint);
        decorateCheckpoint(checkpoint, true);
        ++m_fields->placementIndex;
    }

    void loadFromCheckpoint(CheckpointObject *checkpoint)
    {
        PlayLayer::loadFromCheckpoint(checkpoint);
        decorateCheckpoint(checkpoint, false);
    }

    void resetLevel()
    {
        PlayLayer::resetLevel();
        m_fields->placementIndex = 0;

        if (m_checkpointArray && m_checkpointArray->count())
        {
            decorateCheckpoint(
                static_cast<CheckpointObject *>(m_checkpointArray->lastObject()),
                false);
        }
    }

    void removeCheckpoint(bool p0)
    {
        PlayLayer::removeCheckpoint(p0);

        std::unordered_set<CheckpointObject *> presentPtrs;
        if (m_checkpointArray)
        {
            for (unsigned i = 0; i < m_checkpointArray->count(); ++i)
            {
                auto *cp = static_cast<CheckpointObject *>(m_checkpointArray->objectAtIndex(i));
                if (cp)
                    presentPtrs.insert(cp);
            }
        }

        for (auto it = s_cpToOid.begin(); it != s_cpToOid.end();)
        {
            CheckpointObject *cp = it->first;
            int oid = it->second;

            if (presentPtrs.find(cp) == presentPtrs.end())
            {
                auto sit = s_overlayMap.find(oid);
                cocos2d::CCNode *node = nullptr;

                if (sit != s_overlayMap.end())
                    node = sit->second.second;

                if (node)
                {
                    auto *parent = node->getParent();
                    if (parent)
                    {
                        parent->removeChild(node, true);
                    }
                    else if (m_objectLayer)
                    {
                        auto *child = m_objectLayer->getChildByTag(0x4F000000 + oid);
                        if (child)
                            m_objectLayer->removeChildByTag(0x4F000000 + oid, true);
                    }
                }

                if (sit != s_overlayMap.end())
                    s_overlayMap.erase(sit);

                it = s_cpToOid.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }
};