#include "Checkpoint-Continuity.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/CheckpointObject.hpp>
#include <Geode/binding/EnhancedGameObject.hpp>
#include <Geode/binding/FMODAudioEngine.hpp>
#include <Geode/binding/GJBaseGameLayer.hpp>
#include <Geode/binding/GJGameLevel.hpp>
#include <Geode/binding/GameManager.hpp>
#include <Geode/binding/PlayerCheckpoint.hpp>
#include <Geode/binding/PlayerObject.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>

#include <algorithm>
#include <cmath>
#include <optional>
#include <unordered_map>
#include <vector>

using namespace geode::prelude;

namespace continuity
{
    static bool enabled()
    {
        return Mod::get()->getSettingValue<bool>("checkpoint-continuity-enabled");
    }

    static std::unordered_map<PlayerObject *, float> s_collisionSuppression;

#if CONTINUITY_DEBUG_LOG
    struct AuthoritativeProbe
    {
        PlayerObject *player = nullptr;
        cocos2d::CCPoint position = cocos2d::CCPointZero;
        int mode = 0;
        double gravity = 0.0;
        int nextFrame = 0;
    };

    static AuthoritativeProbe s_authoritativeProbe;

    static void clearAuthoritativeProbe()
    {
        s_authoritativeProbe = {};
    }

    static int probeModeID(PlayerObject *player)
    {
        if (!player)
            return 0;
        if (player->m_isShip)
            return 1;
        if (player->m_isBall)
            return 2;
        if (player->m_isBird)
            return 3;
        if (player->m_isDart)
            return 4;
        if (player->m_isRobot)
            return 5;
        if (player->m_isSpider)
            return 6;
        if (player->m_isSwing)
            return 7;
        return 0;
    }

    static void armAuthoritativeProbe(PlayerObject *player, PlayerSnapshot const &snapshot)
    {
        if (!player)
            return;

        s_authoritativeProbe = {
            player,
            snapshot.m_playerState.position,
            snapshot.m_modeID,
            snapshot.m_gravity,
            1,
        };
    }

    static void logAuthoritativeProbe(PlayLayer *layer)
    {
        auto &probe = s_authoritativeProbe;
        if (!probe.player || probe.nextFrame == 0)
            return;

        auto const position = probe.player->m_position;
        auto const mode = probeModeID(probe.player);
        auto const gravity = probe.player->m_gravity;
        auto const positionChanged = position != probe.position;
        auto const modeChanged = mode != probe.mode;
        auto const gravityChanged = gravity != probe.gravity;
        log::info("[Continuity] authoritative-probe frame={} pos=({}, {}) mode={} gravity={} changed(position={}, mode={}, gravity={})",
                  probe.nextFrame, position.x, position.y, mode, gravity,
                  positionChanged, modeChanged, gravityChanged);

        if (probe.nextFrame++ == 5)
            probe = {};
    }
#endif

    static void suppressCollisionDispatch(PlayerObject *player, float seconds)
    {
        if (player && seconds > 0.0f)
            s_collisionSuppression[player] = std::max(s_collisionSuppression[player], seconds);
    }

    static bool collisionDispatchSuppressed(PlayerObject *player)
    {
        return player && s_collisionSuppression.contains(player);
    }

    static void advanceCollisionSuppression(float dt)
    {
        for (auto it = s_collisionSuppression.begin(); it != s_collisionSuppression.end();)
        {
            it->second -= std::max(0.0f, dt);
            if (it->second <= 0.0f)
                it = s_collisionSuppression.erase(it);
            else
                ++it;
        }
    }

    static void clearCollisionSuppression()
    {
        s_collisionSuppression.clear();
    }

    static bool checkpointCanBeRemoved(CheckpointObject *checkpoint)
    {
        if (!checkpoint)
            return false;

        auto *physical = checkpoint->m_physicalCheckpointObject;
        return physical && (physical->getParent() || checkpoint->getParent());
    }

    static int playerIconID(int mode)
    {
        auto *gameManager = GameManager::sharedState();
        if (!gameManager)
            return -1;

        switch (mode)
        {
        case 1:
            return gameManager->getPlayerShip();
        case 2:
            return gameManager->getPlayerBall();
        case 3:
            return gameManager->getPlayerBird();
        case 4:
            return gameManager->getPlayerDart();
        case 5:
            return gameManager->getPlayerRobot();
        case 6:
            return gameManager->getPlayerSpider();
        case 7:
            return gameManager->getPlayerSwing();
        default:
            return gameManager->getPlayerFrame();
        }
    }

    static void updatePlayerIcon(PlayerObject *player, int mode)
    {
        if (!player)
            return;

        auto icon = playerIconID(mode);
        if (icon < 0)
            return;

        switch (mode)
        {
        case 1:
            player->updatePlayerShipFrame(icon);
            break;
        case 2:
            player->updatePlayerRollFrame(icon);
            break;
        case 3:
            player->updatePlayerBirdFrame(icon);
            break;
        case 4:
            player->updatePlayerDartFrame(icon);
            break;
        case 5:
            player->updatePlayerRobotFrame(icon);
            break;
        case 6:
            player->updatePlayerSpiderFrame(icon);
            break;
        case 7:
            player->updatePlayerSwingFrame(icon);
            break;
        default:
            player->updatePlayerFrame(icon);
            break;
        }
    }

    struct CheckpointRecord
    {
        CheckpointObject *checkpoint = nullptr;
        PlayerSnapshot snapshot;
    };

    struct LevelSignature
    {
        int version = 0;
        int timestamp = 0;
        int length = 0;
        size_t levelStringSize = 0;

        bool operator==(LevelSignature const &other) const
        {
            return version == other.version && timestamp == other.timestamp &&
                   length == other.length && levelStringSize == other.levelStringSize;
        }
    };

    struct LevelState
    {
        std::vector<CheckpointRecord> records;
        std::optional<PlayerSnapshot> latestCheckpoint;
        std::optional<PlayerSnapshot> entrySnapshot;
        std::optional<PlayerSnapshot> exitSnapshot;
        std::optional<LevelSignature> signature;
        bool teardownPending = false;
        bool quitConfirmed = false;
    };

#if CONTINUITY_DEBUG_LOG
    static void logSnapshot(char const *event, PlayLayer *layer, PlayerSnapshot const &snapshot)
    {
        auto checkpointCount = layer && layer->m_checkpointArray
                                   ? static_cast<int>(layer->m_checkpointArray->count())
                                   : 0;
        auto level = layer && layer->m_level ? layer->m_level->m_levelID.value() : -1;
        log::info("[Continuity] {} level={} pos=({}, {}) mode={} gravity={} yVelocity={} dual={} checkpoints={}",
                  event, level, snapshot.m_playerState.position.x, snapshot.m_playerState.position.y,
                  snapshot.m_modeID, snapshot.m_gravity, snapshot.m_yVelocity,
                  snapshot.m_isDualMode, checkpointCount);
    }
#endif

    class StateManager final
    {
    public:
        enum class RestoreMode
        {
            Full,
            TransformOnly,
            PhysicsOnly,
        };

        static int currentModeID(PlayerObject *player)
        {
            return player ? modeID(player) : 0;
        }

    private:
        static constexpr double kMaxGravityMagnitude = 10.0;
        static constexpr double kMinGravityMagnitude = 1e-6;
        static constexpr double kMaxYVelocity = 50.0;

        static inline std::unordered_map<int, LevelState> s_levelStates;
        inline static bool s_restoringCheckpoints = false;

        static LevelSignature signature(PlayLayer *layer)
        {
            auto *level = layer && layer->m_level ? layer->m_level : nullptr;
            if (!level)
                return {};

            return LevelSignature{
                level->m_levelVersion,
                level->m_timestamp,
                level->m_levelLength,
                level->m_levelString.size(),
            };
        }

        static bool signatureMatches(PlayLayer *layer, LevelState const &state)
        {
            return state.signature && *state.signature == signature(layer);
        }

