#include "Checkpoint-Continuity.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/CheckpointObject.hpp>
#include <Geode/binding/EnhancedGameObject.hpp>
#include <Geode/binding/FMODAudioEngine.hpp>
#include <Geode/binding/GJGameLevel.hpp>
#include <Geode/binding/GJGameState.hpp>
#include <Geode/binding/PlayLayer.hpp>
#include <Geode/binding/PlayerCheckpoint.hpp>
#include <Geode/modify/PlayLayer.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <functional>
#include <string>
#include <unordered_map>

using namespace geode::prelude;

namespace continuity
{
    bool enabled()
    {
        return Mod::get()->getSettingValue<bool>("checkpoint-continuity-enabled");
    }

    namespace
    {
        constexpr std::size_t kObjectBase = sizeof(cocos2d::CCNode);

        std::unordered_map<std::int64_t, LevelRecord> s_levels;

        template <class T>
        std::vector<std::uint8_t> toBlob(T const *object)
        {
            std::vector<std::uint8_t> blob;
            if (!object)
                return blob;

            auto const *bytes = reinterpret_cast<std::uint8_t const *>(object);
            blob.assign(bytes + kObjectBase, bytes + sizeof(T));
            return blob;
        }

        void fromBlob(PlayerCheckpoint *object, std::vector<std::uint8_t> const &blob)
        {
            if (!object || blob.size() != sizeof(PlayerCheckpoint) - kObjectBase)
                return;

            auto *bytes = reinterpret_cast<std::uint8_t *>(object);
            struct Range
            {
                std::size_t start;
                std::size_t end;
            };
            std::vector<Range> excluded;
            auto exclude = [&](auto member) {
                auto const *field = reinterpret_cast<std::uint8_t const *>(&(object->*member));
                auto const start = static_cast<std::size_t>(field - bytes) - kObjectBase;
                excluded.push_back({start, start + sizeof(object->*member)});
            };

            exclude(&PlayerCheckpoint::m_dashRing);
            exclude(&PlayerCheckpoint::m_objectSnappedTo);
            exclude(&PlayerCheckpoint::m_playerFollowFloats);
            exclude(&PlayerCheckpoint::m_currentSlope2);
            exclude(&PlayerCheckpoint::m_preLastGroundObject);
            exclude(&PlayerCheckpoint::m_collidedObject);
            exclude(&PlayerCheckpoint::m_lastGroundObject);
            exclude(&PlayerCheckpoint::m_collidingWithLeft);
            exclude(&PlayerCheckpoint::m_collidingWithRight);
            exclude(&PlayerCheckpoint::m_currentPotentialSlope);
            exclude(&PlayerCheckpoint::m_currentSlope);
            exclude(&PlayerCheckpoint::m_ringRelatedSet);
            exclude(&PlayerCheckpoint::m_touchingRings);
            exclude(&PlayerCheckpoint::m_touchedRings);
            exclude(&PlayerCheckpoint::m_lastActivatedPortal);
            exclude(&PlayerCheckpoint::m_jumpPadRelated);

            std::sort(excluded.begin(), excluded.end(), [](Range const &left, Range const &right) {
                return left.start < right.start;
            });

            std::size_t copiedUntil = 0;
            for (auto const &range : excluded)
            {
                if (range.start > copiedUntil)
                    std::memcpy(bytes + kObjectBase + copiedUntil, blob.data() + copiedUntil, range.start - copiedUntil);
                copiedUntil = std::max(copiedUntil, range.end);
            }
            if (copiedUntil < blob.size())
                std::memcpy(bytes + kObjectBase + copiedUntil, blob.data() + copiedUntil, blob.size() - copiedUntil);
        }

        std::int64_t levelKey(PlayLayer *layer)
        {
            auto *level = layer ? layer->m_level : nullptr;
            if (!level)
                return 0;

            auto id = static_cast<std::int64_t>(level->m_levelID.value());
            if (id != 0)
                return id;

            std::string name = level->m_levelName;
            auto hash = static_cast<std::int64_t>(std::hash<std::string>{}(name) & 0x3FFFFFFFFFFFFFFFull);
            return hash | (std::int64_t{1} << 62);
        }

        LevelSignature signatureOf(PlayLayer *layer)
        {
            auto *level = layer ? layer->m_level : nullptr;
            if (!level)
                return {};

            return LevelSignature{
                level->m_levelVersion,
                level->m_timestamp,
                level->m_levelLength,
                level->m_levelString.size(),
            };
        }

