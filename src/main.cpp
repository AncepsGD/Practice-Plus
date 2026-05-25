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

using namespace geode::prelude;

std::vector<PML> PracticeModeList::levels;
bool PracticeModeList::levelsLoaded = false;
std::string PracticeModeList::currentListUrl = "https://raw.githubusercontent.com/AncepsGD/practice-mode-list/refs/heads/main/levels.json";

// Helper to recursively find and remove any active loading circles in the scene tree
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

// Stagger helper using Cocos scheduler to keep the thread unlocked
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

// State tracker for our sequential page queries
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
static PMLSequentialFetch g_pmlFetch;

// Track the active UI layer to prevent transition scene issues
static LevelBrowserLayer* s_activeBrowser = nullptr;

void resetPmlFetch() {
    log::info("PML: Resetting fetch state. Clearing {} gathered levels.", g_pmlFetch.gathered_levels.size());
    g_pmlFetch.active = false;
    g_pmlFetch.delivering = false;
    g_pmlFetch.page = 0;
    g_pmlFetch.expected_ids.clear();
    g_pmlFetch.current_index = 0;
    g_pmlFetch.consecutive_failures = 0;

    // Release active page allocations
    for (auto const& [id, level] : g_pmlFetch.gathered_levels) {
        if (level) {
            level->release();
        }
    }
    g_pmlFetch.gathered_levels.clear();
}

// Forward declaration of sequential loader step function
void fetchNextPmlLevel();

// Stagger-request the page levels one by one using isolated search objects
void fetchNextPmlLevel() {
    if (!g_pmlFetch.active) {
        log::warn("PML Diagnostic: fetchNextPmlLevel called, but g_pmlFetch.active is FALSE!");
        return;
    }
    if (!s_activeBrowser) {
        log::error("PML Diagnostic Error: s_activeBrowser is NULL! Cannot retrieve search object properties.");
        Notification::create("Error: Browser context lost!", NotificationIcon::Error)->show();
        return;
    }

    // Rate-Limit Watchdog Check:
    // If we hit 3 or more consecutive failures, stop fetching immediately to prevent further IP blocks
    if (g_pmlFetch.consecutive_failures >= 3) {
        log::error("PML Rate-Limit Detected! Halting sequential loader after {} consecutive failures.", g_pmlFetch.consecutive_failures);
        g_pmlFetch.active = false;
        removeLoadingCircles(cocos2d::CCDirector::sharedDirector()->getRunningScene());
        
        FLAlertLayer::create(
            "Rate-Limit Warning", 
            "Multiple queries in a row failed.\n"
            "RobTop's servers may be temporarily rate-limiting or blocking your IP address.\n\n"
            "Please wait a few minutes before trying to load the list again.", 
            "OK"
        )->show();
        return;
    }

    // Finished processing the 10 levels on the current page. Assemble and render.
    if (g_pmlFetch.current_index >= g_pmlFetch.expected_ids.size()) {
        log::info("PML: All expected IDs on page {} evaluated. Assembling browser final render list.", g_pmlFetch.page);
        g_pmlFetch.active = false;
        g_pmlFetch.delivering = true;

        auto final_array = cocos2d::CCArray::create();
        int successfully_loaded = 0;
        for (int id : g_pmlFetch.expected_ids) {
            if (g_pmlFetch.gathered_levels.count(id)) {
                final_array->addObject(g_pmlFetch.gathered_levels[id]);
                successfully_loaded++;
            }
        }

        log::info("PML: Rendering complete. Found {} out of {} target levels.", successfully_loaded, g_pmlFetch.expected_ids.size());
        
        if (successfully_loaded == 0) {
            FLAlertLayer::create("Warning", "None of the levels on this page could be loaded from the servers.", "OK")->show();
        }

        // Deliver levels directly to the browser view layout
        s_activeBrowser->setupLevelBrowser(final_array);
        
        // Dynamically find and fade out any LoadingCircles across the running scene tree
        removeLoadingCircles(cocos2d::CCDirector::sharedDirector()->getRunningScene());
        return;
    }

    int levelId = g_pmlFetch.expected_ids[g_pmlFetch.current_index];
    log::info("PML: Initiating query {}/{} (Level ID: {})", g_pmlFetch.current_index + 1, g_pmlFetch.expected_ids.size(), levelId);

    // CRITICAL: We use SearchType::Search (Type 0) to resolve unlisted levels successfully by exact ID!
    auto singleSearch = GJSearchObject::create(SearchType::Search, std::to_string(levelId));
    singleSearch->retain(); // Keep search object alive during our staggered delay

    // Stagger requests by 1.0 seconds to keep Rob's servers completely safe from rate-limiting
    PMLDelayHandler::sched_delay(1.0f, [singleSearch, levelId]() {
        if (g_pmlFetch.active) {
            log::info("PML: Sending staggered query packet to server for ID {}", levelId);
            GameLevelManager::sharedState()->getOnlineLevels(singleSearch);
        } else {
            log::warn("PML: Delay timer finished, but fetch sequence was already aborted for ID {}", levelId);
        }
        singleSearch->release(); // Balance the retain call
    });
}