        static void sanitizeForPersistence(PlayerState &state)
        {
            state.plMembers.m_lastCollisionBottom = 0;
            state.plMembers.m_lastCollisionTop = 0;
            state.plMembers.m_lastCollisionLeft = 0;
            state.plMembers.m_lastCollisionRight = 0;
            state.plMembers.m_groundYVelocity = 0.0;
            state.plMembers.m_collidedTopMinY = 0.0;
            state.plMembers.m_collidedBottomMaxY = 0.0;
            state.plMembers.m_isOnGround = false;
            state.plMembers.m_isOnGround2 = false;
            state.plMembers.m_isOnGround3 = false;
            state.plMembers.m_isOnGround4 = false;
            state.plMembers.m_isOnSlope = false;
            state.plMembers.m_wasOnSlope = false;
            state.plMembers.m_unkAngle1 = 0.0f;
            state.plMembers.m_dashRing = nullptr;
            state.plMembers.m_maybeLastGroundObject = nullptr;
            state.plMembers.m_currentSlope2 = nullptr;
            state.plMembers.m_preLastGroundObject = nullptr;
            state.plMembers.m_collidedObject = nullptr;
            state.plMembers.m_lastGroundObject = nullptr;
            state.plMembers.m_collidingWithLeft = nullptr;
            state.plMembers.m_collidingWithRight = nullptr;
            state.plMembers.m_objectSnappedTo = nullptr;
            state.plMembers.m_lastActivatedPortal = nullptr;
            state.plMembers.m_currentPotentialSlope = nullptr;
            state.plMembers.m_currentSlope = nullptr;
        }

        static double sanitizeGravity(double gravity)
        {
            return std::isfinite(gravity) && std::abs(gravity) <= kMaxGravityMagnitude && std::abs(gravity) >= kMinGravityMagnitude
                       ? gravity
                       : 0.0;
        }

        static double sanitizeYVelocity(double velocity)
        {
            return std::isfinite(velocity) && std::abs(velocity) <= kMaxYVelocity ? velocity : 0.0;
        }

        static int modeID(PlayerObject *player)
        {
            if (player->m_isShip)
                return 1;
            if (player->m_isBall)
                return 2;
            if (player->m_isBird)
                return 3;
            if (player->m_isDart)
                return 4;
            if (player->m_isRobot)
                return 5;
            if (player->m_isSpider)
                return 6;
            if (player->m_isSwing)
                return 7;
            return 0;
        }

        static int modeID(PlayerState::PlayerObjectMembers const &state)
        {
            if (state.m_isShip)
                return 1;
            if (state.m_isBall)
                return 2;
            if (state.m_isBird)
                return 3;
            if (state.m_isDart)
                return 4;
            if (state.m_isRobot)
                return 5;
            if (state.m_isSpider)
                return 6;
            if (state.m_isSwing)
                return 7;
            return 0;
        }

        static int groundModeID(PlayerObject *player, int mode)
        {
            if (!player)
                return mode;

            if (player->m_isBall || mode == 2)
                return 16;
            if (player->m_isShip || mode == 1)
                return 5;
            return 6;
        }

        static void normalizeModeFlags(PlayerState::PlayerObjectMembers &state, int mode)
        {
            state.m_isShip = mode == 1;
            state.m_isBall = mode == 2;
            state.m_isBird = mode == 3;
            state.m_isDart = mode == 4;
            state.m_isRobot = mode == 5;
            state.m_isSpider = mode == 6;
            state.m_isSwing = mode == 7;
        }

        static void normalizeModeFlags(PlayerObject *player, int mode)
        {
            if (!player)
                return;

            player->m_isShip = mode == 1;
            player->m_isBall = mode == 2;
            player->m_isBird = mode == 3;
            player->m_isDart = mode == 4;
            player->m_isRobot = mode == 5;
            player->m_isSpider = mode == 6;
            player->m_isSwing = mode == 7;
        }

        static GameObjectType modeObjectType(int mode)
        {
            switch (mode)
            {
            case 1:
                return GameObjectType::ShipPortal;
            case 2:
                return GameObjectType::BallPortal;
            case 3:
                return GameObjectType::UfoPortal;
            case 4:
                return GameObjectType::WavePortal;
            case 5:
                return GameObjectType::RobotPortal;
            case 6:
                return GameObjectType::SpiderPortal;
            case 7:
                return GameObjectType::SwingPortal;
            default:
                return GameObjectType::CubePortal;
            }
        }

        static bool portalAnimationActive(GJBaseGameLayer *baseLayer)
        {
            if (!baseLayer)
                return false;

            auto const &tweens = baseLayer->m_gameState.m_tweenActions;
            auto it = tweens.find(9);
            return it != tweens.end() && !it->second.m_finished && !it->second.m_disabled;
        }

        static void restoreCameraState(PlayLayer *layer, PlayerSnapshot const &snapshot)
        {
            if (!layer)
                return;

            auto &state = layer->m_gameState;
            state.m_cameraZoom = snapshot.m_cameraZoom;
            state.m_targetCameraZoom = snapshot.m_targetCameraZoom;
            state.m_cameraOffset = snapshot.m_cameraOffset;
            state.m_cameraPosition = snapshot.m_cameraPosition;
            state.m_cameraPosition2 = snapshot.m_cameraPosition2;
            state.m_cameraStepDiff = snapshot.m_cameraStepDiff;
            state.m_cameraAngle = snapshot.m_cameraAngle;
            state.m_targetCameraAngle = snapshot.m_targetCameraAngle;
        }

        static void restorePlayerCore(GJBaseGameLayer *baseLayer, PlayerObject *player, PlayerState const &saved,
                                      int mode, double gravity, float gravityMod, double yVelocity,
                                      bool driveGroundUpdate)
        {
            if (!player)
                return;

            saved.loadState(player);
            player->m_isUpsideDown = saved.plMembers.m_isUpsideDown;
            player->m_isPlatformer = saved.plMembers.m_isPlatformer;
            player->m_isGoingLeft = saved.plMembers.m_isGoingLeft;
            player->m_isSideways = saved.plMembers.m_isSideways;
            player->m_reverseRelated = saved.plMembers.m_reverseRelated;
            player->m_maybeReverseSpeed = saved.plMembers.m_maybeReverseSpeed;
            player->m_maybeReverseAcceleration = saved.plMembers.m_maybeReverseAcceleration;
            player->m_isDashing = saved.plMembers.m_isDashing;
            player->m_vehicleSize = saved.plMembers.m_vehicleSize;
            player->m_playerSpeed = saved.plMembers.m_playerSpeed;
            player->m_platformerVelocityRelated = saved.plMembers.m_platformerVelocityRelated;
            player->m_gravity = sanitizeGravity(gravity);
            player->m_gravityMod = gravityMod;
            player->m_fixGravityBug = saved.plMembers.m_fixGravityBug;
            player->m_slopeFlipGravityRelated = saved.plMembers.m_slopeFlipGravityRelated;
            player->m_stateFlipGravity = saved.plMembers.m_stateFlipGravity;
            player->m_platformerXVelocity = saved.plMembers.m_platformerXVelocity;
            normalizeModeFlags(player, mode);
            (void)baseLayer;
            (void)driveGroundUpdate;

            player->setPosition(saved.position);
            player->m_position = saved.position;
            player->m_positionX = saved.position.x;
            player->m_positionY = saved.position.y;
            player->setRotation(saved.rotation);
            player->setRotationX(saved.rotationX);
            player->setRotationY(saved.rotationY);
            player->setScaleX(saved.scaleX);
            player->setScaleY(saved.scaleY);
            if (auto *obb = player->m_orientedBox)
            {
                obb->m_corners = saved.obbMembers.m_corners;
                player->m_isOrientedBoxDirty = false;
            }

            player->setYVelocity(sanitizeYVelocity(yVelocity), 0);
        }

        static void restoreGroundState(PlayerObject *player, PlayerSnapshot const &snapshot, bool secondPlayer)
        {
            if (!player)
                return;

            player->m_isOnGround = secondPlayer ? snapshot.m_player2IsOnGround : snapshot.m_isOnGround;
            player->m_isOnGround2 = secondPlayer ? snapshot.m_player2IsOnGround2 : snapshot.m_isOnGround2;
            player->m_isOnGround3 = secondPlayer ? snapshot.m_player2IsOnGround3 : snapshot.m_isOnGround3;
            player->m_isOnGround4 = secondPlayer ? snapshot.m_player2IsOnGround4 : snapshot.m_isOnGround4;
            player->m_isOnSlope = secondPlayer ? snapshot.m_player2IsOnSlope : snapshot.m_isOnSlope;
            player->m_wasOnSlope = secondPlayer ? snapshot.m_player2WasOnSlope : snapshot.m_wasOnSlope;
            player->m_groundYVelocity = secondPlayer ? snapshot.m_player2GroundYVelocity : snapshot.m_groundYVelocity;
            player->m_collidedTopMinY = secondPlayer ? snapshot.m_player2CollidedTopMinY : snapshot.m_collidedTopMinY;
            player->m_collidedBottomMaxY = secondPlayer ? snapshot.m_player2CollidedBottomMaxY : snapshot.m_collidedBottomMaxY;
        }