        std::vector<ActivationState> captureActivations(PlayLayer *layer)
        {
            std::vector<ActivationState> result;
            if (!layer)
            {
                log::info("[Continuity] cannot capture activations: PlayLayer is null");
                return result;
            }

            gd::vector<SavedActiveObjectState> active;
            gd::vector<SavedSpecialObjectState> special;
            layer->saveActiveSaveObjects(active, special);
            result.reserve(active.size());

            for (auto const &saved : active)
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
            log::info("[Continuity] captured {} active object states", result.size());
            return result;
        }
    }

    GameStateSlice GameStateSlice::capture(GJGameState const &state)
    {
        GameStateSlice slice;
        slice.isDualMode = state.m_isDualMode;
        slice.middleGroundOffsetY = state.m_middleGroundOffsetY;
        slice.dualRelated = state.m_dualRelated;
        slice.mirrored = state.m_unkBool10;
        slice.cameraZoom = state.m_cameraZoom;
        slice.targetCameraZoom = state.m_targetCameraZoom;
        slice.cameraOffset = state.m_cameraOffset;
        slice.cameraPosition = state.m_cameraPosition;
        slice.cameraPosition2 = state.m_cameraPosition2;
        slice.cameraStepDiff = state.m_cameraStepDiff;
        slice.cameraAngle = state.m_cameraAngle;
        slice.targetCameraAngle = state.m_targetCameraAngle;
        slice.timeWarp = state.m_timeWarp;
        slice.queuedTimeWarp = state.m_queuedTimeWarp;
        slice.timeWarpRelated = state.m_timeWarpRelated;
        slice.timeModRelated = state.m_timeModRelated;
        slice.timeModRelated2 = state.m_timeModRelated2;
        slice.totalTime = state.m_totalTime;
        slice.levelTime = state.m_levelTime;
        slice.currentProgress = state.m_currentProgress;
        slice.currentChannel = state.m_currentChannel;
        slice.rotateChannel = state.m_rotateChannel;
        slice.levelFlipping = state.m_levelFlipping;
        return slice;
    }

    void GameStateSlice::apply(GJGameState &state) const
    {
        state.m_isDualMode = isDualMode;
        state.m_middleGroundOffsetY = middleGroundOffsetY;
        state.m_dualRelated = dualRelated;
        state.m_unkBool10 = mirrored;
        state.m_cameraZoom = cameraZoom;
        state.m_targetCameraZoom = targetCameraZoom;
        state.m_cameraOffset = cameraOffset;
        state.m_cameraPosition = cameraPosition;
        state.m_cameraPosition2 = cameraPosition2;
        state.m_cameraStepDiff = cameraStepDiff;
        state.m_cameraAngle = cameraAngle;
        state.m_targetCameraAngle = targetCameraAngle;
        state.m_timeWarp = timeWarp;
        state.m_queuedTimeWarp = queuedTimeWarp;
        state.m_timeWarpRelated = timeWarpRelated;
        state.m_timeModRelated = timeModRelated;
        state.m_timeModRelated2 = timeModRelated2;
        state.m_totalTime = totalTime;
        state.m_levelTime = levelTime;
        state.m_currentProgress = currentProgress;
        state.m_currentChannel = currentChannel;
        state.m_rotateChannel = rotateChannel;
        state.m_levelFlipping = levelFlipping;
    }

    SavedCheckpoint SavedCheckpoint::capture(PlayLayer *layer, CheckpointObject *checkpoint)
    {
        SavedCheckpoint saved;
        if (!layer || !checkpoint)
        {
            log::info("[Continuity] cannot capture checkpoint: layer={} checkpoint={}",
                      static_cast<void *>(layer), static_cast<void *>(checkpoint));
            return saved;
        }

        saved.player1 = toBlob(checkpoint->m_player1Checkpoint);
        saved.game = GameStateSlice::capture(layer->m_gameState);
        if (saved.game.isDualMode)
            saved.player2 = toBlob(checkpoint->m_player2Checkpoint);
        saved.commandIndex = checkpoint->m_commandIndex;
        if (auto *fmod = FMODAudioEngine::sharedEngine())
            saved.musicTimeMS = fmod->getMusicTimeMS(0);
        saved.activations = captureActivations(layer);
        log::info("[Continuity] captured checkpoint={} player1Bytes={} player2Bytes={} commandIndex={} musicTimeMS={} activations={} dual={}",
                  static_cast<void *>(checkpoint), saved.player1.size(), saved.player2.size(),
                  saved.commandIndex, saved.musicTimeMS, saved.activations.size(), saved.game.isDualMode);
        return saved;
    }

