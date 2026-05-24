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

// Rob's servers have rate-limiting, so save them in memory to avoid querying the same level repeatedly
static std::map<int, GJGameLevel*> pmlCache;

// Stagger web requests so the servers aren't repeatedly spammed and give a temp-ban
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

// SearchType(19) **may** filter out unlisted levels on Rubrub's side
// Load the batch of public levels first, then find what got skipped, and then use fallback single-ID queries to fill in the rest
struct PMLState {
    bool loading_active = false;
    bool bypass_render_hook = false; // Flag to prevent setupLevelBrowser recursion loops
    int current_page = 0;
    std::vector<int> expected_ids;
    std::vector<int> unlisted_ids;
    size_t current_step = 0;
    SearchType cache_type = static_cast<SearchType>(19);
    std::string cache_query = "";
    std::map<int, GJGameLevel*> loaded_levels;
};
static PMLState pmlState;

// Standard routine when switching pages or exiting view
void resetPML() {
    pmlState.loading_active = false;
    pmlState.bypass_render_hook = false;
    pmlState.current_page = 0;
    pmlState.expected_ids.clear();
    pmlState.unlisted_ids.clear();
    pmlState.current_step = 0;
    pmlState.loaded_levels.clear();
}

// Staggered download loop that resolves unlisted levels one by one
void NextUnlisted(GJSearchObject* search) {
    if (!pmlState.loading_active) return;

    // After all levels are resolved, put list together in correct order and send that to UI
    if (pmlState.current_step >= pmlState.unlisted_ids.size()) {
        pmlState.loading_active = false;
        pmlState.bypass_render_hook = true;

        search->m_searchType = pmlState.cache_type;
        search->m_searchQuery = pmlState.cache_query;

        auto final_array = cocos2d::CCArray::create();
        for (int id : pmlState.expected_ids) {
            if (pmlState.loaded_levels.count(id)) {
                final_array->addObject(pmlState.loaded_levels[id]);
            }
        }

        auto active_scene = CCDirector::sharedDirector()->getRunningScene();
        if (active_scene) {
            auto browser_ui = active_scene->getChildByType<LevelBrowserLayer>(0);
            if (browser_ui && browser_ui->m_searchObject == search) {
                browser_ui->setupLevelBrowser(final_array);
            }
        }
        return;
    }

    int levelId = pmlState.unlisted_ids[pmlState.current_step];

    // Read directly from cache to save bandwidth 'cause it's already there
    if (pmlCache.count(levelId)) {
        pmlState.loaded_levels[levelId] = pmlCache[levelId];
        pmlState.current_step++;
        NextUnlisted(search);
        return;
    }

    search->m_searchType = SearchType::Search;
    search->m_searchQuery = std::to_string(levelId);

    // Keep staggering by 600ms to remain  under Cloudflare rate thresholds
    PMLDelayHandler::sched_delay(0.6f, [search]() {
        if (pmlState.loading_active) {
            GameLevelManager::sharedState()->getOnlineLevels(search);
        }
    });
}

class $modify(MyGameLevelManager, GameLevelManager) {
    void getOnlineLevels(GJSearchObject* searchObj) {
        if (searchObj && searchObj->m_searchType == static_cast<SearchType>(19) && !pmlState.loading_active) {
            pmlState.loaded_levels.clear();
            pmlState.expected_ids.clear();
            pmlState.unlisted_ids.clear();
            pmlState.current_step = 0;

            int page_offset = searchObj->m_page * 10;
            int end_offset = std::min(page_offset + 10, static_cast<int>(PracticeModeList::levels.size()));
            for (int i = page_offset; i < end_offset; ++i) {
                pmlState.expected_ids.push_back(PracticeModeList::levels[i].id);
            }
        }
        GameLevelManager::getOnlineLevels(searchObj);
    }
};

class $modify(MyLevelBrowserLayer, LevelBrowserLayer) {
    bool init(GJSearchObject* searchObj) {
        resetPML();

        if (!LevelBrowserLayer::init(searchObj)) return false;

        if (searchObj && searchObj->m_searchType == static_cast<SearchType>(19)) {
            this->m_itemCount = PracticeModeList::levels.size();
        }
        return true;
    }