        static void restorePosition(PlayerObject *player, cocos2d::CCPoint position)
        {
            if (!player)
                return;

            player->setPosition(position);
            player->m_position = position;
            player->m_positionX = position.x;
            player->m_positionY = position.y;
        }

        static bool validate(PlayLayer *layer, PlayerSnapshot const &snap, char const *context)
        {
            if (layer && layer->m_player1 && layer->m_level && snap.m_isValid &&
                (!snap.m_hasPlayer2 || layer->m_player2))
                return true;

            log::warn("[Continuity] {} skipped: missing layer/player/level or invalid snapshot", context);
            return false;
        }

        static std::vector<PersistedInteractionState> captureInteractionState(PlayLayer *layer)
        {
            std::vector<PersistedInteractionState> result;
            if (!layer)
                return result;

            gd::vector<SavedActiveObjectState> activeObjects;
            gd::vector<SavedSpecialObjectState> specialObjects;
            layer->saveActiveSaveObjects(activeObjects, specialObjects);
            result.reserve(activeObjects.size());

            for (auto const &saved : activeObjects)
            {
                auto *object = typeinfo_cast<EnhancedGameObject *>(saved.m_gameObject);
                if (!object)
                    continue;

                result.push_back({
                    object->m_uniqueID,
                    object->m_objectID,
                    object->m_positionX,
                    object->m_positionY,
                    saved.m_activatedByPlayer1,
                    saved.m_activatedByPlayer2,
                });
            }
            return result;
        }

        static void applyInteractionState(PlayLayer *layer, PlayerSnapshot const &snapshot)
        {
            if (!layer || !layer->m_objects || snapshot.m_persistedInteractionStates.empty())
                return;

            for (unsigned i = 0; i < layer->m_objects->count(); ++i)
            {
                auto *object = typeinfo_cast<EnhancedGameObject *>(layer->m_objects->objectAtIndex(i));
                if (!object)
                    continue;

                for (auto const &saved : snapshot.m_persistedInteractionStates)
                {
                    if (object->m_uniqueID != saved.m_uniqueID ||
                        object->m_objectID != saved.m_objectID ||
                        std::abs(object->m_positionX - saved.m_positionX) > 0.01 ||
                        std::abs(object->m_positionY - saved.m_positionY) > 0.01)
                        continue;

                    object->m_activatedByPlayer1 = saved.m_activatedByPlayer1;
                    object->m_activatedByPlayer2 = saved.m_activatedByPlayer2;
                    break;
                }
            }
        }

        static PlayerSnapshot capture(PlayLayer *layer)
        {
            PlayerSnapshot snap{};
            if (!layer || !layer->m_player1 || !layer->m_level)
                return snap;

            auto *p1 = layer->m_player1;
            snap.m_playerState.saveState(p1);
            snap.m_isOnGround = p1->m_isOnGround;
            snap.m_isOnGround2 = p1->m_isOnGround2;
            snap.m_isOnGround3 = p1->m_isOnGround3;
            snap.m_isOnGround4 = p1->m_isOnGround4;
            snap.m_isOnSlope = p1->m_isOnSlope;
            snap.m_wasOnSlope = p1->m_wasOnSlope;
            snap.m_groundYVelocity = p1->m_groundYVelocity;
            snap.m_collidedTopMinY = p1->m_collidedTopMinY;
            snap.m_collidedBottomMaxY = p1->m_collidedBottomMaxY;
            sanitizeForPersistence(snap.m_playerState);
            snap.m_gravity = sanitizeGravity(p1->m_gravity);
            snap.m_gravityMod = p1->m_gravityMod;
            snap.m_yVelocity = sanitizeYVelocity(p1->m_yVelocity);
            snap.m_modeID = modeID(p1);
            normalizeModeFlags(snap.m_playerState.plMembers, snap.m_modeID);

            auto *baseLayer = static_cast<GJBaseGameLayer *>(layer);
            snap.m_portalY = layer->m_gameState.m_portalY;
            snap.m_middleGroundOffsetY = layer->m_gameState.m_middleGroundOffsetY;
            snap.m_dualRelated = layer->m_gameState.m_dualRelated;
            snap.m_groundHeight = baseLayer->getGroundHeight(p1, groundModeID(p1, snap.m_modeID));
            snap.m_portalMinY = baseLayer->getMinPortalY();
            snap.m_portalMaxY = baseLayer->getMaxPortalY();
            snap.m_boundsValid = std::isfinite(snap.m_groundHeight);

            if (auto *fmod = FMODAudioEngine::sharedEngine())
                snap.m_musicTimeMS = fmod->getMusicTimeMS(0);

            snap.m_persistedInteractionStates = captureInteractionState(layer);

            snap.m_isDualMode = layer->m_gameState.m_isDualMode;
            snap.m_isMirrored = layer->m_gameState.m_unkBool10;
            snap.m_cameraZoom = layer->m_gameState.m_cameraZoom;
            snap.m_targetCameraZoom = layer->m_gameState.m_targetCameraZoom;
            snap.m_cameraOffset = layer->m_gameState.m_cameraOffset;
            snap.m_cameraPosition = layer->m_gameState.m_cameraPosition;
            snap.m_cameraPosition2 = layer->m_gameState.m_cameraPosition2;
            snap.m_cameraStepDiff = layer->m_gameState.m_cameraStepDiff;
            snap.m_cameraAngle = layer->m_gameState.m_cameraAngle;
            snap.m_targetCameraAngle = layer->m_gameState.m_targetCameraAngle;
            snap.m_timeWarp = layer->m_gameState.m_timeWarp;
            snap.m_queuedTimeWarp = layer->m_gameState.m_queuedTimeWarp;
            snap.m_timeWarpRelated = layer->m_gameState.m_timeWarpRelated;
            snap.m_timeModRelated = layer->m_gameState.m_timeModRelated;
            snap.m_timeModRelated2 = layer->m_gameState.m_timeModRelated2;
            snap.m_totalTime = layer->m_gameState.m_totalTime;
            snap.m_levelTime = layer->m_gameState.m_levelTime;
            snap.m_currentProgress = layer->m_gameState.m_currentProgress;
            snap.m_currentChannel = layer->m_gameState.m_currentChannel;
            snap.m_rotateChannel = layer->m_gameState.m_rotateChannel;
            snap.m_levelFlipping = layer->m_gameState.m_levelFlipping;

            if (snap.m_isDualMode && baseLayer->m_player2)
            {
                auto *p2 = baseLayer->m_player2;
                snap.m_player2State.saveState(p2);
                snap.m_player2IsOnGround = p2->m_isOnGround;
                snap.m_player2IsOnGround2 = p2->m_isOnGround2;
                snap.m_player2IsOnGround3 = p2->m_isOnGround3;
                snap.m_player2IsOnGround4 = p2->m_isOnGround4;
                snap.m_player2IsOnSlope = p2->m_isOnSlope;
                snap.m_player2WasOnSlope = p2->m_wasOnSlope;
                snap.m_player2GroundYVelocity = p2->m_groundYVelocity;
                snap.m_player2CollidedTopMinY = p2->m_collidedTopMinY;
                snap.m_player2CollidedBottomMaxY = p2->m_collidedBottomMaxY;
                sanitizeForPersistence(snap.m_player2State);
                snap.m_player2Gravity = sanitizeGravity(p2->m_gravity);
                snap.m_player2GravityMod = p2->m_gravityMod;
                snap.m_player2YVelocity = sanitizeYVelocity(p2->m_yVelocity);
                snap.m_player2ModeID = modeID(p2);
                normalizeModeFlags(snap.m_player2State.plMembers, snap.m_player2ModeID);
                snap.m_hasPlayer2 = true;
            }
            snap.m_isValid = true;

            return snap;
        }

        static void inheritBounds(PlayerSnapshot &snapshot, PlayerSnapshot const *previous)
        {
            if (snapshot.m_boundsValid || !previous || !previous->m_boundsValid ||
                snapshot.m_modeID != previous->m_modeID)
                return;

            snapshot.m_groundHeight = previous->m_groundHeight;
            snapshot.m_portalMinY = previous->m_portalMinY;
            snapshot.m_portalMaxY = previous->m_portalMaxY;
            snapshot.m_dualRelated = previous->m_dualRelated;
            snapshot.m_boundsValid = true;
        }