    void SavedCheckpoint::applyTo(CheckpointObject *checkpoint) const
    {
        if (!checkpoint)
        {
            log::info("[Continuity] cannot apply checkpoint state: checkpoint is null");
            return;
        }

        fromBlob(checkpoint->m_player1Checkpoint, player1);
        fromBlob(checkpoint->m_player2Checkpoint, player2);
        game.apply(checkpoint->m_gameState);
        checkpoint->m_commandIndex = commandIndex;

        auto *physical = checkpoint->m_physicalCheckpointObject;
        if (physical && checkpoint->m_player1Checkpoint)
            physical->setPosition(checkpoint->m_player1Checkpoint->m_position);
        log::info("[Continuity] applied checkpoint={} player1Bytes={} player2Bytes={} commandIndex={} physicalObject={}",
                  static_cast<void *>(checkpoint), player1.size(), player2.size(), commandIndex,
                  static_cast<void *>(physical));
    }

    void SavedCheckpoint::applyActivations(PlayLayer *layer) const
    {
        if (activations.empty() || !layer || !layer->m_objects)
        {
            log::info("[Continuity] skipped activation restore: saved={} layer={} objects={}",
                      activations.size(), static_cast<void *>(layer),
                      static_cast<void *>(layer ? layer->m_objects : nullptr));
            return;
        }

        std::unordered_map<int, ActivationState const *> byID;
        byID.reserve(activations.size());
        for (auto const &state : activations)
            byID.emplace(state.uniqueID, &state);

        std::size_t matchedCount = 0;
        for (unsigned i = 0; i < layer->m_objects->count(); ++i)
        {
            auto *object = typeinfo_cast<EnhancedGameObject *>(layer->m_objects->objectAtIndex(i));
            if (!object)
                continue;

            auto it = byID.find(object->m_uniqueID);
            if (it == byID.end())
                continue;

            auto const &state = *it->second;
            if (object->m_objectID != state.objectID ||
                std::abs(object->m_positionX - state.x) > 0.01 ||
                std::abs(object->m_positionY - state.y) > 0.01)
                continue;

            object->m_activatedByPlayer1 = state.player1;
            object->m_activatedByPlayer2 = state.player2;
            ++matchedCount;
        }
        log::info("[Continuity] restored activation states: saved={} matched={}", activations.size(), matchedCount);
    }

    LevelRecord const *find(PlayLayer *layer)
    {
        auto const key = levelKey(layer);
        auto it = s_levels.find(key);
        if (it == s_levels.end())
        {
            log::info("[Continuity] no saved record for key={} level={}", key,
                      static_cast<void *>(layer ? layer->m_level : nullptr));
            return nullptr;
        }

        auto const signature = signatureOf(layer);
        if (!(it->second.signature == signature))
        {
            log::info("[Continuity] saved record invalidated for key={}: signature changed saved=({},{},{},{}) current=({},{},{},{})",
                      key, it->second.signature.version, it->second.signature.timestamp,
                      it->second.signature.length, it->second.signature.levelStringSize,
                      signature.version, signature.timestamp, signature.length, signature.levelStringSize);
            s_levels.erase(it);
            return nullptr;
        }

        if (it->second.checkpoints.empty())
        {
            log::info("[Continuity] saved record has no checkpoints for key={}; removing it", key);
            s_levels.erase(it);
            return nullptr;
        }
        log::info("[Continuity] found saved record for key={} checkpoints={}", key, it->second.checkpoints.size());
        return &it->second;
    }

    void save(PlayLayer *layer, std::vector<SavedCheckpoint> checkpoints)
    {
        if (checkpoints.empty())
        {
            log::info("[Continuity] save requested with no checkpoints; erasing key={}", levelKey(layer));
            erase(layer);
            return;
        }

        auto const key = levelKey(layer);
        auto const count = checkpoints.size();
        s_levels[key] = LevelRecord{signatureOf(layer), std::move(checkpoints)};
        log::info("[Continuity] saved record for key={} checkpoints={} records={}", key, count, s_levels.size());
    }