class $modify(MyGameLevelManager, GameLevelManager) {
    void getOnlineLevels(GJSearchObject* searchObj) {
        if (searchObj) {
            log::info("PML: getOnlineLevels called with search type: {}, query: '{}'", (int)searchObj->m_searchType, searchObj->m_searchQuery);
        }

        // Intercept standard page query initialization (Type 19)
        if (searchObj && searchObj->m_searchType == static_cast<SearchType>(19) && !g_pmlFetch.active) {
            log::info("PML: Intercepting page list search (Type 19). Populating expected IDs for page {}.", searchObj->m_page);
            resetPmlFetch();

            int startIdx = searchObj->m_page * 10;
            int endIdx = std::min(startIdx + 10, static_cast<int>(PracticeModeList::levels.size()));
            
            log::info("PML: Selected rank slice index: {} to {}. ModeList total: {}", startIdx, endIdx, PracticeModeList::levels.size());

            for (int i = startIdx; i < endIdx; ++i) {
                g_pmlFetch.expected_ids.push_back(PracticeModeList::levels[i].id);
                log::info("PML: Added ID {} at index {} to query queue.", PracticeModeList::levels[i].id, i);
            }

            if (g_pmlFetch.expected_ids.empty()) {
                log::error("PML Error: No expected level IDs resolved for this page index!");
                FLAlertLayer::create("Error", "The practice mode list is empty or invalid.", "OK")->show();
                return;
            }

            g_pmlFetch.active = true;
            g_pmlFetch.current_index = 0;
            g_pmlFetch.page = searchObj->m_page;
            g_pmlFetch.orig_type = searchObj->m_searchType;
            g_pmlFetch.orig_query = searchObj->m_searchQuery;

            // Spawn loading circles
            if (s_activeBrowser && s_activeBrowser->isRunning()) {
                log::info("PML: Browser is already running on-screen. Spawning loading circle and fetching next level.");
                
                auto loading = LoadingCircle::create();
                loading->setParent(cocos2d::CCDirector::sharedDirector()->getRunningScene());
                loading->show();

                fetchNextPmlLevel();
            } else {
                log::info("PML: Browser is transitioning. Waiting for onEnter() hook before fetching.");
            }
            return;
        }
        GameLevelManager::getOnlineLevels(searchObj);
    }
};

class $modify(MyLevelBrowserLayer, LevelBrowserLayer) {
    bool init(GJSearchObject* searchObj) {
        log::info("PML: LevelBrowserLayer::init() triggered.");
        resetPmlFetch();
        s_activeBrowser = this;

        if (!LevelBrowserLayer::init(searchObj)) {
            log::error("PML: Base LevelBrowserLayer::init returned false!");
            return false;
        }

        if (searchObj && searchObj->m_searchType == static_cast<SearchType>(19)) {
            log::info("PML: Setting browser total item count back to {}", PracticeModeList::levels.size());
            this->m_itemCount = PracticeModeList::levels.size();
        }
        return true;
    }

