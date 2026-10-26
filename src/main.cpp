#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/EditorUI.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include <Geode/fmod/fmod.hpp>
#include "ForgeCore.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <utility>

using namespace geode::prelude;
namespace fs = std::filesystem;
namespace {
enum class Mode { Off, Record, Playback, Probe };
struct Trial { int offset; int result = -1; }; // -1 pending, 0 failed, 1 survived, 2 invalid shift
struct Runtime {
    Mode mode = Mode::Off;
    forge::Macro macro, trial;
    std::array<bool,6> held{};
    std::size_t cursor = 0, trialIndex = 0, pressIndex = 0;
    std::vector<Trial> trials;
    std::uint32_t frame = 0, horizon = 0, trialLimit = 0;
    std::optional<std::uint32_t> lastClick, clickGap;
    std::optional<bool> outcome;
    bool injecting = false, silencing = false, advanceQueued = false;
    FMOD::ChannelGroup* musicGroup = nullptr;
    float originalPitch = 1.f;
    bool pitchChanged = false;
    std::string status = "Idle";
} g;

PlayLayer* play() { return GameManager::sharedState()->m_playLayer; }
bool activeLayer(GJBaseGameLayer* p) { return play() && static_cast<GJBaseGameLayer*>(play()) == p; }
bool practice(PlayLayer* p) { return p && p->m_isPracticeMode; }
std::string levelKey(PlayLayer* p) {
    if (!p || !p->m_level) throw std::runtime_error("No active level");
    auto* level = p->m_level;
    return forge::fingerprint(std::string(level->m_levelName.c_str()) + "\n" +
        std::string(level->m_levelString.c_str(), level->m_levelString.size()));
}
void message(std::string const& text) { FLAlertLayer::create("Frame Forge", text, "OK")->show(); }
fs::path directory() { auto p = Mod::get()->getSaveDir(); fs::create_directories(p); return p; }
std::string readLimited(fs::path const& path, std::uintmax_t maximum) {
    if (!fs::is_regular_file(path) || fs::file_size(path) > maximum)
        throw std::runtime_error("Missing, non-regular or oversized file: " + path.filename().string());
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("Cannot open " + path.filename().string());
    std::string data((std::istreambuf_iterator<char>(in)), {});
    if (in.bad() || data.size() > maximum) throw std::runtime_error("File read failed");
    return data;
}
fs::path saveUnique(std::string const& prefix, std::string const& extension, std::string const& data) {
    auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    fs::path path;
    for (unsigned counter = 0;; ++counter) {
        path = directory() / (prefix + std::to_string(millis) + "-" + std::to_string(counter) + extension);
        if (!fs::exists(path)) break;
    }
    auto temp = path; temp += ".tmp";
    std::ofstream out(temp, std::ios::binary);
    out << data; out.flush();
    if (!out) { out.close(); fs::remove(temp); throw std::runtime_error("Could not write file"); }
    out.close(); fs::rename(temp, path);
    return path;
}
fs::path replayPath() {
    auto selected = Mod::get()->getSettingValue<std::string>("replay-file");
    if (!selected.empty()) {
        if (fs::path(selected).filename().string() != selected || fs::path(selected).extension() != ".fmacro")
            throw std::runtime_error("Replay file must be a .fmacro filename, not a path");
        return directory() / selected;
    }
    fs::path newest;
    for (auto const& entry : fs::directory_iterator(directory())) {
        if (entry.is_regular_file() && entry.path().extension() == ".fmacro" &&
            (newest.empty() || entry.path().filename().string() > newest.filename().string())) newest = entry.path();
    }
    if (newest.empty()) throw std::runtime_error("Record and Stop+Save a macro first");
    return newest;
}
void loadMacro(PlayLayer* p) {
    if (!Mod::get()->getSettingValue<std::string>("replay-file").empty() || g.macro.events.empty())
        g.macro = forge::decode(readLimited(replayPath(), 8 * 1024 * 1024));
    forge::validate(g.macro);
    if (g.macro.levelKey != levelKey(p)) throw std::runtime_error("Macro belongs to a different or edited level");
}
void restoreMusic() {
    auto* engine = FMODAudioEngine::sharedEngine();
    if (g.pitchChanged && engine->m_backgroundMusicChannel == g.musicGroup && g.musicGroup)
        g.musicGroup->setPitch(g.originalPitch);
    g.pitchChanged = false; g.musicGroup = nullptr;
}
void updateMusic(PlayLayer* p) {
    auto* group = FMODAudioEngine::sharedEngine()->m_backgroundMusicChannel;
    if (!practice(p)) { restoreMusic(); return; }
    double speed = Mod::get()->getSettingValue<bool>("speed-enabled") ?
        Mod::get()->getSettingValue<double>("game-speed") : 1.0;
    double rate = Mod::get()->getSettingValue<double>("audio-speed");
    if (Mod::get()->getSettingValue<bool>("music-sync")) rate *= speed;
    if (!group) return;
    if (group != g.musicGroup) {
        g.musicGroup = group; g.originalPitch = 1.f; g.pitchChanged = false;
        group->getPitch(&g.originalPitch);
    }
    group->setPitch(g.originalPitch * static_cast<float>(rate));
    g.pitchChanged = true;
}
void cue(std::string const& filename, bool fallback) {
    try {
        auto custom = directory() / filename;
        if (fs::is_regular_file(custom)) FMODAudioEngine::sharedEngine()->playEffect(custom.string());
        else if (fallback) FMODAudioEngine::sharedEngine()->playEffect("click.ogg");
    } catch (std::exception const& e) { log::warn("Frame Forge sound: {}", e.what()); }
}
void edge(PlayLayer* p, bool down, int button, bool player1) {
    if (g.silencing || button < 1 || button > 3) return;
    std::uint32_t frame = p->m_gameState.m_currentProgress;
    if (down) {
        if (g.lastClick && frame >= *g.lastClick) g.clickGap = frame - *g.lastClick;
        g.lastClick = frame;
    }
    if (Mod::get()->getSettingValue<bool>("click-sounds")) cue(down ? "press.ogg" : "release.ogg", true);
    if (g.mode != Mode::Record || g.injecting) return;
    forge::Event e{frame, button, player1, down};
    if (g.held[forge::key(e)] == down) return;
    if (frame > forge::MaxFrame || g.macro.events.size() >= forge::MaxEvents - 6)
        throw std::runtime_error("Recording limit reached; stop and save shorter recordings");
    g.held[forge::key(e)] = down;
    g.macro.events.push_back(e);
    g.macro.endFrame = std::max(g.macro.endFrame, frame);
}
void release(PlayLayer* p) {
    if (!p) return;
    bool oldInjecting = g.injecting, oldSilencing = g.silencing;
    g.injecting = true; g.silencing = true;
    for (int b = 1; b <= 3; ++b) {
        p->handleButton(false, b, true); p->handleButton(false, b, false);
    }
    g.injecting = oldInjecting; g.silencing = oldSilencing;
}
void stop(PlayLayer* p, bool saveRecording) {
    bool recording = g.mode == Mode::Record;
    g.mode = Mode::Off; g.outcome.reset(); g.advanceQueued = false;
    release(p);
    if (recording && saveRecording) {
        g.macro.endFrame = std::max(g.macro.endFrame, g.frame);
        for (int i = 0; i < 6; ++i) if (g.held[i]) {
            g.macro.events.push_back({g.macro.endFrame, i % 3 + 1, i < 3, false});
        }
        g.held.fill(false);
        auto path = saveUnique("take-", ".fmacro", forge::encode(g.macro));
        g.status = "Saved " + path.filename().string();
    } else g.status = "Stopped";
    release(p);
}
void emergency(std::string const& error) {
    g.mode = Mode::Off; g.outcome.reset(); g.injecting = false; g.advanceQueued = false;
    g.status = error;
    release(play()); restoreMusic();
    log::warn("Frame Forge: {}", error);
}
void resetAttempt(PlayLayer* p) {
    release(p);
    p->removeAllCheckpoints();
    p->resetLevelFromStart();
}
void reportTrials() {
    std::ostringstream out;
    out << "Frame Forge empirical input-window report\n"
        << "240 engine ticks/second; NOT render frames or an exact mathematical window.\n"
        << "Pass = no death before the fixed survival horizon, or practice completion.\n"
        << "Level fingerprint: " << g.macro.levelKey << "\n"
        << "Player-1 jump press: " << Mod::get()->getSettingValue<int64_t>("probe-press") << "\n"
        << "Original press tick: " << g.macro.events[g.pressIndex].frame << "\n"
        << "Survival horizon tick: " << g.horizon << "\n"
        << "Both press and release are shifted equally. RNG/state are not snapshotted.\n\n";
    int passed = 0, longest = 0, run = 0;
    std::string offsets;
    for (auto const& t : g.trials) {
        char const* name = t.result == 1 ? "PASS" : t.result == 0 ? "FAIL" : "SKIPPED_INVALID_SHIFT";
        out << t.offset << " : " << name << '\n';
        if (t.result == 1) { ++passed; longest = std::max(longest, ++run); offsets += std::to_string(t.offset) + " "; }
        else run = 0;
    }
    out << "\nPassing offsets: " << offsets << "\nLargest contiguous passing sample: " << longest << " ticks\n";
    auto file = saveUnique("window-", ".txt", out.str());
    g.mode = Mode::Off; release(play());
    g.status = "Probe: " + std::to_string(passed) + " offsets passed";
    cue(passed ? "window-pass.ogg" : "window-fail.ogg", false);
    message("Timing trials finished. Passing offsets: " + (offsets.empty() ? std::string("none") : offsets) +
        ". Largest contiguous sampled window: " + std::to_string(longest) + " ticks. Report: " + file.filename().string());
}
void nextTrial(PlayLayer* p) {
    g.outcome.reset(); g.advanceQueued = false;
    while (g.trialIndex < g.trials.size()) {
        try {
            g.trial = forge::shiftedTrial(g.macro, g.pressIndex, g.trials[g.trialIndex].offset);
            g.cursor = 0;
            g.status = "Probe offset " + std::to_string(g.trials[g.trialIndex].offset);
            resetAttempt(p);
            return;
        } catch (std::runtime_error const&) {
            g.trials[g.trialIndex++].result = 2;
        }
    }
    reportTrials();
}
void startRecord(PlayLayer* p) {
    if (!practice(p)) throw std::runtime_error("Enable Practice Mode first. Tools do not verify normal completions.");
    stop(p, false);
    g.mode = Mode::Record; g.macro = {levelKey(p),0,{}}; g.status = "Recording";
    resetAttempt(p);
}
void startPlayback(PlayLayer* p) {
    if (!practice(p)) throw std::runtime_error("Playback requires Practice Mode");
    if (g.mode == Mode::Record) throw std::runtime_error("Stop+Save the recording first");
    loadMacro(p); g.mode = Mode::Playback; g.cursor = 0; g.status = "Playback";
    resetAttempt(p);
}
void startProbe(PlayLayer* p) {
    if (!practice(p)) throw std::runtime_error("Timing trials require Practice Mode");
    if (g.mode == Mode::Record) throw std::runtime_error("Stop+Save the recording first");
    loadMacro(p);
    g.pressIndex = forge::nthJump(g.macro, static_cast<int>(Mod::get()->getSettingValue<int64_t>("probe-press")));
    int radius = static_cast<int>(Mod::get()->getSettingValue<int64_t>("probe-radius"));
    auto horizon = static_cast<std::uint32_t>(Mod::get()->getSettingValue<int64_t>("probe-horizon"));
    g.horizon = g.macro.events[g.pressIndex].frame + horizon + radius;
    if (g.horizon > g.macro.endFrame)
        throw std::runtime_error("Record longer after that press, or reduce the survival horizon/radius");
    g.mode = Mode::Probe; g.trials.clear(); g.trialIndex = 0;
    for (int i = -radius; i <= radius; ++i) g.trials.push_back({i,-1});
    nextTrial(p);
}
std::string aiObjects() {
    auto parsed = matjson::parse(readLimited(directory() / "ai-plan.json", 128 * 1024));
    if (parsed.isErr()) throw std::runtime_error("Invalid AI JSON");
    auto value = parsed.unwrap();
    if (!value.isObject() || value["version"].asInt().unwrapOr(0) != 1 ||
        !value["model"].isString() || !value["prompt"].isString())
        throw std::runtime_error("Expected a version-1 AI plan from ai_decorate.py");
    auto array = value["decorations"].asArray();
    if (array.isErr() || array.unwrap().empty() || array.unwrap().size() > 200)
        throw std::runtime_error("AI plan must contain 1..200 decorations");
    std::string out;
    for (auto const& item : array.unwrap()) {
        if (!item.isObject() || !item["x"].isNumber() || !item["y"].isNumber() || !item["scale"].isNumber())
            throw std::runtime_error("Invalid AI decoration point");
        double x = item["x"].asDouble().unwrapOr(-1);
        double y = item["y"].asDouble().unwrapOr(-1);
        double scale = item["scale"].asDouble().unwrapOr(-1);
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(scale) ||
            x < 0 || x > 250000 || y < 240 || y > 1200 || scale < .25 || scale > 2)
            throw std::runtime_error("AI coordinates exceed the allowed bounds");
        out += forge::object(211, x, y, scale);
    }
    return out;
}

