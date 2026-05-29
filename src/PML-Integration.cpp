#include <Geode/Geode.hpp>
#include <Geode/utils/web.hpp>
#include <matjson.hpp>
#include <Geode/loader/Event.hpp>
#include <Geode/modify/CreatorLayer.hpp>

using namespace geode::prelude;

class $modify(MyCreatorLayer, CreatorLayer) {
    struct Fields {
        TaskHolder<web::WebResponse> m_listener;
        std::string m_lastRankText = "";
        std::string m_lastNameText = "";
    };

    bool init() {
        if (!CreatorLayer::init()) return false;

        auto creatorMenu = this->getChildByID("creator-buttons-menu");
        if (creatorMenu) {
            auto customImage = CCSprite::create("practicecomplete.png"_spr);
            if (customImage) {
                customImage->setScale(0.4f);
                auto buttonNode = CCMenuItemSpriteExtra::create(
                    customImage,
                    this,
                    menu_selector(MyCreatorLayer::onPMLClick)
                );
                buttonNode->setID("pml-action-button");
                creatorMenu->addChild(buttonNode);
                creatorMenu->updateLayout();

                auto inputNode = TextInput::create(70.0f, "Rank", "bigFont.fnt");
                if (inputNode) {
                    inputNode->getInputNode()->setAllowedChars("0123456789");
                    inputNode->setTag(42069);
                    inputNode->setID("pml-number-input");
                    inputNode->setScale(0.7f);
                    
                    creatorMenu->addChild(inputNode);
                    creatorMenu->updateLayout();
                }

                auto nameInput = TextInput::create(100.0f, "Name", "bigFont.fnt");
                if (nameInput) {
                    nameInput->setTag(42070);
                    nameInput->setID("pml-name-input");
                    nameInput->setScale(0.7f);

                    creatorMenu->addChild(nameInput);
                    creatorMenu->updateLayout();
                }

                this->schedule(schedule_selector(MyCreatorLayer::updateInputFields));
            } else {
                log::error("Could not find practicecomplete.png.");
            }
        }
        return true;
    }

    void updateInputFields(float dt) {
        auto creatorMenu = this->getChildByID("creator-buttons-menu");
        if (!creatorMenu) return;

        auto numField = static_cast<TextInput*>(creatorMenu->getChildByID("pml-number-input"));
        auto nameField = static_cast<TextInput*>(creatorMenu->getChildByID("pml-name-input"));
        if (!numField || !nameField) return;

        std::string currentRank = numField->getString();
        std::string currentName = nameField->getString();

        if (currentRank != m_fields->m_lastRankText && !currentRank.empty()) {
            nameField->setString("");
            currentName = "";
        }

        else if (currentName != m_fields->m_lastNameText && !currentName.empty()) {
            numField->setString("");
            currentRank = "";
        }

        m_fields->m_lastRankText = currentRank;
        m_fields->m_lastNameText = currentName;
    }

    void onPMLClick(CCObject* sender) {
        auto creatorMenu = this->getChildByID("creator-buttons-menu");
        if (!creatorMenu) return;

        auto numField = static_cast<TextInput*>(creatorMenu->getChildByID("pml-number-input"));
        auto nameField = static_cast<TextInput*>(creatorMenu->getChildByID("pml-name-input"));
        if (!numField || !nameField) return;

        std::string numStr = numField->getString();
        std::string nameStr = nameField->getString();

        if (!numStr.empty()) {
            int userIndex = geode::utils::numFromString<int>(numStr).unwrapOr(1);
            if (userIndex <= 0) {
                Notification::create("Rank must be 1 or higher", NotificationIcon::Error)->show();
                return;
            }
            this->fetchLevelFromJSON(userIndex, "");
        } 
        else if (!nameStr.empty()) {
            this->fetchLevelFromJSON(0, nameStr);
        } 
        else {
            Notification::create("Please enter a Rank or Name", NotificationIcon::Warning)->show();
        }
    }