        static void finalizeApply(PlayLayer *layer, PlayerSnapshot const &snap, bool wasFullRestore)
        {
            auto *p1 = layer->m_player1;
            auto *baseLayer = static_cast<GJBaseGameLayer *>(layer);
            p1->m_gravity = sanitizeGravity(p1->m_gravity);

            if (auto *obb = p1->m_orientedBox)
            {
                obb->m_corners = snap.m_playerState.obbMembers.m_corners;
                p1->m_isOrientedBoxDirty = false;
            }

            layer->m_gameState.m_isDualMode = snap.m_isDualMode;
            layer->m_gameState.m_middleGroundOffsetY = snap.m_middleGroundOffsetY;
            layer->m_gameState.m_dualRelated = snap.m_dualRelated;
            layer->m_gameState.m_unkBool10 = snap.m_isMirrored;
            restoreCameraState(layer, snap);
            layer->m_gameState.m_timeWarp = snap.m_timeWarp;
            layer->m_gameState.m_queuedTimeWarp = snap.m_queuedTimeWarp;
            layer->m_gameState.m_timeWarpRelated = snap.m_timeWarpRelated;
            layer->m_gameState.m_timeModRelated = snap.m_timeModRelated;
            layer->m_gameState.m_timeModRelated2 = snap.m_timeModRelated2;
            baseLayer->updateTimeWarp(snap.m_timeWarp);
            baseLayer->applyTimeWarp(snap.m_timeWarp);
            layer->m_gameState.m_totalTime = snap.m_totalTime;
            layer->m_gameState.m_levelTime = snap.m_levelTime;
            layer->m_gameState.m_currentProgress = snap.m_currentProgress;
            layer->m_gameState.m_currentChannel = snap.m_currentChannel;
            layer->m_gameState.m_rotateChannel = snap.m_rotateChannel;
            layer->m_gameState.m_levelFlipping = snap.m_levelFlipping;
            auto groundMode = snap.m_isDualMode ? snap.m_dualRelated : groundModeID(p1, snap.m_modeID);
            baseLayer->updateDualGround(p1, groundMode, true, 0.0f);
            if (snap.m_hasPlayer2 && layer->m_player2)
                baseLayer->updateDualGround(layer->m_player2, snap.m_dualRelated, true, 0.0f);

            restorePosition(p1, snap.m_playerState.position);
            if (snap.m_hasPlayer2)
                restorePosition(layer->m_player2, snap.m_player2State.position);

            if (!wasFullRestore)
            {
                if (auto *fmod = FMODAudioEngine::sharedEngine())
                    fmod->setMusicTimeMS(snap.m_musicTimeMS, true, 0);
            }

            layer->updateCamera(0.0f);
            restoreCameraState(layer, snap);
            baseLayer->updateMaxGameplayY();
        }

        static void applyPersistentPhysicsForPlayer(PlayerObject *player, PlayerState const &saved,
                                                    double gravity, float gravityMod, double yVelocity)
        {
            if (!player)
                return;

            player->m_gravity = sanitizeGravity(gravity);
            player->m_gravityMod = gravityMod;
            player->m_wasTeleported = saved.plMembers.m_wasTeleported;
            player->m_fixGravityBug = saved.plMembers.m_fixGravityBug;
            player->m_reverseSync = saved.plMembers.m_reverseSync;
            player->setYVelocity(sanitizeYVelocity(yVelocity), 0);
            player->m_stateFlipGravity = saved.plMembers.m_stateFlipGravity;
            player->m_touchedGravityPortal = saved.plMembers.m_touchedGravityPortal;
            player->m_slopeFlipGravityRelated = saved.plMembers.m_slopeFlipGravityRelated;
        }

        static void applyPersistentPhysics(PlayLayer *layer, PlayerSnapshot const &snap)
        {
            if (!layer || !layer->m_player1 || !snap.m_isValid)
                return;

            applyPersistentPhysicsForPlayer(layer->m_player1, snap.m_playerState,
                                            snap.m_gravity, snap.m_gravityMod, snap.m_yVelocity);
            if (snap.m_hasPlayer2)
                applyPersistentPhysicsForPlayer(layer->m_player2, snap.m_player2State,
                                                snap.m_player2Gravity, snap.m_player2GravityMod,
                                                snap.m_player2YVelocity);
        }

        static void suppressAutoCheckpointPlacementInternal(PlayerObject *player)
        {
            if (!player)
                return;

            player->m_canPlaceCheckpoint = false;
            player->m_shouldTryPlacingCheckpoint = false;
            player->m_checkpointTimeout = false;
            player->m_onFlyCheckpointTries = 0;
        }

        static void fillCheckpoint(PlayerCheckpoint *checkpoint, PlayerState const &saved,
                                   double gravity, float gravityMod, double yVelocity)
        {
            if (!checkpoint)
                return;

            auto const &state = saved.plMembers;
            checkpoint->m_position = saved.position;
            checkpoint->m_lastPosition = saved.position;
            checkpoint->m_yVelocityUnrounded = sanitizeYVelocity(yVelocity);
            checkpoint->m_yVelocity = sanitizeYVelocity(yVelocity);
            checkpoint->m_isUpsideDown = state.m_isUpsideDown;
            checkpoint->m_isSideways = state.m_isSideways;
            checkpoint->m_isShip = state.m_isShip;
            checkpoint->m_isBall = state.m_isBall;
            checkpoint->m_isBird = state.m_isBird;
            checkpoint->m_isSwing = state.m_isSwing;
            checkpoint->m_isDart = state.m_isDart;
            checkpoint->m_isRobot = state.m_isRobot;
            checkpoint->m_isSpider = state.m_isSpider;
            checkpoint->m_isGoingLeft = state.m_isGoingLeft;
            checkpoint->m_isDashing = state.m_isDashing;
            checkpoint->m_platformerCheckpoint = state.m_isPlatformer;
            checkpoint->m_playerSpeed = state.m_playerSpeed;
            checkpoint->m_maybeReverseSpeed = state.m_maybeReverseSpeed;
            checkpoint->m_gravityMod = gravityMod;
            checkpoint->m_gravity = sanitizeGravity(gravity);
            checkpoint->m_wasTeleported = state.m_wasTeleported;
            checkpoint->m_fixGravityBug = state.m_fixGravityBug;
            checkpoint->m_reverseSync = state.m_reverseSync;
        }

        static void fillCheckpointGameState(CheckpointObject *checkpoint, PlayerSnapshot const &snapshot)
        {
            if (!checkpoint)
                return;

            auto &state = checkpoint->m_gameState;
            state.m_isDualMode = snapshot.m_isDualMode;
            state.m_middleGroundOffsetY = snapshot.m_middleGroundOffsetY;
            state.m_dualRelated = snapshot.m_dualRelated;
            state.m_unkBool10 = snapshot.m_isMirrored;
            state.m_cameraZoom = snapshot.m_cameraZoom;
            state.m_targetCameraZoom = snapshot.m_targetCameraZoom;
            state.m_cameraOffset = snapshot.m_cameraOffset;
            state.m_cameraPosition = snapshot.m_cameraPosition;
            state.m_cameraPosition2 = snapshot.m_cameraPosition2;
            state.m_cameraStepDiff = snapshot.m_cameraStepDiff;
            state.m_cameraAngle = snapshot.m_cameraAngle;
            state.m_targetCameraAngle = snapshot.m_targetCameraAngle;
            state.m_timeWarp = snapshot.m_timeWarp;
            state.m_queuedTimeWarp = snapshot.m_queuedTimeWarp;
            state.m_timeWarpRelated = snapshot.m_timeWarpRelated;
            state.m_timeModRelated = snapshot.m_timeModRelated;
            state.m_timeModRelated2 = snapshot.m_timeModRelated2;
            state.m_totalTime = snapshot.m_totalTime;
            state.m_levelTime = snapshot.m_levelTime;
            state.m_currentProgress = snapshot.m_currentProgress;
            state.m_currentChannel = snapshot.m_currentChannel;
            state.m_rotateChannel = snapshot.m_rotateChannel;
            state.m_levelFlipping = snapshot.m_levelFlipping;

            if (!snapshot.m_hasCheckpointLevelState)
                return;

            checkpoint->m_commandIndex = snapshot.m_checkpointCommandIndex;
        }

        static void captureCheckpointLevelState(PlayerSnapshot &snapshot, CheckpointObject *checkpoint)
        {
            if (!checkpoint)
                return;

            snapshot.m_checkpointCommandIndex = checkpoint->m_commandIndex;
            snapshot.m_hasCheckpointLevelState = true;
        }