class ForgePanel : public Popup {
    PlayLayer* m_play = nullptr;
    LevelEditorLayer* m_editor = nullptr;
    void addLabel(char const* text, float x, float y, float scale, ccColor3B color) {
        auto* label = CCLabelBMFont::create(text, "bigFont.fnt");
        if (!label) return;
        label->setScale(scale);
        label->setColor(color);
        label->setPosition({x,y});
        m_mainLayer->addChild(label, 2);
    }
    void addButton(CCMenu* menu, char const* title, int tag, float x, float y) {
        auto* sprite = ButtonSprite::create(title);
        sprite->setScale(.58f);
        auto* item = CCMenuItemSpriteExtra::create(sprite, this, menu_selector(ForgePanel::onAction));
        item->setTag(tag); item->setPosition({x,y}); menu->addChild(item);
    }
    void onAction(CCObject* sender) {
        int action = static_cast<CCNode*>(sender)->getTag();
        try {
            switch (action) {
                case 1: startRecord(m_play); break;
                case 2: startPlayback(m_play); break;
                case 3: stop(m_play, true); message(g.status); break;
                case 4: startProbe(m_play); break;
                case 8: openSettingsPopup(Mod::get()); return;
                default: {
                    if (!m_editor || !m_editor->m_editorUI) throw std::runtime_error("Open the level editor for generators");
                    std::string objects;
                    double x = Mod::get()->getSettingValue<double>("build-x");
                    int count = static_cast<int>(Mod::get()->getSettingValue<int64_t>("build-count"));
                    if (action == 5) objects = forge::layout(count,x);
                    else if (action == 6) objects = forge::decoration(std::min(count,200),x,
                        static_cast<std::uint32_t>(Mod::get()->getSettingValue<int64_t>("decoration-seed")));
                    else if (action == 7) objects = aiObjects();
                    else throw std::runtime_error("Unknown action");
                    // Native paste path supplies selection and one native undo transaction.
                    // Coordinates follow GD's paste placement relative to the current editor view.
                    auto* editor = m_editor;
                    createQuickPopup("Insert generated objects?",
                        "This adds objects; it does not replace the level. Back up first. Check the result and use Undo if needed. No playability or art-quality guarantee.",
                        "Cancel", "Insert", [editor, objects=std::move(objects)](FLAlertLayer*, bool confirmed) {
                            if (confirmed && LevelEditorLayer::get() == editor && editor->m_editorUI) {
                                editor->m_editorUI->pasteObjects(gd::string(objects.c_str()), true, false);
                            }
                        });
                    break;
                }
            }
            onClose(nullptr);
        } catch (std::exception const& e) { message(e.what()); }
    }
public:
    static ForgePanel* create(PlayLayer* p, LevelEditorLayer* editor) {
        auto* panel = new ForgePanel;
        if (panel->init(p,editor)) { panel->autorelease(); return panel; }
        delete panel; return nullptr;
    }
    bool init(PlayLayer* p, LevelEditorLayer* editor) {
        if (!Popup::init(400,350)) return false;
        m_play = p; m_editor = editor; setTitle("CONTROL DECK");
        auto* menu = CCMenu::create(); menu->setPosition({0,0}); m_mainLayer->addChild(menu);

        // Render the approved cyan-and-gold Frame Forge badge in the menu.
        auto* logo = CCSprite::create("frame-forge-logo.png"_spr);
        if (logo) {
            logo->setPosition({66, 300});
            logo->setScale(0.30f);
            m_mainLayer->addChild(logo, 2);
        } else {
            addLabel("FF", 66, 300, .82f, ccc3(75,232,255));
        }
        addLabel("FRAME FORGE", 225, 307, .43f, ccc3(255,205,105));
        addLabel("PRACTICE LAB  /  LEVEL STUDIO", 225, 284, .22f, ccc3(123,221,255));

        addLabel("PRACTICE TOOLS", 200, 257, .25f, ccc3(255,205,105));
        addButton(menu,"Record",1,105,230); addButton(menu,"Play latest",2,295,230);
        addButton(menu,"Stop + Save",3,105,194); addButton(menu,"Measure window",4,295,194);

        addLabel("LEVEL CREATION", 200, 162, .25f, ccc3(255,205,105));
        addButton(menu,"Auto layout",5,105,136); addButton(menu,"Auto decorate",6,295,136);
        addButton(menu,"Import AI plan",7,105,100); addButton(menu,"Settings",8,295,100);

        addLabel(m_editor ? "EDITOR MODE  /  BUILD + DECORATE" : "PRACTICE MODE  /  MACRO + TIMING",
            200, 59, .22f, ccc3(123,221,255));
        addLabel(m_editor ? "Insertions use native paste and can be undone" : "Local practice tools; no run verification",
            200, 39, .19f, ccc3(170,183,205));
        return true;
    }
};
} // namespace

