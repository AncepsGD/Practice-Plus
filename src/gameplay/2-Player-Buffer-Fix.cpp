#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/Geode.hpp>
#include <array>

using namespace geode::prelude;

static bool isTwoPlayerBufferFixEnabled()
{
    auto *mod = Mod::get();
    return mod && mod->getSettingValue<bool>("two-player-buffer-fix-enabled");
}

$execute
{
    if (!isTwoPlayerBufferFixEnabled())
        return;
    constexpr uintptr_t resetButtonRelease = 0x23B4EA;
    constexpr std::array<uint8_t, 24> nopRelease = {
        0x90,
        0x90,
        0x90,
        0x90,
        0x90,
        0x90,
        0x90,
        0x90,
        0x90,
        0x90,
        0x90,
        0x90,
        0x90,
        0x90,
        0x90,
        0x90,
        0x90,
        0x90,
        0x90,
        0x90,
        0x90,
        0x90,
        0x90,
        0x90,
    };

    if (!Mod::get()->patch(reinterpret_cast<void *>(base::get() + resetButtonRelease),
                           nopRelease))
    {
        log::error("Failed to patch resetLevelVariables button release");
    }
}

enum class InputMask : uint8_t
{
    None = 0,
    Jump = 1 << 0,
    Left = 1 << 1,
    Right = 1 << 2
};

constexpr bool hasInput(InputMask value, InputMask flag)
{
    return (static_cast<uint8_t>(value) & static_cast<uint8_t>(flag)) != 0;
}

struct LayerInputState
{
    InputMask player1Held = InputMask::None;
    InputMask player2Held = InputMask::None;
    bool replaying = false;
    int settleFrames = 0;
    bool reapplyScheduled = false;
};

static constexpr InputMask buttonToFlag(int button)
{
    switch (static_cast<PlayerButton>(button))
    {
    case PlayerButton::Jump:
        return InputMask::Jump;
    case PlayerButton::Left:
        return InputMask::Left;
    case PlayerButton::Right:
        return InputMask::Right;
    default:
        return InputMask::None;
    }
}
inline LayerInputState &getState(GJBaseGameLayer *layer)
{
    return *static_cast<LayerInputState *>(layer->getUserData());
}

class $modify(DualInputBaseLayer, GJBaseGameLayer)
{
    bool init()
    {
        if (!GJBaseGameLayer::init())
            return false;

        this->setUserData(new LayerInputState());
        return true;
    }

    void handleButton(bool down, int button, bool isPlayer1)
    {
        if (!isTwoPlayerBufferFixEnabled() || !m_gameState.m_isDualMode)
        {
            GJBaseGameLayer::handleButton(down, button, isPlayer1);
            return;
        }

        auto &state = getState(this);

        const auto flag = buttonToFlag(button);

        if (!state.replaying && state.settleFrames > 0 && flag != InputMask::None)
        {
            auto &tracked = isPlayer1 ? state.player1Held : state.player2Held;
            auto &other = isPlayer1 ? state.player2Held : state.player1Held;

            bool looksLikeMirror = !hasInput(tracked, flag) && hasInput(other, flag);

            if (looksLikeMirror)
                return;
        }

        auto *otherPlayer = isPlayer1 ? m_player2 : m_player1;
        const bool otherJumpBuffered = otherPlayer && otherPlayer->m_jumpBuffered;
        const bool otherHoldingLeft = otherPlayer && otherPlayer->m_holdingLeft;
        const bool otherHoldingRight = otherPlayer && otherPlayer->m_holdingRight;
        gd::map<int, bool> otherHoldingButtons;
        if (otherPlayer)
            otherHoldingButtons = otherPlayer->m_holdingButtons;

        GJBaseGameLayer::handleButton(down, button, isPlayer1);

        if (otherPlayer)
        {
            otherPlayer->m_jumpBuffered = otherJumpBuffered;
            otherPlayer->m_holdingLeft = otherHoldingLeft;
            otherPlayer->m_holdingRight = otherHoldingRight;
            otherPlayer->m_holdingButtons = otherHoldingButtons;
        }

        if (state.replaying || flag == InputMask::None)
            return;

        auto &tracked = isPlayer1 ? state.player1Held : state.player2Held;
        auto mask = static_cast<uint8_t>(tracked);
        if (down)
            mask |= static_cast<uint8_t>(flag);
        else
            mask &= ~static_cast<uint8_t>(flag);
        tracked = static_cast<InputMask>(mask);
    }

    void update(float dt)
    {
        GJBaseGameLayer::update(dt);
        auto &state = getState(this);
        if (state.settleFrames > 0)
            state.settleFrames--;
    }

    void onExit()
    {

        delete static_cast<LayerInputState *>(this->getUserData());
        this->setUserData(nullptr);
        GJBaseGameLayer::onExit();
    }
};

static void reapplyInputs(GJBaseGameLayer *layer)
{
    auto &state = getState(layer);
    state.replaying = true;

    auto clearHolds = [](PlayerObject *player, bool platformer)
    {
        if (!player)
            return;
        player->releaseAllButtons();
        player->m_holdingLeft = false;
        player->m_holdingRight = false;
        if (platformer)
        {
            player->m_holdingButtons[2] = false;
            player->m_holdingButtons[3] = false;
        }
    };

    bool platformer = false;
    if (auto *playLayer = typeinfo_cast<PlayLayer *>(layer))
    {
        platformer = playLayer->m_levelSettings && playLayer->m_levelSettings->m_platformerMode;
    }

    clearHolds(layer->m_player1, platformer);
    clearHolds(layer->m_player2, platformer);

    auto replay = [layer](InputMask held, bool isPlayer1)
    {
        if (hasInput(held, InputMask::Jump))
            layer->handleButton(true, static_cast<int>(PlayerButton::Jump), isPlayer1);
        if (hasInput(held, InputMask::Left))
            layer->handleButton(true, static_cast<int>(PlayerButton::Left), isPlayer1);
        if (hasInput(held, InputMask::Right))
            layer->handleButton(true, static_cast<int>(PlayerButton::Right), isPlayer1);
    };

    replay(state.player1Held, true);
    replay(state.player2Held, false);
    state.replaying = false;
}

class $modify(DualInputPlayLayer, PlayLayer)
{
    void resetLevel()
    {
        PlayLayer::resetLevel();
        if (isTwoPlayerBufferFixEnabled())
            scheduleDeferredReapply();
    }
    void fullReset()
    {
        PlayLayer::fullReset();
        if (isTwoPlayerBufferFixEnabled())
            scheduleDeferredReapply();
    }
    void resetLevelFromStart()
    {
        PlayLayer::resetLevelFromStart();
        if (isTwoPlayerBufferFixEnabled())
            scheduleDeferredReapply();
    }
    void loadFromCheckpoint(CheckpointObject *checkpoint)
    {
        PlayLayer::loadFromCheckpoint(checkpoint);
        if (isTwoPlayerBufferFixEnabled())
            scheduleDeferredReapply();
    }

    void scheduleDeferredReapply()
    {
        auto &state = getState(this);
        if (state.reapplyScheduled)
            return;

        state.reapplyScheduled = true;
        this->scheduleOnce(schedule_selector(DualInputPlayLayer::onDeferredReapply), 0.0f);
    }

    void onDeferredReapply(float)
    {
        auto &state = getState(this);
        state.reapplyScheduled = false;
        if (!isTwoPlayerBufferFixEnabled())
            return;

        reapplyInputs(this);
        state.settleFrames = 3;
    }
};