    void erase(PlayLayer *layer)
    {
        auto const key = levelKey(layer);
        auto const removed = s_levels.erase(key);
        log::info("[Continuity] erased record for key={} removed={} records={}", key, removed, s_levels.size());
    }
}

class $modify(PMLPlayLayer, PlayLayer)
{
public:
    struct Fields
    {
        std::unordered_map<CheckpointObject *, continuity::SavedCheckpoint> m_live;
        bool m_restorePending = false;
        bool m_quitting = false;
    };

    void startGame()
    {
        PlayLayer::startGame();

        m_fields->m_live.clear();
        m_fields->m_quitting = false;
        m_fields->m_restorePending = continuity::enabled() && continuity::find(this) != nullptr;
        log::info("[Continuity] startGame level={} key={} enabled={} practice={} restorePending={}",
                  static_cast<void *>(m_level), continuity::levelKey(this), continuity::enabled(),
                  m_isPracticeMode, m_fields->m_restorePending);
    }

    void restoreSavedCheckpoints()
    {
        m_fields->m_restorePending = false;
        log::info("[Continuity] restore requested level={} practice={} player1={} checkpointArray={}",
                  static_cast<void *>(m_level), m_isPracticeMode, static_cast<void *>(m_player1),
                  static_cast<void *>(m_checkpointArray));
        if (!m_level || !m_player1 || !m_checkpointArray)
        {
            log::info("[Continuity] restore aborted: required level/player/checkpoint array is unavailable");
            return;
        }

        auto const *record = continuity::find(this);
        if (!record)
        {
            log::info("[Continuity] restore aborted: no valid saved record");
            return;
        }

        auto saved = record->checkpoints;
        log::info("[Continuity] restoring {} checkpoints; current checkpoint count={}",
                  saved.size(), m_checkpointArray->count());

        if (!m_isPracticeMode)
        {
            log::info("[Continuity] entering practice mode before checkpoint restore");
            this->togglePracticeMode(true);
        }

        std::size_t restoredCount = 0;
        for (auto const &entry : saved)
        {
            auto *checkpoint = this->createCheckpoint();
            if (!checkpoint)
            {
                log::info("[Continuity] checkpoint creation failed after {} of {} entries", restoredCount, saved.size());
                break;
            }

            entry.applyTo(checkpoint);
            PlayLayer::storeCheckpoint(checkpoint);

            auto &live = m_fields->m_live[checkpoint];
            live = entry;
            live.restored = true;
            ++restoredCount;
        }

        log::info("[Continuity] created {} of {} restored checkpoints; resetting level", restoredCount, saved.size());
        this->resetLevel();

        if (auto *fmod = FMODAudioEngine::sharedEngine())
        {
            fmod->setMusicTimeMS(saved.back().musicTimeMS, true, 0);
            log::info("[Continuity] restored music time to {} ms", saved.back().musicTimeMS);
        }
        else
        {
            log::info("[Continuity] music time not restored: FMODAudioEngine unavailable");
        }

        log::info("[Continuity] restore complete: {} checkpoints", restoredCount);
    }

    void persist(bool allowLiveSnapshot)
    {
        if (!m_level)
        {
            log::info("[Continuity] persist skipped: level is null");
            return;
        }

        if (m_hasCompletedLevel)
        {
            log::info("[Continuity] persist erasing record: level completed allowLiveSnapshot={}",
                      allowLiveSnapshot);
            continuity::erase(this);
            return;
        }

        if (!m_isPracticeMode)
        {
            log::info("[Continuity] persist skipped outside practice mode; preserving saved record allowLiveSnapshot={}",
                      allowLiveSnapshot);
            return;
        }

        std::vector<continuity::SavedCheckpoint> list;
        if (m_checkpointArray)
        {
            auto const checkpointCount = m_checkpointArray->count();
            for (unsigned i = 0; i < m_checkpointArray->count(); ++i)
            {
                auto *checkpoint = static_cast<CheckpointObject *>(m_checkpointArray->objectAtIndex(i));
                auto it = m_fields->m_live.find(checkpoint);
                if (it != m_fields->m_live.end())
                    list.push_back(it->second);
                else
                    log::info("[Continuity] checkpoint {} of {} has no live snapshot",
                              i + 1, checkpointCount);
            }
            log::info("[Continuity] persist collected {}/{} live checkpoint snapshots",
                      list.size(), checkpointCount);
        }
        else
        {
            log::info("[Continuity] persist has no checkpoint array");
        }

        if (list.empty() && allowLiveSnapshot && m_player1 && !m_player1->m_isDead)
        {
            if (auto *checkpoint = this->createCheckpoint())
            {
                list.push_back(continuity::SavedCheckpoint::capture(this, checkpoint));
                log::info("[Continuity] persist captured a live-player snapshot");
            }
            else
            {
                log::info("[Continuity] live-player snapshot failed: createCheckpoint returned null");
            }
        }

        log::info("[Continuity] persisting {} checkpoints allowLiveSnapshot={} player1={} dead={}",
                  list.size(), allowLiveSnapshot, static_cast<void *>(m_player1),
                  m_player1 ? m_player1->m_isDead : false);
        continuity::save(this, std::move(list));
    }