    void onEnter() {
        LevelBrowserLayer::onEnter();
        log::info("PML: LevelBrowserLayer is now active on screen.");

        // Kick off loading sequence
        if (g_pmlFetch.active && g_pmlFetch.current_index == 0) {
            log::info("PML: Triggering safe initial level fetch sequence.");
            fetchNextPmlLevel();
        }
    }

    void onExit() {
        log::info("PML: LevelBrowserLayer exiting. Clearing references.");
        if (s_activeBrowser == this) {
            s_activeBrowser = nullptr;
        }
        resetPmlFetch();
        LevelBrowserLayer::onExit();
    }

    void loadLevelsFinished(cocos2d::CCArray* levels, char const* key, int p2) {
        if (g_pmlFetch.active) {
            int targetId = g_pmlFetch.expected_ids[g_pmlFetch.current_index];
            std::string keyStr = key ? key : "";
            std::string targetIdStr = std::to_string(targetId);

            if (keyStr.find(targetIdStr) != std::string::npos) {
                log::info("PML: loadLevelsFinished intercepted for key: {}", keyStr);
                GJGameLevel* matchedLvl = nullptr;

                if (levels) {
                    for (int i = 0; i < levels->count(); ++i) {
                        auto lvl = static_cast<GJGameLevel*>(levels->objectAtIndex(i));
                        if (lvl && static_cast<int>(lvl->m_levelID) == targetId) {
                            matchedLvl = lvl;
                            break;
                        }
                    }

                    // Fallback check
                    if (!matchedLvl) {
                        for (int i = 0; i < levels->count(); ++i) {
                            auto lvl = static_cast<GJGameLevel*>(levels->objectAtIndex(i));
                            if (lvl) {
                                int levelIdVal = static_cast<int>(lvl->m_levelID);
                                if (std::find(g_pmlFetch.expected_ids.begin(), g_pmlFetch.expected_ids.end(), levelIdVal) != g_pmlFetch.expected_ids.end()) {
                                    matchedLvl = lvl;
                                    break;
                                }
                            }
                        }
                    }
                }

                if (matchedLvl) {
                    int resolvedId = static_cast<int>(matchedLvl->m_levelID);
                    log::info("PML Success: Successfully resolved level '{}' (ID: {})", matchedLvl->m_levelName, resolvedId);
                    if (g_pmlFetch.gathered_levels.count(resolvedId) == 0) {
                        g_pmlFetch.gathered_levels[resolvedId] = matchedLvl;
                        matchedLvl->retain();
                    }
                    g_pmlFetch.consecutive_failures = 0;
                } else {
                    log::warn("PML: Level ID {} returned zero server matches", targetId);
                    Notification::create(fmt::format("Failed level ID: {}", targetId), NotificationIcon::Warning)->show();
                    g_pmlFetch.consecutive_failures++;
                }

                g_pmlFetch.current_index++;
                fetchNextPmlLevel();
                return;
            }
        }

        LevelBrowserLayer::loadLevelsFinished(levels, key, p2);
    }

    void loadLevelsFailed(char const* key, int errorCode) {
        if (g_pmlFetch.active) {
            int failedId = g_pmlFetch.expected_ids[g_pmlFetch.current_index];
            std::string keyStr = key ? key : "";
            std::string failedIdStr = std::to_string(failedId);

            if (keyStr.find(failedIdStr) != std::string::npos) {
                log::warn("PML Warning: Online query failed for level ID {}", failedId);
                Notification::create(fmt::format("Skipped ID (Failed): {}", failedId), NotificationIcon::Warning)->show();
                g_pmlFetch.consecutive_failures++;

                g_pmlFetch.current_index++;
                fetchNextPmlLevel();
                return;
            }
        }
        LevelBrowserLayer::loadLevelsFailed(key, errorCode);
    }