    void setupLevelBrowser(cocos2d::CCArray* levels) {
        if (pmlState.bypass_render_hook) {
            pmlState.bypass_render_hook = false;
            LevelBrowserLayer::setupLevelBrowser(levels);
            return;
        }

        if (m_searchObject && m_searchObject->m_searchType == static_cast<SearchType>(19) && !pmlState.loading_active) {
            if (levels) {
                for (int i = 0; i < levels->count(); ++i) {
                    auto lvl = static_cast<GJGameLevel*>(levels->objectAtIndex(i));
                    if (lvl) {
                        pmlState.loaded_levels[lvl->m_levelID] = lvl;
                        if (pmlCache.count(lvl->m_levelID) == 0) {
                            lvl->retain();
                            pmlCache[lvl->m_levelID] = lvl;
                        }
                    }
                }
            }

            pmlState.unlisted_ids.clear();
            for (int id : pmlState.expected_ids) {
                if (pmlState.loaded_levels.count(id) == 0) {
                    if (pmlCache.count(id)) {
                        pmlState.loaded_levels[id] = pmlCache[id];
                    } else {
                        pmlState.unlisted_ids.push_back(id);
                    }
                }
            }

            // Stagger unlisted levels if any expected IDs are missing from the response
            if (!pmlState.unlisted_ids.empty()) {
                pmlState.loading_active = true;
                pmlState.cache_type = m_searchObject->m_searchType;
                pmlState.cache_query = m_searchObject->m_searchQuery;
                pmlState.current_step = 0;

                NextUnlisted(m_searchObject);
                return;
            } else {
                auto completed_list = cocos2d::CCArray::create();
                for (int id : pmlState.expected_ids) {
                    if (pmlState.loaded_levels.count(id)) {
                        completed_list->addObject(pmlState.loaded_levels[id]);
                    }
                }
                LevelBrowserLayer::setupLevelBrowser(completed_list);
                return;
            }
        }

        // Catch incoming responses
        if (pmlState.loading_active && m_searchObject) {
            if (levels && levels->count() > 0) {
                auto lvl = static_cast<GJGameLevel*>(levels->objectAtIndex(0));
                if (lvl) {
                    pmlState.loaded_levels[lvl->m_levelID] = lvl;
                    if (pmlCache.count(lvl->m_levelID) == 0) {
                        lvl->retain();
                        pmlCache[lvl->m_levelID] = lvl;
                    }
                }
            } else {
                // if level is **maybe** deleted from the server
                int failedId = pmlState.unlisted_ids[pmlState.current_step];
                log::warn("PML query failure for ID {}", failedId);

                Notification::create(
                    fmt::format("Failed to load level ID: {}", failedId),
                    NotificationIcon::Warning
                )->show();
            }

            pmlState.current_step++;
            NextUnlisted(m_searchObject);
            return;
        }

        LevelBrowserLayer::setupLevelBrowser(levels);
    }
// if.... loadLevels failed
    void loadLevelsFailed(char const* p0, int p1) {
        if (pmlState.loading_active) {
            int failedId = pmlState.unlisted_ids[pmlState.current_step];
            log::warn("PML network/server failed on ID {}", failedId);

            Notification::create(
                fmt::format("Failed to load level ID: {}", failedId),
                NotificationIcon::Warning
            )->show();

            pmlState.current_step++;
            NextUnlisted(m_searchObject);
            return;
        }
        LevelBrowserLayer::loadLevelsFailed(p0, p1);
    }
};

void PracticeModeList::loadPracticeList(
    TaskHolder<web::WebResponse>& listener,
    Function<void()> success,
    CopyableFunction<void(int)> failure
) {
    listener.spawn(
        // fetch JSON data from PML
        web::WebRequest().get("https://raw.githubusercontent.com/AncepsGD/practice-mode-list/refs/heads/main/levels.json"),
        [failure = std::move(failure), success = std::move(success)](web::WebResponse answer) mutable {
            if (!answer.ok()) return failure(answer.code());
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
                        if (parsed.isOk()) {
                            rankValue = parsed.unwrap();
                        }
                    }
                }
                auto idRes = level.get<int>("id");
                if (idRes.isOk()) {
                    idValue = idRes.unwrap();
                } else {
                    auto idStrRes = level.get<std::string>("id");
                    if (idStrRes.isOk()) {
                        auto parsed = geode::utils::numFromString<int>(idStrRes.unwrap());
                        if (parsed.isOk()) {
                            idValue = parsed.unwrap();
                        }
                    }
                }
                if (rankValue > 0 && idValue > 0) {
                    PML demon{ idValue, rankValue};

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
// create PML button
class $modify(MyCreatorLayer, CreatorLayer) {
    struct Fields {
        TaskHolder<web::WebResponse> m_listener;
    };

    bool init() {
        if (!CreatorLayer::init()) return false;

        auto creatorMenu = this->getChildByID("creator-buttons-menu");
        if (!creatorMenu) return true;

        auto sprite = CCSprite::createWithSpriteFrameName("GJ_viewListsBtn_001.png");

        auto button = CCMenuItemSpriteExtra::create(
            sprite,
            this,
            menu_selector(MyCreatorLayer::onPMLClick)
        );
        button->setID("PML-button");

        creatorMenu->addChild(button);
        creatorMenu->updateLayout();

        return true;
    }
    void onPMLClick(CCObject* sender) {
        auto loading = LoadingCircle::create();
        loading->setParent(CCDirector::sharedDirector()->getRunningScene());
        loading->show();

        resetPML();

        PracticeModeList::loadPracticeList(
            m_fields->m_listener,
            [loading]() {
                loading->fadeAndRemove();

                if (PracticeModeList::levels.empty()) {
                    FLAlertLayer::create("Error", "No levels were detected in PML json.", "OK")->show();
                    return;
                }

                std::string searchQuery = "";
                for (size_t idx = 0; idx < PracticeModeList::levels.size(); ++idx) {
                    searchQuery += std::to_string(PracticeModeList::levels[idx].id);
                    if (idx < PracticeModeList::levels.size() - 1) {
                        searchQuery += ",";
                    }
                }

                auto searchObj = GJSearchObject::create(static_cast<SearchType>(19), searchQuery);

                auto browserLayer = LevelBrowserLayer::create(searchObj);
                auto scene = CCScene::create();
                scene->addChild(browserLayer);
                CCDirector::sharedDirector()->replaceScene(
                    CCTransitionFade::create(0.5f, scene)
                );
            },
            [loading](int errorCode) {
                loading->fadeAndRemove();
                FLAlertLayer::create(
                    "Error",
                    fmt::format("Failed to download or parse PML json.\nCode: {}", errorCode),
                    "OK"
                )->show();
            }
        );
    }
};