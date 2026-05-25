#include "main.hpp"
#include <Geode/Geode.hpp>
#include <Geode/modify/CreatorLayer.hpp>
#include <Geode/modify/GameLevelManager.hpp>
#include <Geode/modify/LevelBrowserLayer.hpp>
#include <Geode/modify/GJSearchObject.hpp>
#include <matjson.hpp>
#include <vector>
#include <string>
#include <map>
#include <algorithm>

// NOTE: The following code features AI-assisted code. Any code with AI-assistance will be marked with @@@
// ANOTHER NOTE: Code for the PML was made with https://github.com/hiimjasmine00/IntegratedDemonlist/tree/master as a reference.

using namespace geode::prelude;

std::vector<PML> PracticeModeList::levels;
bool PracticeModeList::levelsLoaded = false;
std::string PracticeModeList::currentListUrl = "https://raw.githubusercontent.com/AncepsGD/practice-mode-list/refs/heads/main/levels.json";

// Recursively finds and removes any active loading circles 
void removeLoadingCircles(cocos2d::CCNode* parent) {
    if (!parent) return;
    auto children = parent->getChildren();
    if (!children) return;
    for (int i = children->count() - 1; i >= 0; --i) {
        auto child = static_cast<cocos2d::CCNode*>(children->objectAtIndex(i));
        if (!child) continue;
        if (auto circle = dynamic_cast<LoadingCircle*>(child)) {
            circle->fadeAndRemove();
        } else {
            removeLoadingCircles(child);
        }
    }
}

// Rate-limiter using Cocos scheduler @@@
class PMLDelayHandler : public cocos2d::CCObject {
public:
    std::function<void()> on_trigger;

    void update(float dt) {
        auto sched = cocos2d::CCDirector::sharedDirector()->getScheduler();
        sched->unscheduleSelector(schedule_selector(PMLDelayHandler::update), this);
        if (on_trigger) on_trigger();
        this->release();
    }

    static void sched_delay(float delay_amount, std::function<void()> cb) {
        auto timer_obj = new PMLDelayHandler();
        timer_obj->on_trigger = cb;
        timer_obj->retain();

        auto sched = cocos2d::CCDirector::sharedDirector()->getScheduler();
        sched->scheduleSelector(
            schedule_selector(PMLDelayHandler::update),
            timer_obj,
            delay_amount,
            false
        );
    }
};

struct PMLSequentialFetch {
    bool active = false;
    bool delivering = false;
    int page = 0;
    std::vector<int> expected_ids;
    size_t current_index = 0;
    std::map<int, GJGameLevel*> gathered_levels;
    SearchType orig_type = static_cast<SearchType>(19);
    std::string orig_query = "";
    int consecutive_failures = 0; // Tracks consecutive network/empty failures
};
static PMLSequentialFetch pmlFetch;

// Track active UI layer to prevent transition issues
static LevelBrowserLayer* activeBrowser = nullptr;

void resetPmlFetch() {
    pmlFetch.active = false;
    pmlFetch.delivering = false;
    pmlFetch.page = 0;
    pmlFetch.expected_ids.clear();
    pmlFetch.current_index = 0;
    pmlFetch.consecutive_failures = 0;

    for (auto const& [id, level] : pmlFetch.gathered_levels) {
        if (level) {
            level->release();
        }
    }
    pmlFetch.gathered_levels.clear();
}

void fetchNextPmlLevel();