        static void clearTransientGroundState(PlayerObject *p1)
        {
            if (!p1)
                return;

            auto position = p1->getPosition();
            p1->m_lastCollisionBottom = 0;
            p1->m_lastCollisionTop = 0;
            p1->m_lastCollisionLeft = 0;
            p1->m_lastCollisionRight = 0;
            p1->m_dashRing = nullptr;
            p1->m_maybeLastGroundObject = nullptr;
            p1->m_currentSlope2 = nullptr;
            p1->m_preLastGroundObject = nullptr;
            p1->m_collidedObject = nullptr;
            p1->m_lastGroundObject = nullptr;
            p1->m_collidingWithLeft = nullptr;
            p1->m_collidingWithRight = nullptr;
            p1->m_objectSnappedTo = nullptr;
            p1->m_pendingCheckpoint = nullptr;
            p1->m_lastActivatedPortal = nullptr;
            p1->m_currentPotentialSlope = nullptr;
            p1->m_currentSlope = nullptr;
            p1->m_groundYVelocity = 0.0;
            p1->m_yVelocityRelated = 0.0;
            p1->m_fallSpeed = 0.0;
            p1->m_lastGroundedPos = position;
            p1->m_fallStartY = position.y;
            p1->m_groundObjectMaterial = 0;
            p1->m_maybeIsFalling = false;
            p1->m_isCollidingWithSlope = false;
            p1->m_maybeIsColliding = false;
            p1->m_isCurrentSlopeTop = false;
            p1->m_isSliding = false;
            p1->m_isOnIce = false;
            p1->m_isOnGround = false;
            p1->m_isOnGround2 = false;
            p1->m_isOnGround3 = false;
            p1->m_isOnGround4 = false;
            p1->m_isOnSlope = false;
            p1->m_wasOnSlope = false;
            p1->m_stateOnGround = 0;
        }

        static void removeStalePlayer2(PlayLayer *layer, PlayerSnapshot const &snap)
        {
            (void)layer;
            (void)snap;
        }

        static std::optional<PlayerSnapshot> getLatestCheckpointSnapshot(int levelID)
        {
            auto it = s_levelStates.find(levelID);
            if (it == s_levelStates.end())
                return std::nullopt;

            if (it->second.latestCheckpoint && it->second.latestCheckpoint->m_isValid)
                return it->second.latestCheckpoint;
            if (it->second.records.empty())
                return std::nullopt;

            auto const &snapshot = it->second.records.back().snapshot;
            return snapshot.m_isValid ? std::optional<PlayerSnapshot>(snapshot) : std::nullopt;
        }

        static std::optional<PlayerSnapshot> firstValid(std::initializer_list<std::optional<PlayerSnapshot> const *> snapshots)
        {
            for (auto *snapshot : snapshots)
                if (snapshot && snapshot->has_value() && (*snapshot)->m_isValid)
                    return **snapshot;
            return std::nullopt;
        }

        static void clearLevel(int levelID)
        {
            s_levelStates.erase(levelID);
        }

        static void syncRecordsToLiveCheckpoints(PlayLayer *layer, LevelState &state)
        {
            if (!layer || !layer->m_checkpointArray)
                return;

            std::vector<CheckpointRecord> liveRecords;
            liveRecords.reserve(layer->m_checkpointArray->count());
            for (unsigned i = 0; i < layer->m_checkpointArray->count(); ++i)
            {
                auto *checkpoint = static_cast<CheckpointObject *>(
                    layer->m_checkpointArray->objectAtIndex(i));
                auto record = std::find_if(state.records.begin(), state.records.end(),
                                           [checkpoint](CheckpointRecord const &candidate)
                                           {
                                               return candidate.checkpoint == checkpoint;
                                           });
                if (record != state.records.end())
                    liveRecords.push_back(*record);
            }
            state.records = std::move(liveRecords);
        }

    public:
        static bool canRemoveCheckpoint(CheckpointObject *checkpoint)
        {
            return checkpointCanBeRemoved(checkpoint);
        }

        static void suppressAutoCheckpointPlacement(PlayLayer *layer)
        {
            if (!layer)
                return;

            suppressAutoCheckpointPlacementInternal(layer->m_player1);
            suppressAutoCheckpointPlacementInternal(layer->m_player2);
        }

        static int levelID(PlayLayer *layer)
        {
            return layer && layer->m_level ? layer->m_level->m_levelID.value() : -1;
        }

        static void abandon(PlayLayer *layer)
        {
            if (layer)
                clearLevel(levelID(layer));
        }

        static void markTeardownPending(PlayLayer *layer, char const *source, bool logLiveState = false)
        {
            if (!layer)
                return;

            auto it = s_levelStates.find(levelID(layer));
            if (it != s_levelStates.end() && !it->second.quitConfirmed)
            {
                if (!it->second.teardownPending)
                {
                    it->second.teardownPending = true;
#if CONTINUITY_DEBUG_LOG
                    log::info("[Continuity] leave-detected level={} source={} checkpoints={} records={} entrySnapshot={} exitSnapshot={}",
                              levelID(layer), source,
                              layer->m_checkpointArray ? layer->m_checkpointArray->count() : 0,
                              it->second.records.size(),
                              it->second.entrySnapshot.has_value(),
                              it->second.exitSnapshot.has_value());

                    if (logLiveState && layer->m_player1)
                    {
                        log::info("[Continuity] leave-state level={} pos=({}, {}) mode={} gravity={} yVelocity={} dual={} player2={}",
                                  levelID(layer),
                                  layer->m_player1->m_positionX,
                                  layer->m_player1->m_positionY,
                                  modeID(layer->m_player1),
                                  layer->m_player1->m_gravity,
                                  layer->m_player1->m_yVelocity,
                                  layer->m_gameState.m_isDualMode,
                                  layer->m_player2 != nullptr);
                    }
#endif
                }
            }
        }

        static bool validateRestoration(PlayLayer *layer, PlayerSnapshot const &snap,
                                        int expectedCheckpointCount, char const *context)
        {
            if (!layer || !layer->m_player1 || !snap.m_isValid)
                return false;

            auto *p1 = layer->m_player1;
            auto checkpointCount = layer->m_checkpointArray ? static_cast<int>(layer->m_checkpointArray->count()) : 0;

            bool mismatch =
                modeID(p1) != snap.m_modeID ||
                p1->m_positionX != static_cast<double>(snap.m_playerState.position.x) ||
                p1->m_positionY != static_cast<double>(snap.m_playerState.position.y) ||
                p1->m_gravity != snap.m_gravity ||
                p1->m_yVelocity != snap.m_yVelocity ||
                layer->m_gameState.m_isDualMode != snap.m_isDualMode ||
                (expectedCheckpointCount >= 0 && checkpointCount != expectedCheckpointCount) ||
                (snap.m_hasPlayer2 && (!layer->m_player2 ||
                                       modeID(layer->m_player2) != snap.m_player2ModeID ||
                                       layer->m_player2->m_positionX != static_cast<double>(snap.m_player2State.position.x) ||
                                       layer->m_player2->m_positionY != static_cast<double>(snap.m_player2State.position.y) ||
                                       layer->m_player2->m_gravity != snap.m_player2Gravity ||
                                       layer->m_player2->m_yVelocity != snap.m_player2YVelocity));

            if (mismatch)
                log::warn("[Continuity] restore mismatch context={} level={}", context, levelID(layer));

            return mismatch;
        }

        static void onCheckpoint(PlayLayer *layer, CheckpointObject *cp)
        {
            if (!layer || !cp || !layer->m_player1 || !layer->m_level)
                return;

            int id = levelID(layer);
            auto it = s_levelStates.find(id);
            if (it != s_levelStates.end() && !signatureMatches(layer, it->second))
                clearLevel(id);

            auto &state = s_levelStates[id];
            state.signature = signature(layer);

            auto snapshot = capture(layer);
            captureCheckpointLevelState(snapshot, cp);
            if (!snapshot.m_boundsValid)
            {
                auto const *previous = state.records.empty() ? nullptr : &state.records.back().snapshot;
                inheritBounds(snapshot, previous);
            }
            state.exitSnapshot.reset();
            state.latestCheckpoint = snapshot;
            state.records.push_back(CheckpointRecord{cp, snapshot});
            syncRecordsToLiveCheckpoints(layer, state);
#if CONTINUITY_DEBUG_LOG
            logSnapshot("checkpoint-captured", layer, snapshot);
#endif
        }

        static void onRemoveCheckpoint(PlayLayer *layer, CheckpointObject *cp, bool first)
        {
            if (!cp || s_restoringCheckpoints)
                return;

            int id = levelID(layer);
            auto it = s_levelStates.find(id);
            if (it == s_levelStates.end())
                return;
            if (!signatureMatches(layer, it->second))
            {
                clearLevel(id);
                return;
            }

            if (it->second.teardownPending)
                return;

            auto &records = it->second.records;
            if (records.empty())
                return;

            auto index = first ? size_t{0} : records.size() - 1;
            if (index < records.size())
                records.erase(records.begin() + index);

            if (layer->m_checkpointArray)
            {
                auto liveCount = static_cast<size_t>(layer->m_checkpointArray->count());
                if (records.size() > liveCount)
                    records.resize(liveCount);
            }

            syncRecordsToLiveCheckpoints(layer, it->second);

            it->second.latestCheckpoint = records.empty()
                                              ? std::nullopt
                                              : std::optional<PlayerSnapshot>(records.back().snapshot);
        }

