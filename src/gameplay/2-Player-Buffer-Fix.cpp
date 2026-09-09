#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/Geode.hpp>
#include <array>

using namespace geode::prelude;

static constexpr bool kEnableBufferLogging = false;

static bool isTwoPlayerBufferFixEnabled() {
    auto mod = Mod::get();
    return mod && mod->getSettingValue<bool>("two-player-buffer-fix-enabled");
}

$execute {
    constexpr uintptr_t resetButtonRelease = 0x23B4EA;
    constexpr std::array<uint8_t, 24> nopRelease = {
        0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90,
        0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90,
        0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90,
    };
    if (!Mod::get()->patch(reinterpret_cast<void *>(base::get() + resetButtonRelease), nopRelease))
        log::error("Failed to patch reset button release");
}

enum class InputMask : uint8_t { None = 0, Jump = 1, Left = 2, Right = 4 };

constexpr bool hasInput(InputMask value, InputMask flag) {
    return (static_cast<uint8_t>(value) & static_cast<uint8_t>(flag)) != 0;
}

struct LayerInputState {
    InputMask player1Held = InputMask::None;
    InputMask player2Held = InputMask::None;
    bool resetting = false;
    bool replaying = false;
    bool correctingRespawnRelease = false;
    uint64_t eventSequence = 0;
};
struct PlayerInputState {
    bool jumpBuffered = false;
    bool holdingLeft = false;
    bool holdingRight = false;
    gd::map<int, bool> holdingButtons;
};

static LayerInputState *getState(GJBaseGameLayer *layer) {
    return static_cast<LayerInputState *>(layer ? layer->getUserData() : nullptr);
}

static bool isActualTwoPlayerMode(GJBaseGameLayer *layer) {
    auto playLayer = typeinfo_cast<PlayLayer *>(layer);
    return playLayer && playLayer->m_level && playLayer->m_level->m_twoPlayerMode;
}

static InputMask buttonMask(int button) {
    switch (static_cast<PlayerButton>(button)) {
    case PlayerButton::Jump: return InputMask::Jump;
    case PlayerButton::Left: return InputMask::Left;
    case PlayerButton::Right: return InputMask::Right;
    default: return InputMask::None;
    }
}

static void updateMask(InputMask &mask, bool down, InputMask button) {
    auto value = static_cast<uint8_t>(mask);
    auto flag = static_cast<uint8_t>(button);
    if (down) value |= flag;
    else value &= static_cast<uint8_t>(~flag);
    mask = static_cast<InputMask>(value);
}

static PlayerInputState captureInputState(PlayerObject *player) {
    if (!player) return {};
    return {player->m_jumpBuffered, player->m_holdingLeft, player->m_holdingRight, player->m_holdingButtons};
}

static void restoreInputState(PlayerObject *player, const PlayerInputState &state) {
    if (!player) return;
    player->m_jumpBuffered = state.jumpBuffered;
    player->m_holdingLeft = state.holdingLeft;
    player->m_holdingRight = state.holdingRight;
    player->m_holdingButtons = state.holdingButtons;
}

static void replayInput(PlayerObject *player, InputMask held) {
    if (!player) return;
    player->m_jumpBuffered = hasInput(held, InputMask::Jump);
    player->m_holdingLeft = hasInput(held, InputMask::Left);
    player->m_holdingRight = hasInput(held, InputMask::Right);
    player->m_holdingButtons[2] = player->m_holdingLeft;
    player->m_holdingButtons[3] = player->m_holdingRight;
}

static void logPlayerState(char const *label, PlayerObject *player) {
    if constexpr (!kEnableBufferLogging) return;
    if (!player) {
        log::info("[2PBuffer] {} player=null", label);
        return;
    }
    log::info("[2PBuffer] {} jump={} left={} right={} buttons2={} buttons3={} dead={}",
        label,
        player->m_jumpBuffered,
        player->m_holdingLeft,
        player->m_holdingRight,
        player->m_holdingButtons[2],
        player->m_holdingButtons[3],
        player->m_isDead
    );
}

class $modify(DualInputBaseLayer, GJBaseGameLayer) {
    bool init() {
        if (!GJBaseGameLayer::init()) return false;
        setUserData(new LayerInputState());
        return true;
    }