class $modify(ForgeGameLayer, GJBaseGameLayer) {
    void update(float dt) {
        if (!activeLayer(this)) { GJBaseGameLayer::update(dt); return; }
        auto* p = play();
        if (!practice(p) && g.mode != Mode::Off) emergency("Stopped: Practice Mode was disabled");
        float speed = practice(p) && Mod::get()->getSettingValue<bool>("speed-enabled") ?
            static_cast<float>(Mod::get()->getSettingValue<double>("game-speed")) : 1.f;
        updateMusic(p);
        GJBaseGameLayer::update(dt * speed);
    }
    void handleButton(bool down, int button, bool isPlayer1) {
        if (activeLayer(this)) {
            if (!g.injecting && (g.mode == Mode::Playback || g.mode == Mode::Probe)) return;
            try { edge(play(),down,button,isPlayer1); }
            catch (std::exception const& e) { emergency(e.what()); }
        }
        GJBaseGameLayer::handleButton(down,button,isPlayer1);
    }
    void processCommands(float dt, bool halfTick, bool lastTick) {
        if (activeLayer(this)) {
            g.frame = m_gameState.m_currentProgress;
            if (g.mode == Mode::Record) g.macro.endFrame = std::max(g.macro.endFrame,g.frame);
            if ((g.mode == Mode::Playback || g.mode == Mode::Probe) && !g.outcome) {
                auto const& macro = g.mode == Mode::Probe ? g.trial : g.macro;
                g.injecting = true;
                while (g.cursor < macro.events.size() && macro.events[g.cursor].frame <= g.frame) {
                    auto const& e = macro.events[g.cursor++];
                    this->handleButton(e.down,e.button,e.player1);
                }
                g.injecting = false;
            }
        }
        GJBaseGameLayer::processCommands(dt,halfTick,lastTick);
        if (activeLayer(this)) {
            if (g.mode == Mode::Probe && !g.outcome && g.frame >= g.horizon)
                g.outcome = m_player1 && !m_player1->m_isDead &&
                    (!m_gameState.m_isDualMode || (m_player2 && !m_player2->m_isDead));
            if (g.mode == Mode::Playback && g.frame >= g.macro.endFrame) {
                g.mode = Mode::Off; g.status = "Playback ended"; release();
            }
        }
    }
};