        static void discardCheckpoint(PlayLayer *layer, CheckpointObject *checkpoint, bool first)
        {
            if (!layer || !checkpoint || !layer->m_checkpointArray)
                return;

            if (!layer->m_checkpointArray->containsObject(checkpoint))
                return;

            layer->PlayLayer::removeCheckpoint(first);
            onRemoveCheckpoint(layer, checkpoint, first);
        }

        static bool beginPlay(PlayLayer *layer)
        {
            int id = levelID(layer);
            auto it = s_levelStates.find(id);
            bool rejoining = it != s_levelStates.end() && it->second.teardownPending;
            if (rejoining)
            {
#if CONTINUITY_DEBUG_LOG
                auto snapshot = capture(layer);
                log::info("[Continuity] rejoin-detected level={} checkpoints={} records={} entrySnapshot={} exitSnapshot={} icon={} mode={} player2Icon={} boundsValid={} groundHeight={} portalMinY={} portalMaxY={}",
                          id,
                          layer->m_checkpointArray ? layer->m_checkpointArray->count() : 0,
                          it->second.records.size(),
                          it->second.entrySnapshot.has_value(),
                          it->second.exitSnapshot.has_value(),
                          playerIconID(snapshot.m_modeID),
                          snapshot.m_modeID,
                          snapshot.m_hasPlayer2 ? playerIconID(snapshot.m_player2ModeID) : -1,
                          snapshot.m_boundsValid,
                          snapshot.m_groundHeight,
                          snapshot.m_portalMinY,
                          snapshot.m_portalMaxY);
#endif
                it->second.teardownPending = false;
            }
            if (rejoining && it != s_levelStates.end() && it->second.latestCheckpoint &&
                !it->second.records.empty())
                it->second.records.back().snapshot = *it->second.latestCheckpoint;
            if (it != s_levelStates.end() && !signatureMatches(layer, it->second))
                clearLevel(id);
            auto snapshot = firstValid({});
            if (auto cp = getLatestCheckpointSnapshot(id))
                snapshot = cp;
            else if (auto exit = s_levelStates.find(id); exit != s_levelStates.end() && exit->second.exitSnapshot)
                snapshot = exit->second.exitSnapshot->m_isValid ? exit->second.exitSnapshot : std::nullopt;

            s_levelStates[id].entrySnapshot = snapshot;
            s_levelStates[id].teardownPending = false;
            s_levelStates[id].quitConfirmed = false;
#if CONTINUITY_DEBUG_LOG
            if (snapshot)
                logSnapshot("entry-selected", layer, *snapshot);
            else
                log::info("[Continuity] entry-selected level={} snapshot=none", id);
#endif
            return snapshot.has_value();
        }

        static void endPractice(PlayLayer *layer)
        {
            clearLevel(levelID(layer));
        }

        static void onQuit(PlayLayer *layer)
        {
            if (!layer)
                return;

            int id = levelID(layer);
            bool completed = layer->m_hasCompletedLevel;

            if (auto it = s_levelStates.find(id); it != s_levelStates.end())
            {
                it->second.teardownPending = true;
                it->second.quitConfirmed = true;
            }

            if (!completed)
            {
                auto it = s_levelStates.find(id);
                if (it != s_levelStates.end() && !signatureMatches(layer, it->second))
                    clearLevel(id);

                auto &state = s_levelStates[id];
                state.signature = signature(layer);
                if (state.records.empty() && layer->m_player1)
                {
                    auto snapshot = capture(layer);
                    inheritBounds(snapshot, state.exitSnapshot ? &*state.exitSnapshot : nullptr);
                    state.exitSnapshot = snapshot;
                }
                else if (state.records.empty())
                {
                    state.exitSnapshot.reset();
                }
            }

            if (completed)
                clearLevel(id);
#if CONTINUITY_DEBUG_LOG
            log::info("[Continuity] quit level={} completed={} checkpoints={}", id, completed,
                      layer->m_checkpointArray ? layer->m_checkpointArray->count() : 0);
#endif
        }
        static std::optional<PlayerSnapshot> resetPracticeRun(PlayLayer *layer)
        {
            int id = levelID(layer);
            if (auto s = getLatestCheckpointSnapshot(id))
                return s;

            auto it = s_levelStates.find(id);
            if (it == s_levelStates.end())
                return std::nullopt;
            if (it->second.entrySnapshot && it->second.entrySnapshot->m_isValid)
                return it->second.entrySnapshot;
            if (it->second.exitSnapshot && it->second.exitSnapshot->m_isValid)
                return it->second.exitSnapshot;
            return std::nullopt;
        }

        static std::optional<PlayerSnapshot> latestEntry(int levelID)
        {
            auto it = s_levelStates.find(levelID);
            if (it == s_levelStates.end() || !it->second.entrySnapshot || !it->second.entrySnapshot->m_isValid)
                return std::nullopt;
            return it->second.entrySnapshot;
        }

        static std::optional<PlayerSnapshot> latestCheckpoint(int levelID)
        {
            return getLatestCheckpointSnapshot(levelID);
        }

        static std::optional<PlayerSnapshot> snapshotForCheckpoint(PlayLayer *layer, CheckpointObject *checkpoint)
        {
            if (!layer || !checkpoint)
                return std::nullopt;

            auto it = s_levelStates.find(levelID(layer));
            if (it == s_levelStates.end() || !signatureMatches(layer, it->second))
                return std::nullopt;

            for (auto const &record : it->second.records)
            {
                if (record.checkpoint == checkpoint && record.snapshot.m_isValid)
                    return record.snapshot;
            }
            return std::nullopt;
        }

        static bool restoreCheckpoints(PlayLayer *layer)
        {
            if (!layer || !layer->m_checkpointArray)
                return false;

            auto it = s_levelStates.find(levelID(layer));
            if (it == s_levelStates.end() || it->second.records.empty())
                return false;

            auto &state = it->second;
            auto snapshots = std::vector<PlayerSnapshot>();
            snapshots.reserve(state.records.size());
            for (auto const &record : state.records)
                snapshots.push_back(record.snapshot);

            if (state.latestCheckpoint && state.latestCheckpoint->m_isValid)
            {
                if (snapshots.empty())
                    snapshots.push_back(*state.latestCheckpoint);
                else
                    snapshots.back() = *state.latestCheckpoint;
            }

            std::vector<CheckpointRecord> restored;
            restored.reserve(snapshots.size());
            s_restoringCheckpoints = true;
            layer->PlayLayer::removeAllCheckpoints();

            for (auto const &snapshot : snapshots)
            {
                auto *checkpoint = layer->PlayLayer::createCheckpoint();
                if (!checkpoint)
                {
                    s_restoringCheckpoints = false;
                    return false;
                }

                fillCheckpoint(checkpoint->m_player1Checkpoint, snapshot.m_playerState,
                               snapshot.m_gravity, snapshot.m_gravityMod, snapshot.m_yVelocity);
                if (snapshot.m_hasPlayer2)
                    fillCheckpoint(checkpoint->m_player2Checkpoint, snapshot.m_player2State,
                                   snapshot.m_player2Gravity, snapshot.m_player2GravityMod,
                                   snapshot.m_player2YVelocity);
                fillCheckpointGameState(checkpoint, snapshot);

                layer->PlayLayer::storeCheckpoint(checkpoint);
                restored.push_back(CheckpointRecord{checkpoint, snapshot});
            }

            if (!restored.empty())
                layer->PlayLayer::loadFromCheckpoint(restored.back().checkpoint);

            if (!restored.empty())
            {
            }

            state.records = std::move(restored);
            syncRecordsToLiveCheckpoints(layer, state);
            s_restoringCheckpoints = false;
            return !state.records.empty() || state.latestCheckpoint.has_value();
        }

        static CheckpointObject *prepareResetCheckpoint(PlayLayer *layer)
        {
            if (!layer || !layer->m_checkpointArray || layer->m_checkpointArray->count() != 0)
                return nullptr;

            auto snapshot = getLatestCheckpointSnapshot(levelID(layer));
            if (!snapshot)
                return nullptr;

            auto *checkpoint = layer->PlayLayer::createCheckpoint();
            if (!checkpoint)
                return nullptr;

            fillCheckpoint(checkpoint->m_player1Checkpoint, snapshot->m_playerState,
                           snapshot->m_gravity, snapshot->m_gravityMod, snapshot->m_yVelocity);
            if (snapshot->m_hasPlayer2)
                fillCheckpoint(checkpoint->m_player2Checkpoint, snapshot->m_player2State,
                               snapshot->m_player2Gravity, snapshot->m_player2GravityMod,
                               snapshot->m_player2YVelocity);
            fillCheckpointGameState(checkpoint, *snapshot);

            layer->PlayLayer::storeCheckpoint(checkpoint);
            return checkpoint;
        }

