#include "TrainerPopup.hpp"

#include <Geode/binding/PauseLayer.hpp>
#include <algorithm>

using namespace geode::prelude;

namespace {
    CCMenuItemSpriteExtra* makeButton(char const* text, CCObject* target, SEL_MenuHandler cb, char const* bg = "GJ_button_01.png") {
        auto spr = ButtonSprite::create(text, "goldFont.fnt", bg, .8f);
        spr->setScale(.75f);
        return CCMenuItemSpriteExtra::create(spr, target, cb);
    }

    std::string lower(std::string s) {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
        return s;
    }
}

std::vector<std::filesystem::path> macroSearchDirs() {
    std::vector<std::filesystem::path> dirs;
    dirs.push_back(Mod::get()->getSaveDir() / "imports");
    if (auto eclipse = Loader::get()->getInstalledMod("eclipse.eclipse-menu")) {
        dirs.push_back(eclipse->getSaveDir() / "replays");
    }
    if (auto pathfinder = Loader::get()->getInstalledMod("camila314.pathfinder")) {
        dirs.push_back(pathfinder->getSaveDir());
    }
    dirs.push_back(dirs::getGameDir() / "replays");
    return dirs;
}

void restartFromStart() {
    // Deferred: this is usually called from a button inside one of the popups we close.
    Loader::get()->queueInMainThread([] {
        // Close any of our popups first
        auto scene = CCScene::get();
        while (auto popup = scene->getChildByType<ImportPopup>(0)) popup->removeFromParent();
        while (auto popup = scene->getChildByType<TrainerPopup>(0)) popup->removeFromParent();

        if (auto pause = scene->getChildByType<PauseLayer>(0)) {
            auto pl = PlayLayer::get();
            if (pl && pl->m_isPracticeMode) pause->onRestartFull(nullptr);
            else pause->onRestart(nullptr);
        }
        else if (auto pl = PlayLayer::get()) {
            pl->resetLevelFromStart();
        }
    });
}

void startBotDemo() {
    auto& t = Trainer::get();
    if (!t.hasChart) return;
    t.armRecording = t.recording = false;
    t.armCapture = true;
    // The demo has to play from the start without checkpoints.
    if (auto pl = PlayLayer::get(); pl && pl->m_isPracticeMode) pl->togglePracticeMode(false);
    restartFromStart();
}

// ---------------------------------------------------------------- TrainerPopup

