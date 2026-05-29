#include "main.hpp"
#include <Geode/Geode.hpp>
#include <Geode/utils/web.hpp>
#include <Geode/ui/TextInput.hpp>
#include <matjson.hpp>
#include <string>
#include <vector>
#include <algorithm>
#include <cctype>
#include <thread>
#include <Geode/modify/CreatorLayer.hpp>

std::vector<PML> PracticeModeList::levels;
bool PracticeModeList::levelsLoaded = false;
std::string PracticeModeList::ListUrl = "https://raw.githubusercontent.com/AncepsGD/practice-mode-list/refs/heads/main/levels.json";
using namespace geode::prelude;

static std::string toLower(const std::string &s)
{
    std::string lower = s;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c)
                   { return std::tolower(c); });
    return lower;
}

static std::vector<PML> parsePMLJson(const std::string &data)
{
    std::vector<PML> out;
    auto parsed = matjson::parse(data);
    if (!parsed)
        return out;
    auto arrOpt = parsed.unwrap().asArray();
    if (!arrOpt)
        return out;
    auto arr = arrOpt.unwrap();
    out.reserve(arr.size());

    for (auto const &item : arr)
    {
        PML p;

        if (item.contains("id"))
        {
            if (item["id"].isNumber())
                p.id = item["id"].asInt().unwrapOr(0);
            else
                p.id = geode::utils::numFromString<int>(item["id"].asString().unwrapOr("0")).unwrapOr(0);
        }

        if (item.contains("rank"))
        {
            if (item["rank"].isNumber())
                p.rank = item["rank"].asInt().unwrapOr(0);
            else
                p.rank = geode::utils::numFromString<int>(item["rank"].asString().unwrapOr("0")).unwrapOr(0);
        }

        if (item.contains("name"))
            p.name = item["name"].asString().unwrapOr("");

        if (p.id > 0)
            out.push_back(p);
    }
    return out;
}

class PMLInputDelegate : public TextInputDelegate
{
    TextInput *m_otherInput;
    CCObject *m_target;
    SEL_MenuHandler m_selector;

public:
    PMLInputDelegate(TextInput *other, CCObject *target, SEL_MenuHandler selector)
        : m_otherInput(other), m_target(target), m_selector(selector) {}

    void textChanged(CCTextInputNode *input) override
    {
        std::string s = input->getString();
        if (!s.empty())
            m_otherInput->setString("");
    }
    void textInputClosed(CCTextInputNode *) override
    {
        if (m_target && m_selector)
            (m_target->*m_selector)(nullptr);
    }
};

class PMLResultPopup : public FLAlertLayer
{
    std::vector<PML> m_results;

    bool init(const std::vector<PML> &results)
    {
        if (!FLAlertLayer::init(nullptr, "Select a level", "", "Close", nullptr, 320.f, true, 300.f, 1.f))
            return false;

        m_results = results;

        float totalH = 45.f * results.size();

        CCScrollView *scroll = CCScrollView::create(CCSize(280.f, 230.f));
        scroll->setPosition(ccp(20.f, 50.f));
        m_mainLayer->addChild(scroll);

        CCMenu *menu = CCMenu::create();
        menu->setContentSize(CCSize(280.f, totalH));
        menu->setPosition(ccp(0.f, 0.f));

        for (size_t i = 0; i < results.size(); ++i)
        {
            auto btn = CCMenuItemSpriteExtra::create(
                ButtonSprite::create(results[i].name.c_str(), 260, true,
                                     "goldFont.fnt", "GJ_button_01.png", 20, 0.6f),
                this, menu_selector(PMLResultPopup::onSelect));
            btn->setTag(static_cast<int>(i));
            btn->setPosition(ccp(140.f, totalH - 45.f * i - 22.5f));
            menu->addChild(btn);
        }

        scroll->addChild(menu);
        scroll->setContentSize(CCSize(280.f, totalH));

        return true;
    }

    void onSelect(CCObject *sender)
    {
        int idx = sender->getTag();
        if (idx >= 0 && idx < static_cast<int>(m_results.size()))
        {
            int id = m_results[idx].id;
            auto obj = GJSearchObject::create(SearchType::Search, std::to_string(id));
            auto scene = LevelBrowserLayer::scene(obj);
            CCDirector::sharedDirector()->replaceScene(
                CCTransitionFade::create(0.5f, scene));
        }
        keyBackClicked();
    }

public:
    static PMLResultPopup *create(const std::vector<PML> &results)
    {
        auto ret = new PMLResultPopup();
        if (ret && ret->init(results))
        {
            ret->autorelease();
            return ret;
        }
        CC_SAFE_DELETE(ret);
        return nullptr;
    }
};

class $modify(MyCreatorLayer, CreatorLayer)
{
    struct Fields
    {
        CCMenuItemSpriteExtra *searchBtn = nullptr;
        CCLabelBMFont *loadingLabel = nullptr;
        TextInput *rankInput = nullptr;
        TextInput *nameInput = nullptr;
        std::string pendingName;
        std::string pendingRank;
    };

