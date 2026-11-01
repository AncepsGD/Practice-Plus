#pragma once

#include <Geode/Geode.hpp>
#include <cstdint>
#include <vector>

namespace continuity
{
    bool enabled();

    struct ActivationState
    {
        int uniqueID = 0;
        int objectID = 0;
        double x = 0.0;
        double y = 0.0;
        bool player1 = false;
        bool player2 = false;
    };

    struct GameStateSlice
    {
        bool isDualMode = false;
        float middleGroundOffsetY = 0.0f;
        int dualRelated = 0;
        bool mirrored = false;
        float cameraZoom = 0.0f;
        float targetCameraZoom = 0.0f;
        cocos2d::CCPoint cameraOffset = cocos2d::CCPointZero;
        cocos2d::CCPoint cameraPosition = cocos2d::CCPointZero;
        cocos2d::CCPoint cameraPosition2 = cocos2d::CCPointZero;
        cocos2d::CCPoint cameraStepDiff = cocos2d::CCPointZero;
        float cameraAngle = 0.0f;
        float targetCameraAngle = 0.0f;
        float timeWarp = 1.0f;
        float queuedTimeWarp = 1.0f;
        float timeWarpRelated = 1.0f;
        float timeModRelated = 1.0f;
        bool timeModRelated2 = false;
        double totalTime = 0.0;
        double levelTime = 0.0;
        unsigned int currentProgress = 0;
        int currentChannel = 0;
        int rotateChannel = 0;
        float levelFlipping = 0.0f;

        static GameStateSlice capture(GJGameState const &state);
        void apply(GJGameState &state) const;
    };

    struct SavedCheckpoint
    {
        std::vector<std::uint8_t> player1;
        std::vector<std::uint8_t> player2;
        GameStateSlice game;
        int commandIndex = 0;
        int musicTimeMS = 0;
        std::vector<ActivationState> activations;
        bool restored = false;

        static SavedCheckpoint capture(PlayLayer *layer, CheckpointObject *checkpoint);
        void applyTo(CheckpointObject *checkpoint) const;
        void applyActivations(PlayLayer *layer) const;
    };

    struct LevelSignature
    {
        int version = 0;
        int timestamp = 0;
        int length = 0;
        std::size_t levelStringSize = 0;

        bool operator==(LevelSignature const &other) const
        {
            return version == other.version && timestamp == other.timestamp &&
                   length == other.length && levelStringSize == other.levelStringSize;
        }
    };

    struct LevelRecord
    {
        LevelSignature signature;
        std::vector<SavedCheckpoint> checkpoints;
    };

    LevelRecord const *find(PlayLayer *layer);
    void save(PlayLayer *layer, std::vector<SavedCheckpoint> checkpoints);
    void erase(PlayLayer *layer);
}