    void storeCheckpoint(CheckpointObject *checkpoint)
    {
        PlayLayer::storeCheckpoint(checkpoint);

        if (!continuity::enabled() || !checkpoint || !m_player1 || !m_level)
        {
            log::info("[Continuity] checkpoint store not tracked: enabled={} checkpoint={} player1={} level={}",
                      continuity::enabled(), static_cast<void *>(checkpoint),
                      static_cast<void *>(m_player1), static_cast<void *>(m_level));
            return;
        }

        m_fields->m_live.insert_or_assign(checkpoint, continuity::SavedCheckpoint::capture(this, checkpoint));
        log::info("[Continuity] tracked checkpoint={} liveSnapshots={}",
                  static_cast<void *>(checkpoint), m_fields->m_live.size());
    }

    void removeCheckpoint(bool first)
    {
        if (m_checkpointArray && m_checkpointArray->count() > 0)
        {
            auto *target = first ? m_checkpointArray->objectAtIndex(0) : m_checkpointArray->lastObject();
            m_fields->m_live.erase(static_cast<CheckpointObject *>(target));
            log::info("[Continuity] removing {} checkpoint={} liveSnapshotsBeforeBase={}",
                      first ? "first" : "latest", static_cast<void *>(target), m_fields->m_live.size());
        }
        else
        {
            log::info("[Continuity] removeCheckpoint called with no checkpoints; first={}", first);
        }

        PlayLayer::removeCheckpoint(first);
    }

    void removeAllCheckpoints()
    {
        log::info("[Continuity] removing all checkpoints; liveSnapshots={}", m_fields->m_live.size());
        m_fields->m_live.clear();
        PlayLayer::removeAllCheckpoints();
    }

    void loadFromCheckpoint(CheckpointObject *checkpoint)
    {
        PlayLayer::loadFromCheckpoint(checkpoint);

        if (!continuity::enabled())
            return;

        auto it = m_fields->m_live.find(checkpoint);
        if (it != m_fields->m_live.end() && it->second.restored)
        {
            log::info("[Continuity] applying restored object activations for checkpoint={}",
                      static_cast<void *>(checkpoint));
            it->second.applyActivations(this);
        }
        else
        {
            log::info("[Continuity] no restored activation snapshot for checkpoint={}",
                      static_cast<void *>(checkpoint));
        }
    }

    void togglePracticeMode(bool toggle)
    {
        if (!toggle && continuity::enabled())
        {
            log::info("[Continuity] practice mode disabled; preserving saved record and clearing {} live snapshots",
                      m_fields->m_live.size());
            m_fields->m_live.clear();
        }

        log::info("[Continuity] togglePracticeMode toggle={} enabled={} quitting={}",
                  toggle, continuity::enabled(), m_fields->m_quitting);
        PlayLayer::togglePracticeMode(toggle);
    }

    void postUpdate(float dt)
    {
        PlayLayer::postUpdate(dt);

        if (m_fields->m_restorePending && continuity::enabled())
        {
            log::info("[Continuity] deferred restore triggered from postUpdate");
            this->restoreSavedCheckpoints();
        }
    }

    void onQuit()
    {
        if (continuity::enabled())
        {
            m_fields->m_quitting = true;
            log::info("[Continuity] onQuit: persisting state with live snapshot");
            this->persist(true);
        }

        PlayLayer::onQuit();
    }

    void onExit()
    {
        if (continuity::enabled() && !m_fields->m_quitting)
        {
            log::info("[Continuity] onExit: persisting state without live snapshot");
            this->persist(false);
        }

        PlayLayer::onExit();
    }
};