        static bool applySnapshot(PlayLayer *layer, PlayerSnapshot const &snap, RestoreMode mode)
        {
            if (!validate(layer, snap, "applySnapshot"))
                return false;

            auto restoreSafePlayer = [](PlayerObject *player, PlayerState const &state,
                                        double gravity, float gravityMod, double yVelocity)
            {
                if (!player)
                    return;

                player->setPosition(state.position);
                player->m_position = state.position;
                player->m_positionX = state.position.x;
                player->m_positionY = state.position.y;
                player->m_gravity = sanitizeGravity(gravity);
                player->m_gravityMod = gravityMod;
                player->setYVelocity(sanitizeYVelocity(yVelocity), 0);
            };

            restoreSafePlayer(layer->m_player1, snap.m_playerState,
                              snap.m_gravity, snap.m_gravityMod, snap.m_yVelocity);
            if (snap.m_hasPlayer2)
                restoreSafePlayer(layer->m_player2, snap.m_player2State,
                                  snap.m_player2Gravity, snap.m_player2GravityMod,
                                  snap.m_player2YVelocity);

            (void)mode;
            return true;

            if (!validate(layer, snap, "applySnapshot"))
                return false;

            auto *baseLayer = static_cast<GJBaseGameLayer *>(layer);
            applyInteractionState(layer, snap);
            if (mode == RestoreMode::PhysicsOnly)
            {
                applyPersistentPhysics(layer, snap);
                return true;
            }

            bool modeChanged = modeID(layer->m_player1) != snap.m_modeID;
            bool player2ModeChanged = snap.m_hasPlayer2 && (!layer->m_player2 || modeID(layer->m_player2) != snap.m_player2ModeID);
            bool boundsChanged = layer->m_gameState.m_isDualMode != snap.m_isDualMode ||
                                 layer->m_gameState.m_dualRelated != snap.m_dualRelated ||
                                 layer->m_gameState.m_unkBool10 != snap.m_isMirrored;
            bool authoritative = mode == RestoreMode::Full;
            bool resetTransient = authoritative || modeChanged || player2ModeChanged || boundsChanged;

            if (snap.m_hasPlayer2 && !layer->m_player2)
                return false;
            if (authoritative)
                removeStalePlayer2(layer, snap);
            if (resetTransient)
            {
                clearTransientGroundState(layer->m_player1);
                if (snap.m_hasPlayer2)
                    clearTransientGroundState(layer->m_player2);
            }

            restorePlayerCore(baseLayer, layer->m_player1, snap.m_playerState, snap.m_modeID,
                              snap.m_gravity, snap.m_gravityMod, snap.m_yVelocity,
                              modeChanged);
            if (snap.m_hasPlayer2)
                restorePlayerCore(baseLayer, layer->m_player2, snap.m_player2State, snap.m_player2ModeID,
                                  snap.m_player2Gravity, snap.m_player2GravityMod, snap.m_player2YVelocity,
                                  player2ModeChanged);

            baseLayer->m_gameState.m_unkBool10 = snap.m_isMirrored;
            if (authoritative)
            {
                finalizeApply(layer, snap, false);
                baseLayer->updateMaxGameplayY();
                applyPersistentPhysics(layer, snap);
                restoreGroundState(layer->m_player1, snap, false);
                if (snap.m_hasPlayer2)
                    restoreGroundState(layer->m_player2, snap, true);
                suppressAutoCheckpointPlacementInternal(layer->m_player1);
                suppressAutoCheckpointPlacementInternal(layer->m_player2);
#if CONTINUITY_DEBUG_LOG
                armAuthoritativeProbe(layer->m_player1, snap);
#endif
            }
            else if (resetTransient)
            {
                baseLayer->updateDualGround(layer->m_player1,
                                            snap.m_isDualMode ? snap.m_dualRelated : groundModeID(layer->m_player1, snap.m_modeID),
                                            true, 0.0f);
                if (portalAnimationActive(baseLayer))
                    layer->m_gameState.m_portalY = snap.m_portalY;
                finalizeApply(layer, snap, false);
                restoreGroundState(layer->m_player1, snap, false);
                if (snap.m_hasPlayer2)
                    restoreGroundState(layer->m_player2, snap, true);
            }
#if CONTINUITY_DEBUG_LOG
            logSnapshot("snapshot-applied", layer, snap);
#endif
            return true;
        }

        static bool applyAuthoritativeSnapshot(PlayLayer *layer, PlayerSnapshot const &snap)
        {
            return applySnapshot(layer, snap, RestoreMode::Full);
        }
    };
}

class $modify(PMLPlayLayer, PlayLayer)
{
public:
    static constexpr float kEntryRestoreDuration = 2.0f;
    static constexpr int kRestoreSettleFrames = 2;
    struct Fields
    {
        bool m_isRestoredPracticeRun = false;
        bool m_restoreCheckpointsPending = false;
        bool m_quitHandled = false;
        int m_restoreSettleFrames = 0;
        std::optional<PlayerSnapshot> m_restoreSettleSnapshot;
        std::optional<PlayerSnapshot> m_pendingEntrySnapshot;
        float m_entryRestoreTime = 0.0f;
    };

    void armRestoreSettle(bool mismatch = false)
    {
        auto frames = kRestoreSettleFrames + (mismatch ? 1 : 0);
        m_fields->m_restoreSettleFrames = std::max(m_fields->m_restoreSettleFrames, frames);
    }

    void startGame()
    {
        PlayLayer::startGame();
        continuity::clearCollisionSuppression();
#if CONTINUITY_DEBUG_LOG
        continuity::clearAuthoritativeProbe();
#endif
        m_fields->m_quitHandled = false;
        m_fields->m_restoreCheckpointsPending = false;
        m_fields->m_restoreSettleFrames = 0;
        m_fields->m_restoreSettleSnapshot.reset();
        m_fields->m_pendingEntrySnapshot.reset();

        if (!continuity::enabled())
            return;

        if (!this->m_level || !this->m_checkpointArray)
            return;
        if (!continuity::StateManager::beginPlay(this))
            return;

        m_fields->m_isRestoredPracticeRun = true;
        this->togglePracticeMode(true);
        m_fields->m_restoreCheckpointsPending = true;

        int id = continuity::StateManager::levelID(this);
        auto snap = continuity::StateManager::latestEntry(id);
        if (!snap)
            return;
        m_fields->m_pendingEntrySnapshot = *snap;
        m_fields->m_entryRestoreTime = kEntryRestoreDuration;
    }

    void restorePendingCheckpoints()
    {
        if (!m_fields->m_restoreCheckpointsPending || !this->m_level || !this->m_player1)
            return;

        m_fields->m_restoreCheckpointsPending = false;
        continuity::StateManager::restoreCheckpoints(this);

        auto id = continuity::StateManager::levelID(this);
        if (auto snap = continuity::StateManager::latestCheckpoint(id))
        {
            continuity::StateManager::applySnapshot(this, *snap, continuity::StateManager::RestoreMode::Full);
            m_fields->m_restoreSettleSnapshot = *snap;
            bool mismatch = continuity::StateManager::validateRestoration(
                this, *snap, this->m_checkpointArray ? this->m_checkpointArray->count() : 0, "checkpoint-restore");
            armRestoreSettle(mismatch);
        }
    }

    void storeCheckpoint(CheckpointObject *cp)
    {
        if (!continuity::enabled())
        {
            PlayLayer::storeCheckpoint(cp);
            return;
        }

        if (!cp || m_fields->m_restoreSettleFrames > 0)
            return;

        PlayLayer::storeCheckpoint(cp);

        if (!this->m_player1 || !this->m_level || m_fields->m_restoreCheckpointsPending ||
            m_fields->m_entryRestoreTime > 0.0f)
            return;

        m_fields->m_isRestoredPracticeRun = false;
        continuity::StateManager::onCheckpoint(this, cp);
    }

    void loadFromCheckpoint(CheckpointObject *checkpoint)
    {
        PlayLayer::loadFromCheckpoint(checkpoint);

        if (!continuity::enabled())
            return;

        if (auto snapshot = continuity::StateManager::snapshotForCheckpoint(this, checkpoint))
        {
            if (continuity::StateManager::applyAuthoritativeSnapshot(this, *snapshot))
            {
                m_fields->m_restoreSettleSnapshot = *snapshot;
                armRestoreSettle();
            }
        }
    }