// Request levels one by one @@@
void fetchNextPmlLevel() {
    if (!pmlFetch.active) {
        return;
    }
    if (!activeBrowser) {
        Notification::create("Error: Browser context lost!", NotificationIcon::Error)->show();
        return;
    }

    // Rate-Limit Check: If 3 or more consecutive requests fail, stop fetching immediately to prevent temp-ban
    if (pmlFetch.consecutive_failures >= 3) {
        pmlFetch.active = false;
        removeLoadingCircles(cocos2d::CCDirector::sharedDirector()->getRunningScene());
        
        FLAlertLayer::create(
            "Rate-Limit Warning", 
            "Multiple requests in a row failed.\n"
            "RobTop's servers may be temporarily rate-limiting or blocking your IP address.\n"
            "Please wait a few minutes before trying to load the list again.", 
            "OK"
        )->show();
        return;
    }

    // Display the levels onto the page after they are finished
    if (pmlFetch.current_index >= pmlFetch.expected_ids.size()) {
        pmlFetch.active = false;
        pmlFetch.delivering = true;

        auto final_array = cocos2d::CCArray::create();
        int successfully_loaded = 0;
        for (int id : pmlFetch.expected_ids) {
            if (pmlFetch.gathered_levels.count(id)) {
                final_array->addObject(pmlFetch.gathered_levels[id]);
                successfully_loaded++;
            }
        }
        
        if (successfully_loaded == 0) {
            FLAlertLayer::create("Warning", "No levels on this page loaded from the servers.", "OK")->show();
        }

        // Send levels directly to level view
        activeBrowser->setupLevelBrowser(final_array);
        
        // Remove any LoadingCircles
        removeLoadingCircles(cocos2d::CCDirector::sharedDirector()->getRunningScene());
        return;
    }

    int levelId = pmlFetch.expected_ids[pmlFetch.current_index];
    // @@@
    auto singleSearch = GJSearchObject::create(SearchType::Search, std::to_string(levelId));
    singleSearch->retain(); // Keep search object alive

    // Limit requests by 1.0 seconds to keep Rob's servers happy :) [hopefully]
    PMLDelayHandler::sched_delay(1.0f, [singleSearch, levelId]() {
        if (pmlFetch.active) {
            GameLevelManager::sharedState()->getOnlineLevels(singleSearch);
        }
        singleSearch->release();
    });
}

class $modify(MyGameLevelManager, GameLevelManager) {
    void getOnlineLevels(GJSearchObject* searchObj) {
        // @@@
        if (searchObj && searchObj->m_searchType == static_cast<SearchType>(19) && !pmlFetch.active) {
            resetPmlFetch();

            int startIdx = searchObj->m_page * 10;
            int endIdx = std::min(startIdx + 10, static_cast<int>(PracticeModeList::levels.size()));
            

            for (int i = startIdx; i < endIdx; ++i) {
                pmlFetch.expected_ids.push_back(PracticeModeList::levels[i].id);
            }

            if (pmlFetch.expected_ids.empty()) {
                FLAlertLayer::create("Error", "End of PML list or invalid.", "OK")->show();
                return;
            }

            pmlFetch.active = true;
            pmlFetch.current_index = 0;
            pmlFetch.page = searchObj->m_page;
            pmlFetch.orig_type = searchObj->m_searchType;
            pmlFetch.orig_query = searchObj->m_searchQuery;

            // Spawn loading circles
            if (activeBrowser && activeBrowser->isRunning()) {
                
                auto loading = LoadingCircle::create();
                loading->setParent(cocos2d::CCDirector::sharedDirector()->getRunningScene());
                loading->show();

                fetchNextPmlLevel();
            }
            return;
        }
        GameLevelManager::getOnlineLevels(searchObj);
    }
};

class $modify(MyLevelBrowserLayer, LevelBrowserLayer) {
    bool init(GJSearchObject* searchObj) {
        resetPmlFetch();
        activeBrowser = this;

        if (!LevelBrowserLayer::init(searchObj)) {
            return false;
        }

        if (searchObj && searchObj->m_searchType == static_cast<SearchType>(19)) {
            this->m_itemCount = PracticeModeList::levels.size();
        }
        return true;
    }

    void onEnter() {
        LevelBrowserLayer::onEnter();

        if (pmlFetch.active && pmlFetch.current_index == 0) {
            fetchNextPmlLevel();
        }
    }

    void onExit() {
        if (activeBrowser == this) {
            activeBrowser = nullptr;
        }
        resetPmlFetch();
        LevelBrowserLayer::onExit();
    }