TrainerPopup* TrainerPopup::create(GJGameLevel* level) {
    auto ret = new TrainerPopup();
    if (ret->init(level)) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool TrainerPopup::init(GJGameLevel* level) {
    if (!Popup::init(370.f, 280.f)) return false;
    m_level = level;
    this->setTitle("Click Trainer");

    m_status = CCLabelBMFont::create("", "chatFont.fnt");
    m_status->setScale(.7f);
    m_status->setAlignment(kCCTextAlignmentCenter);
    m_mainLayer->addChildAtPosition(m_status, Anchor::Center, {0.f, 70.f});

    m_recordBtn = makeButton("Record my run", this, menu_selector(TrainerPopup::onRecord));
    m_buttonMenu->addChildAtPosition(m_recordBtn, Anchor::Center, {-82.f, 16.f});

    auto importBtn = makeButton("Import macro", this, menu_selector(TrainerPopup::onImport));
    m_buttonMenu->addChildAtPosition(importBtn, Anchor::Center, {82.f, 16.f});

    m_mapBtn = makeButton("Map onto level", this, menu_selector(TrainerPopup::onMap), "GJ_button_02.png");
    m_buttonMenu->addChildAtPosition(m_mapBtn, Anchor::Center, {-82.f, -18.f});

    m_toggleBtn = makeButton("Trainer: ON", this, menu_selector(TrainerPopup::onToggle), "GJ_button_01.png");
    m_buttonMenu->addChildAtPosition(m_toggleBtn, Anchor::Center, {82.f, -18.f});

    m_styleBtn = makeButton("View: Rings", this, menu_selector(TrainerPopup::onStyle), "GJ_button_02.png");
    m_buttonMenu->addChildAtPosition(m_styleBtn, Anchor::Center, {-82.f, -52.f});

    auto calibrateBtn = makeButton("Calibrate", this, menu_selector(TrainerPopup::onCalibrate), "GJ_button_05.png");
    m_buttonMenu->addChildAtPosition(calibrateBtn, Anchor::Center, {82.f, -52.f});

    m_deleteBtn = makeButton("Delete chart", this, menu_selector(TrainerPopup::onDelete), "GJ_button_06.png");
    m_buttonMenu->addChildAtPosition(m_deleteBtn, Anchor::Bottom, {-118.f, 30.f});

    auto sectionsBtn = makeButton("Sections", this, menu_selector(TrainerPopup::onSections), "GJ_button_02.png");
    m_buttonMenu->addChildAtPosition(sectionsBtn, Anchor::Bottom, {0.f, 30.f});

    // Settings, right here in the pause menu
    auto gearSpr = CCSprite::createWithSpriteFrameName("GJ_optionsBtn_001.png");
    gearSpr->setScale(.55f);
    auto gearBtn = CCMenuItemSpriteExtra::create(gearSpr, this, menu_selector(TrainerPopup::onSettings));
    m_buttonMenu->addChildAtPosition(gearBtn, Anchor::TopRight, {-24.f, -24.f});

    auto folderBtn = makeButton("Folder", this, menu_selector(TrainerPopup::onOpenFolder), "GJ_button_05.png");
    m_buttonMenu->addChildAtPosition(folderBtn, Anchor::Bottom, {118.f, 30.f});

    this->refresh();
    return true;
}

void TrainerPopup::refresh() {
    auto& t = Trainer::get();
    std::string status;
    if (t.hasChart) {
        int taps = 0, holds = 0;
        for (auto const& n : t.chart.notes) (n.isHold ? holds : taps)++;
        status = fmt::format("Chart: {} clicks ({} taps, {} holds)\nFrom: {}", t.chart.notes.size(), taps, holds, t.chart.source);
        if (t.path.empty()) status += "\nNot mapped onto the level yet: press Map onto level";
        status += t.sectionsOnly
            ? fmt::format("\nGuide shown in {} section{}", t.sections.size(), t.sections.size() == 1 ? "" : "s")
            : std::string("\nGuide shown on the whole level");
    }
    else {
        status = "No chart for this level yet.\nRecord a run or import a bot macro.";
    }
    if (t.recording) status += "\n\nRecording... reach the end to save (practice + checkpoints is fine).";
    else if (t.armRecording) status += "\n\nRecording starts when you restart.";
    else if (!t.hasChart) status += "\nTip: record in practice mode with checkpoints, only your final route is kept.";
    m_status->setString(status.c_str());

    bool recordingActive = t.recording || t.armRecording;
    auto spr = static_cast<ButtonSprite*>(m_recordBtn->getNormalImage());
    spr->setString(recordingActive ? "Stop recording" : "Record my run");
    m_recordBtn->updateSprite();

    m_deleteBtn->setEnabled(t.hasChart);
    static_cast<ButtonSprite*>(m_deleteBtn->getNormalImage())->setOpacity(t.hasChart ? 255 : 100);
    m_mapBtn->setEnabled(t.hasChart);
    static_cast<ButtonSprite*>(m_mapBtn->getNormalImage())->setOpacity(t.hasChart ? 255 : 100);

    auto styleSpr = static_cast<ButtonSprite*>(m_styleBtn->getNormalImage());
    char const* styleName = t.settings.style == DisplayStyle::ClickTrack ? "View: Track"
        : t.settings.style == DisplayStyle::SideLane ? "View: Lane"
        : t.settings.style == DisplayStyle::TargetRings ? "View: Rings" : "View: Gates";
    styleSpr->setString(styleName);
    m_styleBtn->updateSprite();

    auto toggleSpr = static_cast<ButtonSprite*>(m_toggleBtn->getNormalImage());
    toggleSpr->setString(t.settings.enabled ? "Trainer: ON" : "Trainer: OFF");
    toggleSpr->updateBGImage(t.settings.enabled ? "GJ_button_01.png" : "GJ_button_04.png");
    m_toggleBtn->updateSprite();
}

void TrainerPopup::onMap(CCObject*) {
    createQuickPopup(
        "Map onto level",
        "A <cb>bot</c> will play the level once using this chart, so every click can be shown <cg>in the level</c>.\n"
        "Watch it to learn the route. The run doesn't count.",
        "Cancel", "Start",
        [](auto, bool yes) {
            if (yes) startBotDemo();
        }
    );
}

void TrainerPopup::onSettings(CCObject*) {
    // Changes apply live (see the settings listener in main.cpp).
    openSettingsPopup(Mod::get(), false);
}

void TrainerPopup::onSections(CCObject*) {
    SectionsPopup::create()->show();
}

void TrainerPopup::onCalibrate(CCObject*) {
    auto& t = Trainer::get();
    if (t.errorCount < 5) {
        FLAlertLayer::create("Calibrate",
            "Play the level with the guide for a bit (at least 5 hits), then press this. "
            "It shifts the visuals by your average early/late so the ring closes when you actually click.",
            "OK")->show();
        return;
    }
    t.calibrate();
    this->refresh();
}

void TrainerPopup::onStyle(CCObject*) {
    Trainer::get().cycleStyle();
    this->refresh();
}

void TrainerPopup::onToggle(CCObject*) {
    Trainer::get().toggleEnabled();
    this->refresh();
}

void TrainerPopup::onRecord(CCObject*) {
    auto& t = Trainer::get();
    if (t.recording || t.armRecording) {
        t.recording = false;
        t.armRecording = false;
        t.recorded.clear();
        this->refresh();
        return;
    }

    t.armCapture = t.capturing = false;
    t.armRecording = true;
    // Restart from the very start so the recording covers the whole level.
    restartFromStart();
}

void TrainerPopup::onImport(CCObject*) {
    ImportPopup::create(m_level, this)->show();
}

void TrainerPopup::onDelete(CCObject*) {
    WeakRef<TrainerPopup> self = this;
    createQuickPopup(
        "Delete chart",
        "Delete the saved chart for this level?",
        "Cancel", "Delete",
        [self, level = m_level](auto, bool yes) {
            if (!yes) return;
            if (auto res = Trainer::get().deleteChart(level); !res) {
                Notification::create(res.unwrapErr(), NotificationIcon::Error)->show();
            }
            if (auto popup = self.lock()) popup->refresh();
        }
    );
}

void TrainerPopup::onOpenFolder(CCObject*) {
    auto dir = Mod::get()->getSaveDir() / "imports";
    (void)file::createDirectoryAll(dir);
    file::openFolder(dir);
}

// ---------------------------------------------------------------- ImportPopup

ImportPopup* ImportPopup::create(GJGameLevel* level, TrainerPopup* parent) {
    auto ret = new ImportPopup();
    if (ret->init(level, parent)) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool ImportPopup::init(GJGameLevel* level, TrainerPopup* parent) {
    if (!Popup::init(360.f, 260.f)) return false;
    m_level = level;
    m_parent = parent;
    this->setTitle("Import macro / copy a chart");

    // Collect macros
    struct Entry {
        std::filesystem::path path;
        bool matchesLevel;
        std::filesystem::file_time_type time;
        bool own = false;
        std::string label;
    };
    std::vector<Entry> entries;
    std::string levelName = lower(std::string(level->m_levelName));
    std::string levelId = std::to_string(level->m_levelID.value());
    for (auto const& dir : macroSearchDirs()) {
        std::error_code ec;
        if (!std::filesystem::is_directory(dir, ec)) continue;
        // Bots sometimes sort macros into subfolders, so look one level deep too.
        auto it = std::filesystem::recursive_directory_iterator(dir, std::filesystem::directory_options::skip_permission_denied, ec);
        for (; !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
            if (it.depth() >= 1) it.disable_recursion_pending();
            auto const& entry = *it;
            if (!entry.is_regular_file(ec)) continue;
            auto ext = lower(string::pathToString(entry.path().extension()));
            if (ext != ".gdr2" && ext != ".gdr" && ext != ".json") continue;
            // Skip mods' own config files living next to the macros.
            auto fname = lower(string::pathToString(entry.path().filename()));
            if (fname == "saved.json" || fname == "settings.json" || fname == "config.json") continue;
            auto name = lower(string::pathToString(entry.path().stem()));
            bool match = (!levelName.empty() && name.find(levelName) != std::string::npos)
                || (levelId != "0" && name.find(levelId) != std::string::npos);
            entries.push_back({entry.path(), match, entry.last_write_time(ec)});
        }
    }
    // Your own charts from other levels (e.g. Acu -> Acu SP): copied with their route.
    {
        std::error_code ec;
        auto ownKey = levelKey(level);
        if (std::filesystem::is_directory(chartsDir(), ec)) {
            for (auto const& entry : std::filesystem::directory_iterator(chartsDir(), ec)) {
                if (!entry.is_regular_file(ec) || entry.path().extension() != ".gdr2") continue;
                if (string::pathToString(entry.path().stem()) == ownKey) continue;
                auto name = Chart::levelNameOf(entry.path());
                if (name.empty()) name = string::pathToString(entry.path().stem());
                auto lname = lower(name);
                // "Acu" matches "Acu SP" and the other way round
                bool match = !levelName.empty() && !lname.empty()
                    && (levelName.find(lname) != std::string::npos || lname.find(levelName) != std::string::npos);
                entries.push_back({entry.path(), match, entry.last_write_time(ec), true, name + "  (your chart)"});
            }
        }
    }
    std::sort(entries.begin(), entries.end(), [](auto const& a, auto const& b) {
        if (a.matchesLevel != b.matchesLevel) return a.matchesLevel;
        if (a.own != b.own) return a.own;
        return a.time > b.time;
    });

    auto listSize = CCSize{320.f, 180.f};
    auto bg = CCLayerColor::create({0, 0, 0, 80}, listSize.width, listSize.height);
    bg->ignoreAnchorPointForPosition(false);
    m_mainLayer->addChildAtPosition(bg, Anchor::Center, {0.f, -12.f});

    auto scroll = ScrollLayer::create(listSize);
    scroll->ignoreAnchorPointForPosition(false);
    m_mainLayer->addChildAtPosition(scroll, Anchor::Center, {0.f, -12.f});

    if (entries.empty()) {
        auto label = CCLabelBMFont::create(
            "No macros found (.gdr2 / .json).\n\nMake one with Pathfinder or Eclipse,\nor drop files into the Folder\nfrom the previous menu.",
            "chatFont.fnt"
        );
        label->setAlignment(kCCTextAlignmentCenter);
        label->setScale(.7f);
        m_mainLayer->addChildAtPosition(label, Anchor::Center, {0.f, -12.f});
        return true;
    }

    float rowH = 28.f;
    float totalH = std::max(listSize.height, rowH * entries.size());
    scroll->m_contentLayer->setContentSize({listSize.width, totalH});

    for (size_t i = 0; i < entries.size(); i++) {
        m_files.push_back(entries[i].path);
        m_own.push_back(entries[i].own);
        float y = totalH - rowH * (i + .5f);

        auto name = entries[i].own ? entries[i].label : string::pathToString(entries[i].path.filename());
        if (name.size() > 36) name = name.substr(0, 33) + "...";
        auto label = CCLabelBMFont::create(name.c_str(), "chatFont.fnt");
        label->setScale(.65f);
        label->setAnchorPoint({0.f, .5f});
        label->setPosition({8.f, y});
        if (entries[i].matchesLevel) label->setColor({120, 255, 120});
        else if (entries[i].own) label->setColor({120, 220, 255});
        scroll->m_contentLayer->addChild(label);

        auto menu = CCMenu::create();
        menu->setPosition({listSize.width - 30.f, y});
        auto spr = ButtonSprite::create("Use", "goldFont.fnt", "GJ_button_01.png", .6f);
        spr->setScale(.6f);
        auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(ImportPopup::onPick));
        btn->setTag(static_cast<int>(i));
        menu->addChild(btn);
        scroll->m_contentLayer->addChild(menu);
    }
    scroll->scrollToTop();
    return true;
}

void ImportPopup::onPick(CCObject* sender) {
    auto idx = static_cast<size_t>(static_cast<CCNode*>(sender)->getTag());
    if (idx >= m_files.size()) return;

    if (m_own[idx]) {
        auto res = Trainer::get().copyChartFrom(m_level, m_files[idx]);
        if (!res) {
            FLAlertLayer::create("Copy failed", res.unwrapErr(), "OK")->show();
            return;
        }
        if (res.unwrap()) {
            // Route came along: ready to play, no bot demo needed.
            Notification::create(fmt::format("Copied {} clicks + route", Trainer::get().chart.notes.size()), NotificationIcon::Success)->show();
            if (auto parent = m_parent.lock()) parent->refresh();
            this->onClose(nullptr);
        }
        else {
            Notification::create("Copied the clicks - bot demo mapping them onto the level", NotificationIcon::Success)->show();
            startBotDemo();
        }
        return;
    }

    auto res = Trainer::get().importChart(m_level, m_files[idx]);
    if (!res) {
        FLAlertLayer::create("Import failed", res.unwrapErr(), "OK")->show();
        return;
    }
    Notification::create(
        fmt::format("Imported {} clicks - bot demo mapping them onto the level", Trainer::get().chart.notes.size()),
        NotificationIcon::Success
    )->show();
    startBotDemo();
}

// ---------------------------------------------------------------- SectionsPopup

SectionsPopup* SectionsPopup::create() {
    auto ret = new SectionsPopup();
    if (ret->init()) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool SectionsPopup::init() {
    if (!Popup::init(340.f, 270.f)) return false;
    this->setTitle("Where to show the guide");
    auto& t = Trainer::get();

    // Whole level / only sections
    m_onlyToggle = CCMenuItemToggler::createWithStandardSprites(this, menu_selector(SectionsPopup::onOnly), .7f);
    m_onlyToggle->toggle(t.sectionsOnly);
    m_buttonMenu->addChildAtPosition(m_onlyToggle, Anchor::Top, {-120.f, -50.f});
    auto onlyLabel = CCLabelBMFont::create("Only in these sections (off = whole level)", "bigFont.fnt");
    onlyLabel->setScale(.33f);
    onlyLabel->setAnchorPoint({0.f, .5f});
    m_mainLayer->addChildAtPosition(onlyLabel, Anchor::Top, {-102.f, -50.f});

    // Current list
    auto listBg = CCLayerColor::create({0, 0, 0, 70}, 300.f, 92.f);
    listBg->ignoreAnchorPointForPosition(false);
    m_mainLayer->addChildAtPosition(listBg, Anchor::Center, {0.f, 20.f});
    m_list = CCMenu::create();
    m_list->setContentSize({300.f, 92.f});
    m_list->ignoreAnchorPointForPosition(false);
    m_mainLayer->addChildAtPosition(m_list, Anchor::Center, {0.f, 20.f});

    // Add a section: type it, or grab where you paused
    m_fromInput = TextInput::create(60.f, "from %");
    m_fromInput->setCommonFilter(CommonFilter::Float);
    m_mainLayer->addChildAtPosition(m_fromInput, Anchor::Bottom, {-115.f, 72.f});
    m_toInput = TextInput::create(60.f, "to %");
    m_toInput->setCommonFilter(CommonFilter::Float);
    m_mainLayer->addChildAtPosition(m_toInput, Anchor::Bottom, {-40.f, 72.f});

    auto addSpr = ButtonSprite::create("Add", "goldFont.fnt", "GJ_button_01.png", .7f);
    addSpr->setScale(.7f);
    auto addBtn = CCMenuItemSpriteExtra::create(addSpr, this, menu_selector(SectionsPopup::onAdd));
    m_buttonMenu->addChildAtPosition(addBtn, Anchor::Bottom, {28.f, 72.f});

    auto startSpr = ButtonSprite::create("Start here", "goldFont.fnt", "GJ_button_04.png", .7f);
    startSpr->setScale(.6f);
    auto startBtn = CCMenuItemSpriteExtra::create(startSpr, this, menu_selector(SectionsPopup::onStartHere));
    m_buttonMenu->addChildAtPosition(startBtn, Anchor::Bottom, {-78.f, 36.f});

    auto endSpr = ButtonSprite::create("End here", "goldFont.fnt", "GJ_button_04.png", .7f);
    endSpr->setScale(.6f);
    auto endBtn = CCMenuItemSpriteExtra::create(endSpr, this, menu_selector(SectionsPopup::onEndHere));
    m_buttonMenu->addChildAtPosition(endBtn, Anchor::Bottom, {20.f, 36.f});

    m_hereLabel = CCLabelBMFont::create(fmt::format("You paused at {:.1f}%", t.currentPercent()).c_str(), "chatFont.fnt");
    m_hereLabel->setScale(.6f);
    m_mainLayer->addChildAtPosition(m_hereLabel, Anchor::Bottom, {112.f, 36.f});

    this->refreshList();
    return true;
}

void SectionsPopup::refreshList() {
    auto& t = Trainer::get();
    m_list->removeAllChildren();
    if (t.sections.empty()) {
        auto label = CCLabelBMFont::create("No sections yet. Add one below.", "chatFont.fnt");
        label->setScale(.65f);
        label->setOpacity(160);
        label->setPosition(m_list->getContentSize() / 2);
        m_list->addChild(label);
        return;
    }
    // Up to 2 columns of 4 rows
    for (size_t i = 0; i < t.sections.size() && i < 8; i++) {
        auto const& sec = t.sections[i];
        float x = i < 4 ? 75.f : 225.f;
        float y = 92.f - 12.f - 22.f * static_cast<float>(i % 4);
        auto label = CCLabelBMFont::create(fmt::format("{:.1f}% - {:.1f}%", sec.from, sec.to).c_str(), "bigFont.fnt");
        label->setScale(.32f);
        label->setPosition({x - 10.f, y});
        m_list->addChild(label);

        auto xSpr = CCSprite::createWithSpriteFrameName("GJ_deleteIcon_001.png");
        xSpr->setScale(.5f);
        auto xBtn = CCMenuItemSpriteExtra::create(xSpr, this, menu_selector(SectionsPopup::onRemove));
        xBtn->setTag(static_cast<int>(i));
        xBtn->setPosition({x + 52.f, y});
        m_list->addChild(xBtn);
    }
}

void SectionsPopup::changed() {
    auto& t = Trainer::get();
    t.saveSections();
    t.rebuild();
    this->refreshList();
    // Keep the main Clicks menu's status line up to date
    if (auto parent = CCScene::get()->getChildByType<TrainerPopup>(0)) parent->refresh();
}

void SectionsPopup::onOnly(CCObject*) {
    // The toggler flips after this callback
    Trainer::get().sectionsOnly = !m_onlyToggle->isToggled();
    this->changed();
}

void SectionsPopup::onStartHere(CCObject*) {
    m_fromInput->setString(fmt::format("{:.1f}", Trainer::get().currentPercent()));
}

void SectionsPopup::onEndHere(CCObject*) {
    m_toInput->setString(fmt::format("{:.1f}", Trainer::get().currentPercent()));
}

void SectionsPopup::onAdd(CCObject*) {
    auto from = numFromString<float>(std::string(m_fromInput->getString()));
    auto to = numFromString<float>(std::string(m_toInput->getString()));
    if (!from || !to) {
        Notification::create("Enter both percentages", NotificationIcon::Warning, 1.5f)->show();
        return;
    }
    float a = std::clamp(from.unwrap(), 0.f, 100.f);
    float b = std::clamp(to.unwrap(), 0.f, 100.f);
    if (a > b) std::swap(a, b);
    if (b - a < .1f) {
        Notification::create("The section is empty", NotificationIcon::Warning, 1.5f)->show();
        return;
    }
    auto& t = Trainer::get();
    if (t.sections.size() >= 8) {
        Notification::create("Up to 8 sections per level", NotificationIcon::Warning, 1.5f)->show();
        return;
    }
    t.sections.push_back({a, b});
    // Adding a section means you want sections: switch it on.
    if (!t.sectionsOnly) {
        t.sectionsOnly = true;
        m_onlyToggle->toggle(true);
    }
    m_fromInput->setString("");
    m_toInput->setString("");
    this->changed();
}

void SectionsPopup::onRemove(CCObject* sender) {
    auto& t = Trainer::get();
    auto i = static_cast<size_t>(static_cast<CCNode*>(sender)->getTag());
    if (i < t.sections.size()) t.sections.erase(t.sections.begin() + static_cast<std::ptrdiff_t>(i));
    this->changed();
}
