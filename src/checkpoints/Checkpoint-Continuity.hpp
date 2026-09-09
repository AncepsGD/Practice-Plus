#pragma once

#include <Geode/Geode.hpp>
#include <Geode/binding/CheckpointObject.hpp>
#include <vector>
#include "Checkpoint-Fix-State.hpp"

struct PersistedInteractionState
{
    int m_uniqueID = 0;
    int m_objectID = 0;
    double m_positionX = 0.0;
    double m_positionY = 0.0;
    bool m_activatedByPlayer1 = false;
    bool m_activatedByPlayer2 = false;
};

struct PlayerSnapshot
{
    PlayerState m_playerState;
    double m_gravity = 0.0;
    float m_gravityMod = 1.0f;
    double m_yVelocity = 0.0;
    int m_modeID = 0;
    PlayerState m_player2State;
    double m_player2Gravity = 0.0;
    float m_player2GravityMod = 1.0f;
    double m_player2YVelocity = 0.0;
    int m_player2ModeID = 0;
    bool m_hasPlayer2 = false;
    bool m_isOnGround = false;
    bool m_isOnGround2 = false;
    bool m_isOnGround3 = false;
    bool m_isOnGround4 = false;
    bool m_isOnSlope = false;
    bool m_wasOnSlope = false;
    double m_groundYVelocity = 0.0;
    double m_collidedTopMinY = 0.0;
    double m_collidedBottomMaxY = 0.0;
    bool m_player2IsOnGround = false;
    bool m_player2IsOnGround2 = false;
    bool m_player2IsOnGround3 = false;
    bool m_player2IsOnGround4 = false;
    bool m_player2IsOnSlope = false;
    bool m_player2WasOnSlope = false;
    double m_player2GroundYVelocity = 0.0;
    double m_player2CollidedTopMinY = 0.0;
    double m_player2CollidedBottomMaxY = 0.0;
    float m_groundHeight = 0.0f;
    float m_portalMinY = 0.0f;
    float m_portalMaxY = 0.0f;
    float m_portalY = 0.0f;
    float m_middleGroundOffsetY = 0.0f;
    int m_dualRelated = 6;
    bool m_boundsValid = false;
    int m_musicTimeMS = 0;
    bool m_isDualMode = false;
    bool m_isMirrored = false;
    float m_cameraZoom = 0.0f;
    float m_targetCameraZoom = 0.0f;
    cocos2d::CCPoint m_cameraOffset = cocos2d::CCPointZero;
    cocos2d::CCPoint m_cameraPosition = cocos2d::CCPointZero;
    cocos2d::CCPoint m_cameraPosition2 = cocos2d::CCPointZero;
    cocos2d::CCPoint m_cameraStepDiff = cocos2d::CCPointZero;
    float m_cameraAngle = 0.0f;
    float m_targetCameraAngle = 0.0f;
    float m_timeWarp = 1.0f;
    float m_queuedTimeWarp = 1.0f;
    float m_timeWarpRelated = 1.0f;
    float m_timeModRelated = 1.0f;
    bool m_timeModRelated2 = false;
    double m_totalTime = 0.0;
    double m_levelTime = 0.0;
    unsigned int m_currentProgress = 0;
    int m_currentChannel = 0;
    int m_rotateChannel = 0;
    float m_levelFlipping = 0.0f;
    std::vector<PersistedInteractionState> m_persistedInteractionStates;
    int m_checkpointCommandIndex = 0;
    bool m_hasCheckpointLevelState = false;
    bool m_isValid = false;
};

using SnapshotList = std::vector<PlayerSnapshot>;