    void loadLevelsFinished(cocos2d::CCArray* levels, char const* key, int p2) {
        if (pmlFetch.active) {
            int targetId = pmlFetch.expected_ids[pmlFetch.current_index];
            std::string keyStr = key ? key : "";
            std::string targetIdStr = std::to_string(targetId);

            if (keyStr.find(targetIdStr) != std::string::npos) {
                GJGameLevel* matchedLvl = nullptr;

                if (levels) {
                    for (int i = 0; i < levels->count(); ++i) {
                        auto lvl = static_cast<GJGameLevel*>(levels->objectAtIndex(i));
                        if (lvl && static_cast<int>(lvl->m_levelID) == targetId) {
                            matchedLvl = lvl;
                            break;
                        }
                    }

                    // Fallback check @@@
                    if (!matchedLvl) {
                        for (int i = 0; i < levels->count(); ++i) {
                            auto lvl = static_cast<GJGameLevel*>(levels->objectAtIndex(i));
                            if (lvl) {
                                int levelIdVal = static_cast<int>(lvl->m_levelID);
                                if (std::find(pmlFetch.expected_ids.begin(), pmlFetch.expected_ids.end(), levelIdVal) != pmlFetch.expected_ids.end()) {
                                    matchedLvl = lvl;
                                    break;
                                }
                            }
                        }
                    }
                }

                if (matchedLvl) {
                    int resolvedId = static_cast<int>(matchedLvl->m_levelID);
                    if (pmlFetch.gathered_levels.count(resolvedId) == 0) {
                        pmlFetch.gathered_levels[resolvedId] = matchedLvl;
                        matchedLvl->retain();
                    }
                    pmlFetch.consecutive_failures = 0;
                } else {
                    Notification::create(fmt::format("Failed level ID: {}", targetId), NotificationIcon::Warning)->show();
                    pmlFetch.consecutive_failures++;
                }

                pmlFetch.current_index++;
                fetchNextPmlLevel();
                return;
            }
        }

        LevelBrowserLayer::loadLevelsFinished(levels, key, p2);
    }

    void loadLevelsFailed(char const* key, int errorCode) { // @@@
        if (pmlFetch.active) {
            int failedId = pmlFetch.expected_ids[pmlFetch.current_index];
            std::string keyStr = key ? key : "";
            std::string failedIdStr = std::to_string(failedId);

            if (keyStr.find(failedIdStr) != std::string::npos) {
                Notification::create(fmt::format("Skipped ID (Failed): {}", failedId), NotificationIcon::Warning)->show();
                pmlFetch.consecutive_failures++;

                pmlFetch.current_index++;
                fetchNextPmlLevel();
                return;
            }
        }
        LevelBrowserLayer::loadLevelsFailed(key, errorCode);
    }

    void setupLevelBrowser(cocos2d::CCArray* levels) {
        if (pmlFetch.delivering) {
            pmlFetch.delivering = false;
            
            this->m_itemCount = PracticeModeList::levels.size();
            LevelBrowserLayer::setupLevelBrowser(levels);
            this->m_itemCount = PracticeModeList::levels.size();

            // Force Draw Navigation Arrows/Controls
            if (this->m_searchObject && this->m_searchObject->m_searchType == static_cast<SearchType>(19)) {
                if (auto rightArrow = this->getChildByID("next-page-menu")) {
                    rightArrow->setVisible(true);
                    if (auto btn = rightArrow->getChildByID("next-page-button")) btn->setVisible(true);
                }
                if (auto leftArrow = this->getChildByID("prev-page-menu")) {
                    leftArrow->setVisible(true);
                    if (auto btn = leftArrow->getChildByID("prev-page-button")) btn->setVisible(true);
                }
                
                if (auto pageLabel = this->getChildByID("page-text-label")) pageLabel->setVisible(true);
                this->updatePageLabel();
            }
            return;
        }
        LevelBrowserLayer::setupLevelBrowser(levels);
    }
};

void PracticeModeList::loadPracticeList( // @@@
    TaskHolder<web::WebResponse>& listener,
    Function<void()> success,
    CopyableFunction<void(int)> failure
) {
    listener.spawn(
        // Fetch JSON data from requested URL
        web::WebRequest().get(currentListUrl),
        [failure = std::move(failure), success = std::move(success)](web::WebResponse answer) mutable {
            if (!answer.ok()) {
                return failure(answer.code());
            }
            
            levelsLoaded = true;
            levels.clear();

            auto jsonRes = answer.json();
            if (!jsonRes.isOk()) {
                return failure(-1);
            }

            auto json = jsonRes.unwrap();

            auto arrayRes = json.as<std::vector<matjson::Value>>();
            if (!arrayRes.isOk()) {
                return failure(-2);
            }
            
            for (auto& level : arrayRes.unwrap()) {
                int rankValue = 0;
                int idValue = 0;
                
                auto rankRes = level.get<int>("rank");
                if (rankRes.isOk()) {
                    rankValue = rankRes.unwrap();
                } else {
                    auto rankStrRes = level.get<std::string>("rank");
                    if (rankStrRes.isOk()) {
                        auto parsed = geode::utils::numFromString<int>(rankStrRes.unwrap());
                        if (parsed.isOk()) rankValue = parsed.unwrap();
                    }
                }

                auto idRes = level.get<int>("id");
                if (idRes.isOk()) {
                    idValue = idRes.unwrap();
                } else {
                    auto idStrRes = level.get<std::string>("id");
                    if (idStrRes.isOk()) {
                        auto parsed = geode::utils::numFromString<int>(idStrRes.unwrap());
                        if (parsed.isOk()) idValue = parsed.unwrap();
                    }
                }

                if (rankValue > 0 && idValue > 0) {
                    PML demon{ idValue, rankValue };

                    levels.insert(
                        std::upper_bound(levels.begin(), levels.end(), demon, [](const PML& a, const PML& b) {
                            return a.rank < b.rank;
                        }),
                        std::move(demon)
                    );
                }
            }
            
            if (levels.empty()) {
                return failure(-3);
            }

            success();
        }
    );
}

