#include <Geode/Geode.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>

using namespace geode::prelude;

class $modify(ConfirmPracticeModeExitPauseLayer, PauseLayer) {
    struct Fields {
        CCObject* pendingSender = nullptr;
    };

    bool shouldConfirmPracticeModeExit() {
        auto mod = Mod::get();
        return mod && mod->getSettingValue<bool>("confirm-practice-mode-exit");
    }

    void onNormalMode(CCObject* sender) {
        if (!shouldConfirmPracticeModeExit()) {
            PauseLayer::onNormalMode(sender);
            return;
        }

        geode::createQuickPopup(
            "Exit Practice Mode?",
            "Are you sure you want to exit practice mode?",
            "Cancel", "Yes",
            [this, sender](auto, bool btn2) {
                if (btn2) {
                    m_fields->pendingSender = sender;
                    this->scheduleOnce(
                        schedule_selector(ConfirmPracticeModeExitPauseLayer::exitPracticeMode),
                        0.0f
                    );
                }
            }
        );
    }

    void exitPracticeMode(float) {
        auto sender = m_fields->pendingSender;
        m_fields->pendingSender = nullptr;
        if (auto *playLayer = PlayLayer::get())
            playLayer->togglePracticeMode(false);
        PauseLayer::onResume(sender);
    }
};