    void removeCheckpoint(bool p0)
    {
        if (!continuity::enabled())
        {
            PlayLayer::removeCheckpoint(p0);
            return;
        }

        if (!this->m_checkpointArray || this->m_checkpointArray->count() == 0)
        {
            PlayLayer::removeCheckpoint(p0);
            return;
        }

        auto *targetObj = p0 ? this->m_checkpointArray->objectAtIndex(0) : this->m_checkpointArray->lastObject();
        auto *targetCP = targetObj ? static_cast<CheckpointObject *>(targetObj) : nullptr;

        if (!continuity::StateManager::canRemoveCheckpoint(targetCP))
        {
            continuity::StateManager::discardCheckpoint(this, targetCP, p0);
            return;
        }

        PlayLayer::removeCheckpoint(p0);

        if (targetCP)
            continuity::StateManager::onRemoveCheckpoint(this, targetCP, p0);
    }

    void togglePracticeMode(bool toggle)
    {
        if (!continuity::enabled())
        {
            PlayLayer::togglePracticeMode(toggle);
            return;
        }

        if (!toggle && this->m_level && !m_fields->m_quitHandled)
        {
            continuity::StateManager::endPractice(this);
            m_fields->m_isRestoredPracticeRun = false;
            m_fields->m_restoreSettleFrames = 0;
            m_fields->m_restoreSettleSnapshot.reset();
        }

        PlayLayer::togglePracticeMode(toggle);
    }

    void resetLevel()
    {
        PlayLayer::resetLevel();

        if (!continuity::enabled())
            return;

        if (!this->m_level || !this->m_player1 || !m_fields->m_isRestoredPracticeRun)
            return;

        auto snap = continuity::StateManager::resetPracticeRun(this);
        if (!snap)
        {
            m_fields->m_isRestoredPracticeRun = false;
            return;
        }

        bool hasVanillaCheckpoint = this->m_checkpointArray && this->m_checkpointArray->count() > 0;
        if (hasVanillaCheckpoint)
        {
            continuity::StateManager::applySnapshot(this, *snap, continuity::StateManager::RestoreMode::Full);
            m_fields->m_restoreSettleSnapshot.reset();
        }
        else
        {
            continuity::StateManager::applySnapshot(this, *snap, continuity::StateManager::RestoreMode::Full);
            m_fields->m_restoreSettleSnapshot = *snap;
        }
        bool mismatch = continuity::StateManager::validateRestoration(
            this, *snap, this->m_checkpointArray ? this->m_checkpointArray->count() : 0, "practice-reset");
        armRestoreSettle(mismatch);
    }

    void postUpdate(float dt)
    {
        if (!continuity::enabled())
        {
            PlayLayer::postUpdate(dt);
            return;
        }

#if CONTINUITY_DEBUG_LOG
        continuity::logAuthoritativeProbe(this);
#endif

        if (m_fields->m_restoreSettleFrames > 0)
            continuity::StateManager::suppressAutoCheckpointPlacement(this);

        if (this->m_player1 && (!std::isfinite(this->m_player1->m_gravity) ||
                                std::abs(this->m_player1->m_gravity) > 10.0 ||
                                std::abs(this->m_player1->m_gravity) < 1e-6))
        {
            log::warn("[Continuity] invalid gravity corrected level={} gravity={}",
                      continuity::StateManager::levelID(this), this->m_player1->m_gravity);
            this->m_player1->m_gravity = 0.0;
        }

        PlayLayer::postUpdate(dt);

        if (m_fields->m_pendingEntrySnapshot)
        {
            auto snap = *m_fields->m_pendingEntrySnapshot;
            m_fields->m_pendingEntrySnapshot.reset();
            if (!continuity::StateManager::applyAuthoritativeSnapshot(this, snap))
            {
                continuity::StateManager::abandon(this);
                m_fields->m_isRestoredPracticeRun = false;
                m_fields->m_restoreCheckpointsPending = false;
                return;
            }

            bool mismatch = continuity::StateManager::validateRestoration(this, snap, -1, "entry");
            armRestoreSettle(mismatch);
            m_fields->m_restoreSettleSnapshot = snap;
        }

        if (m_fields->m_restoreSettleFrames > 0 && m_fields->m_restoreSettleSnapshot)
        {
            bool reasserted = continuity::StateManager::applySnapshot(
                this, *m_fields->m_restoreSettleSnapshot, continuity::StateManager::RestoreMode::TransformOnly);
            if (this->m_player1)
                log::info("[Continuity] DEBUG settle reasserted={} frames={} actualPos=({}, {}) actualMode={} actualGravity={}",
                          reasserted, m_fields->m_restoreSettleFrames,
                          this->m_player1->m_positionX, this->m_player1->m_positionY,
                          continuity::StateManager::currentModeID(this->m_player1), this->m_player1->m_gravity);
            continuity::StateManager::suppressAutoCheckpointPlacement(this);
        }

        if (m_fields->m_restoreCheckpointsPending && this->m_level && this->m_player1)
        {
            m_fields->m_restoreCheckpointsPending = false;
            continuity::StateManager::restoreCheckpoints(this);

            auto id = continuity::StateManager::levelID(this);
            if (auto snap = continuity::StateManager::latestCheckpoint(id))
            {
                if (continuity::StateManager::applySnapshot(
                        this, *snap, continuity::StateManager::RestoreMode::Full))
                    continuity::StateManager::applySnapshot(
                        this, *snap, continuity::StateManager::RestoreMode::TransformOnly);

                armRestoreSettle();
                m_fields->m_restoreSettleSnapshot = *snap;
                bool mismatch = continuity::StateManager::validateRestoration(
                    this, *snap, this->m_checkpointArray ? this->m_checkpointArray->count() : 0, "checkpoint-restore");
                if (mismatch)
                    armRestoreSettle(true);
            }
        }

        if (m_fields->m_restoreSettleFrames > 0)
            --m_fields->m_restoreSettleFrames;
        else
            m_fields->m_restoreSettleSnapshot.reset();

        continuity::advanceCollisionSuppression(dt);
        m_fields->m_entryRestoreTime = std::max(0.0f, m_fields->m_entryRestoreTime - std::max(0.0f, dt));
    }

    void onQuit()
    {
        if (!continuity::enabled())
        {
            PlayLayer::onQuit();
            return;
        }

        m_fields->m_quitHandled = true;
        continuity::clearCollisionSuppression();
#if CONTINUITY_DEBUG_LOG
        continuity::clearAuthoritativeProbe();
#endif
        continuity::StateManager::onQuit(this);
        PlayLayer::onQuit();
    }

    void onExit()
    {
        if (!continuity::enabled())
        {
            PlayLayer::onExit();
            return;
        }

        continuity::clearCollisionSuppression();
#if CONTINUITY_DEBUG_LOG
        continuity::clearAuthoritativeProbe();
#endif
        if (!m_fields->m_quitHandled)
            continuity::StateManager::markTeardownPending(this, "onExit", true);
        PlayLayer::onExit();
    }

    ~PMLPlayLayer()
    {
        if (!continuity::enabled())
            return;

        if (!m_fields->m_quitHandled)
            continuity::StateManager::markTeardownPending(this, "destructor");
    }
};

class $modify(ContinuitySafePlayerObject, PlayerObject)
{
    bool collidedWithObjectInternal(float dt, GameObject *object, cocos2d::CCRect rect, bool skipCheck)
    {
        if (!continuity::enabled())
            return PlayerObject::collidedWithObjectInternal(dt, object, rect, skipCheck);

        if (!this || !object || !this->m_collisionLogTop || !this->m_collisionLogBottom ||
            !this->m_collisionLogLeft || !this->m_collisionLogRight ||
            continuity::collisionDispatchSuppressed(this))
            return false;

        if (!this->m_maybeLastGroundObject)
            this->m_maybeLastGroundObject = object;

        return PlayerObject::collidedWithObjectInternal(dt, object, rect, skipCheck);
    }
};

class $modify(ContinuitySafeGameLayer, GJBaseGameLayer)
{
    int checkCollisions(PlayerObject *player, float dt, bool ignoreDamage)
    {
        if (!continuity::enabled())
            return GJBaseGameLayer::checkCollisions(player, dt, ignoreDamage);

        if (continuity::collisionDispatchSuppressed(player))
            return 0;

        return GJBaseGameLayer::checkCollisions(player, dt, ignoreDamage);
    }
};