// Add PML list buttons to the Creator Layer
class $modify(MyCreatorLayer, CreatorLayer) {
    struct Fields {
        TaskHolder<web::WebResponse> m_listener;
    };

    bool init() {
        if (!CreatorLayer::init()) return false;

        auto creatorMenu = this->getChildByID("creator-buttons-menu");
        if (!creatorMenu) return true;

        // 1. Practice Mode List button
        auto spritePML = CCSprite::createWithSpriteFrameName("GJ_viewListsBtn_001.png");
        auto buttonPML = CCMenuItemSpriteExtra::create(
            spritePML,
            this,
            menu_selector(MyCreatorLayer::onPMLClick)
        );
        buttonPML->setID("PML-button");

        // 2. Open Verifications list button
        auto spriteVerif = CCSprite::createWithSpriteFrameName("GJ_viewListsBtn_001.png");
        auto buttonVerif = CCMenuItemSpriteExtra::create(
            spriteVerif,
            this,
            menu_selector(MyCreatorLayer::onVerificationsClick)
        );
        buttonVerif->setID("verifications-button");

        creatorMenu->addChild(buttonPML);
        creatorMenu->addChild(buttonVerif);

        creatorMenu->updateLayout();
        return true;
    }

    void onPMLClick(CCObject* sender) {
        PracticeModeList::currentListUrl = "https://raw.githubusercontent.com/AncepsGD/practice-mode-list/refs/heads/main/levels.json";
        this->startPmlLoadingSequence();
    }

    void onVerificationsClick(CCObject* sender) {
        PracticeModeList::currentListUrl = "https://raw.githubusercontent.com/AncepsGD/practice-mode-list/refs/heads/main/verifications.json";
        this->startPmlLoadingSequence();
    }

    void startPmlLoadingSequence() { // @@@
        auto loading = LoadingCircle::create();
        loading->setParent(CCDirector::sharedDirector()->getRunningScene());
        loading->show();

        PracticeModeList::loadPracticeList(
            m_fields->m_listener,
            [this, loading]() {
                loading->fadeAndRemove();

                if (PracticeModeList::levels.empty()) {
                    FLAlertLayer::create("Error", "No levels were detected in the source JSON.", "OK")->show();
                    return;
                }

                this->launchLevelBrowser();
            },
            [loading](int errorCode) {
                loading->fadeAndRemove();
                FLAlertLayer::create(
                    "Error",
                    fmt::format("Failed to download or parse the selected list.\nCode: {}", errorCode),
                    "OK"
                )->show();
            }
        );
    }

    void launchLevelBrowser() { // @@@
        std::string searchQuery = "";
        for (size_t idx = 0; idx < PracticeModeList::levels.size(); ++idx) {
            searchQuery += std::to_string(PracticeModeList::levels[idx].id);
            if (idx < PracticeModeList::levels.size() - 1) {
                searchQuery += ",";
            }
        }

        auto searchObj = GJSearchObject::create(static_cast<SearchType>(19), searchQuery); // @@@
        auto browserLayer = LevelBrowserLayer::create(searchObj);
        auto scene = CCScene::create();
        scene->addChild(browserLayer);
        CCDirector::sharedDirector()->replaceScene(
            CCTransitionFade::create(0.5f, scene)
        );
    }
};