    bool init()
    {
        if (!CreatorLayer::init())
            return false;

        auto menu = this->getChildByID("creator-buttons-menu");
        if (!menu)
            return true;

        auto sprite = CCSprite::create("practicecomplete.png"_spr);
        if (!sprite)
            return true;
        sprite->setScale(0.25f);

        m_fields->searchBtn = CCMenuItemSpriteExtra::create(
            sprite, this, menu_selector(MyCreatorLayer::onPMLClick));
        m_fields->searchBtn->setID("pml-action-button");
        menu->addChild(m_fields->searchBtn);

        m_fields->rankInput = TextInput::create(60.f, "Rank", "bigFont.fnt");
        m_fields->rankInput->setID("pml-rank-input");
        m_fields->rankInput->setScale(0.5f);
        m_fields->rankInput->getInputNode()->setAllowedChars("0123456789");
        menu->addChild(m_fields->rankInput);

        m_fields->nameInput = TextInput::create(80.f, "Name", "bigFont.fnt");
        m_fields->nameInput->setID("pml-name-input");
        m_fields->nameInput->setScale(0.5f);
        menu->addChild(m_fields->nameInput);

        menu->updateLayout();

        m_fields->rankInput->setDelegate(
            new PMLInputDelegate(m_fields->nameInput, this,
                                 menu_selector(MyCreatorLayer::onPMLClick)));
        m_fields->nameInput->setDelegate(
            new PMLInputDelegate(m_fields->rankInput, this,
                                 menu_selector(MyCreatorLayer::onPMLClick)));

        return true;
    }

    void onPMLClick(CCObject *)
    {
        std::string r = m_fields->rankInput->getString();
        std::string n = m_fields->nameInput->getString();

        if (r.empty() && n.empty())
        {
            Notification::create("Enter rank or name", NotificationIcon::Warning)->show();
            return;
        }

        if (PracticeModeList::levelsLoaded)
        {
            performSearch(n, r);
            return;
        }

        std::string url = PracticeModeList::ListUrl;
        if (url.empty())
        {
            Notification::create("List URL not set", NotificationIcon::Error)->show();
            return;
        }

        showLoading(true);
        m_fields->pendingName = n;
        m_fields->pendingRank = r;

        std::thread([this, url]()
                    {
            auto req = web::WebRequest();
            auto res = req.getSync(url);

            bool ok = res.ok();
            std::string data = ok ? res.string().unwrapOr("") : "";

            Loader::get()->queueInMainThread([this, ok, data]() {
                showLoading(false);
                if (!ok)
                {
                    Notification::create("Request failed", NotificationIcon::Error)->show();
                    return;
                }
                auto levels = parsePMLJson(data);
                if (levels.empty())
                {
                    Notification::create("No data", NotificationIcon::Error)->show();
                    return;
                }
                PracticeModeList::levels = levels;
                PracticeModeList::levelsLoaded = true;
                performSearch(m_fields->pendingName, m_fields->pendingRank);
            }); })
            .detach();
    }

    void performSearch(const std::string &nameQuery, const std::string &rankQuery)
    {
        auto &levels = PracticeModeList::levels;

        if (!nameQuery.empty())
        {
            std::string q = toLower(nameQuery);
            std::vector<PML> matches;
            for (const auto &lvl : levels)
            {
                if (toLower(lvl.name).find(q) != std::string::npos)
                    matches.push_back(lvl);
            }

            if (matches.empty())
            {
                Notification::create("No match", NotificationIcon::Error)->show();
            }
            else if (matches.size() == 1)
            {
                openLevel(matches[0].id);
            }
            else
            {
                PMLResultPopup::create(matches)->show();
            }
        }
        else
        {
            int rankNum = 0;
            try
            {
                rankNum = std::stoi(rankQuery);
            }
            catch (...)
            {
                Notification::create("Invalid rank", NotificationIcon::Error)->show();
                return;
            }

            int index = rankNum - 1;
            if (index < 0 || index >= static_cast<int>(levels.size()))
            {
                Notification::create(
                    "Rank out of range (1 - " + std::to_string(levels.size()) + ")",
                    NotificationIcon::Error)
                    ->show();
                return;
            }
            openLevel(levels[index].id);
        }
    }

    void openLevel(int id)
    {
        auto obj = GJSearchObject::create(SearchType::Search, std::to_string(id));
        auto scene = LevelBrowserLayer::scene(obj);
        CCDirector::sharedDirector()->replaceScene(
            CCTransitionFade::create(0.5f, scene));
    }

    void showLoading(bool show)
    {
        if (show)
        {
            if (!m_fields->loadingLabel)
            {
                m_fields->loadingLabel = CCLabelBMFont::create("Loading...", "bigFont.fnt");
                m_fields->loadingLabel->setPosition(m_fields->searchBtn->getPosition());
                m_fields->loadingLabel->setScale(0.5f);
                m_fields->searchBtn->getParent()->addChild(m_fields->loadingLabel);
            }
            m_fields->loadingLabel->setVisible(true);
            m_fields->searchBtn->setEnabled(false);
        }
        else
        {
            if (m_fields->loadingLabel)
                m_fields->loadingLabel->setVisible(false);
            m_fields->searchBtn->setEnabled(true);
        }
    }
};