    void setupLevelBrowser(cocos2d::CCArray* levels) {
        if (g_pmlFetch.delivering) {
            log::info("PML: Delivering final array bypass triggered.");
            g_pmlFetch.delivering = false;
            
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

void PracticeModeList::loadPracticeList(
    TaskHolder<web::WebResponse>& listener,
    Function<void()> success,
    CopyableFunction<void(int)> failure
) {
    log::info("PML: Starting fetch from GitHub URL: {}", currentListUrl);
    listener.spawn(
        web::WebRequest().get(currentListUrl),
        [failure = std::move(failure), success = std::move(success)](web::WebResponse answer) mutable {
            if (!answer.ok()) {
                log::error("PML Error: GitHub request failed with web response code: {}", answer.code());
                return failure(answer.code());
            }
            
            levelsLoaded = true;
            levels.clear();

            auto jsonRes = answer.json();
            if (!jsonRes.isOk()) {
                log::error("PML Error: Received malformed JSON format!");
                return failure(-1);
            }

            auto json = jsonRes.unwrap();

            auto arrayRes = json.as<std::vector<matjson::Value>>();
            if (!arrayRes.isOk()) {
                log::error("PML Error: Root JSON element is not a standard array structure!");
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
            
            log::info("PML: Loaded {} valid levels from list JSON", levels.size());
            if (levels.empty()) {
                log::error("PML Error: No valid levels resolved from parsed array elements.");
                return failure(-3);
            }

            success();
        }
    );
}

// Add PML list-search buttons to the Creator Layer
class $modify(MyCreatorLayer, CreatorLayer) {
    struct Fields {
        TaskHolder<web::WebResponse> m_listener;
    };

    bool init() {
        if (!CreatorLayer::init()) return false;

        auto creatorMenu = this->getChildByID("creator-buttons-menu");
        if (!creatorMenu) return true;

        // 1. Practice Mode List button (Using folder list icon)
        auto spritePML = CCSprite::createWithSpriteFrameName("GJ_viewListsBtn_001.png");
        auto buttonPML = CCMenuItemSpriteExtra::create(
            spritePML,
            this,
            menu_selector(MyCreatorLayer::onPMLClick)
        );
        buttonPML->setID("PML-button");

        // 2. Open Verifications list button (Using prestigious trophy icon!)
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
        log::info("PML: PML list button clicked.");
        PracticeModeList::currentListUrl = "https://raw.githubusercontent.com/AncepsGD/practice-mode-list/refs/heads/main/levels.json";
        this->startPmlLoadingSequence();
    }

    void onVerificationsClick(CCObject* sender) {
        log::info("PML: Verifications list button clicked.");
        PracticeModeList::currentListUrl = "https://raw.githubusercontent.com/AncepsGD/practice-mode-list/refs/heads/main/verifications.json";
        this->startPmlLoadingSequence();
    }

    void startPmlLoadingSequence() {
        auto loading = LoadingCircle::create();
        loading->setParent(CCDirector::sharedDirector()->getRunningScene());
        loading->show();

        PracticeModeList::loadPracticeList(
            m_fields->m_listener,
            [this, loading]() {
                loading->fadeAndRemove();
                log::info("PML: List loaded successfully from GitHub.");

                if (PracticeModeList::levels.empty()) {
                    FLAlertLayer::create("Error", "No levels were detected in the source JSON.", "OK")->show();
                    return;
                }

                this->launchLevelBrowser();
            },
            [loading](int errorCode) {
                loading->fadeAndRemove();
                log::error("PML Error: Unable to fetch list. Parse code: {}", errorCode);
                FLAlertLayer::create(
                    "Error",
                    fmt::format("Failed to download or parse the selected list.\nCode: {}", errorCode),
                    "OK"
                )->show();
            }
        );
    }

    void launchLevelBrowser() {
        std::string searchQuery = "";
        for (size_t idx = 0; idx < PracticeModeList::levels.size(); ++idx) {
            searchQuery += std::to_string(PracticeModeList::levels[idx].id);
            if (idx < PracticeModeList::levels.size() - 1) {
                searchQuery += ",";
            }
        }

        log::info("PML: Assembled launch search object (Type 19) for browser creation.");
        auto searchObj = GJSearchObject::create(static_cast<SearchType>(19), searchQuery);

        auto browserLayer = LevelBrowserLayer::create(searchObj);
        auto scene = CCScene::create();
        scene->addChild(browserLayer);
        CCDirector::sharedDirector()->replaceScene(
            CCTransitionFade::create(0.5f, scene)
        );
    }
};