    void handleButton(bool down, int button, bool isPlayer1) {
        if (!isTwoPlayerBufferFixEnabled() || !isActualTwoPlayerMode(this)) {
            GJBaseGameLayer::handleButton(down, button, isPlayer1);
            return;
        }

        auto state = getState(this);
        if (!state) {
            GJBaseGameLayer::handleButton(down, button, isPlayer1);
            return;
        }
        auto flag = buttonMask(button);
        auto sequence = ++state->eventSequence;
        bool redirectRelease = !state->replaying && state->correctingRespawnRelease &&
            isPlayer1 && !down && flag != InputMask::None &&
            !hasInput(state->player1Held, flag) && hasInput(state->player2Held, flag);
        bool suppressMirroredPress = !state->replaying && state->correctingRespawnRelease &&
            isPlayer1 && down && flag != InputMask::None &&
            !hasInput(state->player1Held, flag) && hasInput(state->player2Held, flag);
        if constexpr (kEnableBufferLogging) {
            log::info("[2PBuffer] #{} input player={} {} button={} flag={} redirect={} suppress={} resetting={} replaying={} p1mask={} p2mask={}",
                sequence,
                isPlayer1 ? 1 : 2,
                down ? "press" : "release",
                button,
                static_cast<int>(flag),
                redirectRelease,
                suppressMirroredPress,
                state->resetting,
                state->replaying,
                static_cast<int>(state->player1Held),
                static_cast<int>(state->player2Held)
            );
        }
        if (!state->replaying && !suppressMirroredPress && flag != InputMask::None)
            updateMask(redirectRelease ? state->player2Held : (isPlayer1 ? state->player1Held : state->player2Held), down, flag);
        if (redirectRelease)
            state->correctingRespawnRelease = false;
        if (!state->resetting && !isPlayer1 && !down && flag != InputMask::None)
            state->correctingRespawnRelease = false;
        if (suppressMirroredPress)
            return;

        auto routedPlayer1 = redirectRelease ? false : isPlayer1;
        auto other = routedPlayer1 ? m_player2 : m_player1;
        auto otherState = captureInputState(other);
        logPlayerState("before vanilla p1", m_player1);
        logPlayerState("before vanilla p2", m_player2);
        GJBaseGameLayer::handleButton(down, button, routedPlayer1);
        restoreInputState(other, otherState);
        logPlayerState("after vanilla p1", m_player1);
        logPlayerState("after vanilla p2", m_player2);
    }

    void onExit() {
        delete static_cast<LayerInputState *>(getUserData());
        setUserData(nullptr);
        GJBaseGameLayer::onExit();
    }
};

class $modify(DualInputPlayLayer, PlayLayer) {
    template <class Reset>
    void resetWithSnapshot(Reset reset) {
        if (!isTwoPlayerBufferFixEnabled() || !isActualTwoPlayerMode(this)) {
            reset();
            return;
        }

        auto state = getState(this);
        if (!state) {
            reset();
            return;
        }
        if constexpr (kEnableBufferLogging)
            log::info("[2PBuffer] reset begin p1mask={} p2mask={}", static_cast<int>(state->player1Held), static_cast<int>(state->player2Held));
        state->resetting = true;
        reset();
        state->resetting = false;
        state->replaying = true;
        replayInput(m_player1, state->player1Held);
        replayInput(m_player2, state->player2Held);
        state->correctingRespawnRelease = state->player2Held != InputMask::None;
        state->replaying = false;
        if constexpr (kEnableBufferLogging) {
            log::info("[2PBuffer] reset replay p1mask={} p2mask={}", static_cast<int>(state->player1Held), static_cast<int>(state->player2Held));
            logPlayerState("replay p1", m_player1);
            logPlayerState("replay p2", m_player2);
        }
    }

    void resetLevel() { resetWithSnapshot([this] { PlayLayer::resetLevel(); }); }
    void fullReset() { resetWithSnapshot([this] { PlayLayer::fullReset(); }); }
    void resetLevelFromStart() { resetWithSnapshot([this] { PlayLayer::resetLevelFromStart(); }); }
    void loadFromCheckpoint(CheckpointObject *checkpoint) {
        resetWithSnapshot([this, checkpoint] { PlayLayer::loadFromCheckpoint(checkpoint); });
    }
};