    void fetchLevelFromJSON(int targetIndex, std::string const& targetName) {
        log::info("Fetching level data...");

        auto req = web::WebRequest();
        auto future = req.get("https://raw.githubusercontent.com/AncepsGD/practice-mode-list/refs/heads/main/levels.json");

        m_fields->m_listener.spawn(
            "Fetching Awesome Data",
            std::move(future),
            [targetIndex, targetName](web::WebResponse response) mutable { 
                if (!response.ok()) {
                    log::error("Request Failed || Status code: {}", response.code());
                    Notification::create("Request Failed", NotificationIcon::Error)->show();
                    return;
                }

                std::string data = response.string().unwrapOr("");
                if (data.empty()) {
                    log::error("No Data");
                    Notification::create("No JSON Data Found", NotificationIcon::Error)->show();
                    return;
                }

                auto jsonParse = matjson::parse(data);
                if (!jsonParse) {
                    log::error("JSON failed to parse");
                    Notification::create("JSON failed to parse", NotificationIcon::Error)->show();
                    return;
                }

                auto json = jsonParse.unwrap();
                if (!json.isArray()) {
                    log::error("JSON is not an array");
                    Notification::create("JSON is not an array", NotificationIcon::Error)->show();
                    return;
                }

                auto jsonArrayRes = json.asArray();
                if (!jsonArrayRes) {
                    log::error("Could not get JSON Array Result");
                    Notification::create("Could not get JSON Array Result", NotificationIcon::Error)->show();
                    return;
                }

                auto jsonArray = jsonArrayRes.unwrap();
                if (jsonArray.empty()) {
                    log::error("empty JSON");
                    Notification::create("empty JSON", NotificationIcon::Error)->show();
                    return;
                }

                if (targetIndex <= 0 && !targetName.empty()) {
                    std::string lowerQuery = targetName;
                    std::transform(lowerQuery.begin(), lowerQuery.end(), lowerQuery.begin(), ::tolower);

                    bool nameMatched = false;
                    for (size_t i = 0; i < jsonArray.size(); ++i) {
                        if (jsonArray[i].contains("name")) {
                            std::string currentName = jsonArray[i]["name"].asString().unwrapOr("");
                            std::string lowerName = currentName;
                            std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);

                            if (lowerName.find(lowerQuery) != std::string::npos) {
                                targetIndex = static_cast<int>(i + 1);
                                nameMatched = true;
                                break;
                            }
                        }
                    }

                    if (!nameMatched) {
                        Notification::create("No level matched that name", NotificationIcon::Error)->show();
                        return;
                    }
                }

                size_t vectorIndex = static_cast<size_t>(targetIndex - 1);

                if (vectorIndex >= jsonArray.size()) {
                    log::error("Index out of bounds. Max size: {}", jsonArray.size());
                    std::string errMsg = "Max index is " + std::to_string(jsonArray.size());
                    Notification::create(errMsg.c_str(), NotificationIcon::Error)->show();
                    return;
                }

                auto targetElement = jsonArray[vectorIndex];
                int levelID = 0;

                if (targetElement.contains("id")) {
                    auto idObj = targetElement["id"];
                    if (idObj.isNumber()) {
                        levelID = idObj.asInt().unwrapOr(0);
                    } else if (idObj.isString()) {
                        std::string idStr = idObj.asString().unwrapOr("0");
                        levelID = geode::utils::numFromString<int>(idStr).unwrapOr(0);
                    }
                } else {
                    log::error("Could not find ID");
                    Notification::create("Could not find ID", NotificationIcon::Error)->show();
                    return;
                }

                if (levelID <= 0) {
                    log::error("Impossible ID: {}", levelID);
                    Notification::create("Impossible ID", NotificationIcon::Error)->show();
                    return;
                }

                log::info("Successful ID: {}", levelID);

                auto searchPage = GJSearchObject::create(SearchType::Search, std::to_string(levelID));
                auto scene = LevelBrowserLayer::scene(searchPage);
                CCDirector::sharedDirector()->replaceScene(CCTransitionFade::create(0.5f, scene));
            }
        );
    }
};