class $modify(ForgePlayLayer, PlayLayer) {
    struct Fields { CCLabelBMFont* counter = nullptr; };
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        restoreMusic(); g = Runtime{};
        if (!PlayLayer::init(level,useReplay,dontCreateObjects)) return false;
        auto size = CCDirector::sharedDirector()->getWinSize();
        auto* label = CCLabelBMFont::create("Frame Forge", "bigFont.fnt");
        label->setAnchorPoint({0,1}); label->setPosition({8,size.height-35}); label->setScale(.32f);
        this->addChild(label,10000); m_fields->counter = label;
        return true;
    }
    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);
        if (g.mode == Mode::Probe && g.outcome && !g.advanceQueued) {
            g.advanceQueued = true;
            this->scheduleOnce(schedule_selector(ForgePlayLayer::advanceProbe), 0.f);
        }
        if (auto* label = m_fields->counter) {
            label->setVisible(Mod::get()->getSettingValue<bool>("frame-counter"));
            std::string text = "Tick " + std::to_string(m_gameState.m_currentProgress) + "  Gap " +
                (g.clickGap ? std::to_string(*g.clickGap) : "--") + "\n" + g.status;
            label->setString(text.c_str());
        }
    }
    void advanceProbe(float) {
        if (g.mode != Mode::Probe || !g.advanceQueued || !g.outcome) return;
        g.trials[g.trialIndex].result = *g.outcome ? 1 : 0;
        ++g.trialIndex;
        try { nextTrial(this); }
        catch (std::exception const& e) { emergency(e.what()); message(e.what()); }
    }
    void resetLevel() {
        if (g.mode != Mode::Off) removeAllCheckpoints();
        bool wasInjecting = g.injecting; g.injecting = true;
        PlayLayer::resetLevel();
        g.injecting = wasInjecting;
        g.cursor = 0; g.frame = 0; g.held.fill(false); g.lastClick.reset(); g.clickGap.reset();
        if (g.mode == Mode::Record) { g.macro.events.clear(); g.macro.endFrame = 0; }
    }
    void destroyPlayer(PlayerObject* player, GameObject* object) {
        if (g.mode == Mode::Probe && !g.outcome) g.outcome = false;
        PlayLayer::destroyPlayer(player,object);
    }
    void levelComplete() {
        if (g.mode == Mode::Probe && !g.outcome) g.outcome = true;
        PlayLayer::levelComplete();
    }
    void onQuit() {
        try { stop(this,g.mode == Mode::Record); }
        catch (std::exception const& e) { log::warn("Could not save recording on exit: {}",e.what()); }
        restoreMusic(); g.mode = Mode::Off;
        PlayLayer::onQuit();
    }
};

class $modify(ForgePauseLayer, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();
        auto* menu = CCMenu::create(); menu->setPosition({0,0}); this->addChild(menu,1000);
        auto* sprite = ButtonSprite::create("Forge"); sprite->setScale(.6f);
        auto* item = CCMenuItemSpriteExtra::create(sprite,this,menu_selector(ForgePauseLayer::onForge));
        item->setPosition({58,30}); menu->addChild(item);
    }
    void onForge(CCObject*) { if (auto* panel = ForgePanel::create(play(),nullptr)) panel->show(); }
};
class $modify(ForgeEditorUI, EditorUI) {
    bool init(LevelEditorLayer* editor) {
        if (!EditorUI::init(editor)) return false;
        auto size = CCDirector::sharedDirector()->getWinSize();
        auto* menu = CCMenu::create(); menu->setPosition({0,0}); this->addChild(menu,1000);
        auto* sprite = ButtonSprite::create("Forge"); sprite->setScale(.55f);
        auto* item = CCMenuItemSpriteExtra::create(sprite,this,menu_selector(ForgeEditorUI::onForge));
        item->setPosition({size.width-48,size.height-26}); menu->addChild(item);
        return true;
    }
    void onForge(CCObject*) { if (auto* panel = ForgePanel::create(nullptr,m_editorLayer)) panel->show(); }
};
