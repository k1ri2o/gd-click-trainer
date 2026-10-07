#include "Chart.hpp"

#include <gdr/gdr.hpp>
#include <algorithm>
#include <cstring>
#include <optional>
#include <map>

using namespace geode::prelude;

namespace {
    class TrainerReplay : public gdr::Replay<TrainerReplay, gdr::Input<"">> {
    public:
        TrainerReplay() : Replay("Click Trainer", 1) {}
    };

    ccColor4F laneColor(bool player2, uint8_t button) {
        if (player2) return {1.f, .45f, .75f, 1.f};      // pink
        switch (button) {
            case 2: return {.45f, 1.f, .45f, 1.f};       // left: green
            case 3: return {1.f, .9f, .35f, 1.f};        // right: yellow
            default: return {.35f, .85f, 1.f, 1.f};      // jump: cyan
        }
    }

    std::string laneLabel(bool player2, uint8_t button) {
        std::string p = player2 ? "P2" : "P1";
        switch (button) {
            case 2: return p + " L";
            case 3: return p + " R";
            default: return p;
        }
    }
}

void Chart::build(int holdThreshold) {
    std::stable_sort(inputs.begin(), inputs.end(), [](auto const& a, auto const& b) {
        return a.frame < b.frame;
    });

    // Lanes: every (player, button) combo that has at least one press, P1 first.
    lanes.clear();
    for (bool p2 : {false, true}) {
        for (uint8_t btn : {uint8_t(2), uint8_t(1), uint8_t(3)}) {
            bool used = std::any_of(inputs.begin(), inputs.end(), [&](auto const& in) {
                return in.down && in.player2 == p2 && in.button == btn;
            });
            if (used) lanes.push_back({p2, btn, laneLabel(p2, btn), laneColor(p2, btn)});
        }
    }

    int64_t lastFrame = inputs.empty() ? 0 : inputs.back().frame;

    notes.clear();
    std::map<int, int64_t> held; // lane -> press tick
    for (auto const& in : inputs) {
        int lane = laneFor(in.player2, in.button);
        if (lane < 0) continue;
        auto it = held.find(lane);
        if (in.down) {
            if (it == held.end()) held[lane] = in.frame;
        }
        else if (it != held.end()) {
            notes.push_back({lane, it->second, in.frame});
            held.erase(it);
        }
    }
    // Presses never released: hold until a bit past the end.
    for (auto const& [lane, start] : held) {
        notes.push_back({lane, start, lastFrame + static_cast<int64_t>(framerate)});
    }

    std::stable_sort(notes.begin(), notes.end(), [](auto const& a, auto const& b) {
        return a.start < b.start;
    });
    for (auto& n : notes) {
        n.isHold = (n.end - n.start) > holdThreshold;
    }
}

int Chart::laneFor(bool player2, uint8_t button) const {
    for (size_t i = 0; i < lanes.size(); i++) {
        if (lanes[i].player2 == player2 && lanes[i].button == button) return static_cast<int>(i);
    }
    return -1;
}

void Chart::resetJudgements(int64_t tick) {
    // A hold that started before the respawn point is skipped entirely:
    // you can't be holding it when you respawn.
    for (auto& n : notes) {
        bool past = n.start < tick;
        n.press = past ? Judgement::Perfect : Judgement::None;
        n.release = (!n.isHold || past) ? Judgement::Perfect : Judgement::None;
    }
}

namespace {
    // Reads the first numeric/bool field found under any of `keys`.
    std::optional<double> numField(matjson::Value const& v, std::initializer_list<char const*> keys) {
        for (auto key : keys) {
            if (!v.contains(key)) continue;
            auto const& f = v[key];
            if (f.isNumber()) return f.asDouble().unwrapOr(0.0);
            if (f.isBool()) return f.asBool().unwrapOr(false) ? 1.0 : 0.0;
        }
        return std::nullopt;
    }

    // JSON macros: GDR v1 (.gdr.json), xdBot (.json), old Mega Hack (.mhr.json).
    Result<Chart> loadJson(std::string_view text) {
        auto parsed = matjson::parse(text);
        if (!parsed) return Err("Not a valid macro file");
        auto json = std::move(parsed).unwrap();

        Chart chart;
        if (auto fps = numField(json, {"framerate", "fps"})) chart.framerate = *fps;
        else if (json.contains("meta")) {
            if (auto fps = numField(json["meta"], {"fps", "framerate"})) chart.framerate = *fps;
        }

        matjson::Value const* list = nullptr;
        for (auto key : {"inputs", "events", "actions"}) {
            if (json.contains(key) && json[key].isArray()) {
                list = &json[key];
                break;
            }
        }
        if (!list) return Err("No inputs found in this JSON macro");

        for (auto const& item : list->asArray().unwrap()) {
            auto frame = numField(item, {"frame", "f"});
            auto down = numField(item, {"down", "hold", "pressed", "click"});
            if (!frame || !down) continue;
            auto button = numField(item, {"btn", "button"}).value_or(1.0);
            auto p2 = numField(item, {"2p", "p2", "player2"}).value_or(0.0);
            chart.inputs.push_back({
                static_cast<int64_t>(*frame),
                static_cast<uint8_t>(button >= 1 && button <= 3 ? button : 1),
                p2 != 0.0,
                *down != 0.0,
            });
        }
        return Ok(std::move(chart));
    }

    Result<Chart> loadGdr2(std::span<uint8_t> data) {
        auto res = TrainerReplay::importData(data);
        if (res.isErr()) return Err(res.unwrapErr());
        auto const& replay = res.unwrap();

        Chart chart;
        // Our own files are always in GD's native tick unit; other bots use their framerate.
        bool ours = replay.botInfo.name == "Click Trainer";
        chart.framerate = ours ? kTicksPerSecond : (replay.framerate > 0 ? replay.framerate : 240.0);
        chart.source = replay.description;
        for (auto const& in : replay.inputs) {
            chart.inputs.push_back({static_cast<int64_t>(in.frame), in.button, in.player2, in.down});
        }
        return Ok(std::move(chart));
    }
}

Result<Chart> Chart::load(std::filesystem::path const& path) {
    GEODE_UNWRAP_INTO(auto data, file::readBinary(path));
    if (data.empty()) return Err("The file is empty");

    Result<Chart> res = Err("");
    if (data[0] == '{') {
        res = loadJson(std::string_view(reinterpret_cast<char const*>(data.data()), data.size()));
    }
    else if (data.size() >= 3 && std::memcmp(data.data(), "GDR", 3) == 0) {
        res = loadGdr2(std::span<uint8_t>(data.data(), data.size()));
    }
    else {
        return Err(
            "This looks like an old binary .gdr macro, which isn't supported.\n"
            "Convert it to .gdr2 in Mega Hack (Replay tab) and import that."
        );
    }
    if (!res) return Err("Could not read macro: {}", res.unwrapErr());

    auto chart = std::move(res).unwrap();
    if (chart.source.empty()) chart.source = string::pathToString(path.filename());
    if (chart.framerate <= 0) chart.framerate = 240.0;
    if (chart.framerate != kTicksPerSecond) {
        // Bot macros count frames at 240 per second (or their physics-bypass rate):
        // convert to GD's tick counter.
        double scale = kTicksPerSecond / chart.framerate;
        for (auto& in : chart.inputs) in.frame = static_cast<int64_t>(in.frame * scale + 0.5);
        chart.framerate = kTicksPerSecond;
    }
    return Ok(std::move(chart));
}

std::string Chart::levelNameOf(std::filesystem::path const& path) {
    auto data = file::readBinary(path);
    if (!data) return "";
    auto bytes = std::move(data).unwrap();
    auto res = TrainerReplay::importData(std::span<uint8_t>(bytes.data(), bytes.size()));
    return res.isOk() ? res.unwrap().levelInfo.name : "";
}

Result<> Chart::save(std::filesystem::path const& path, std::string const& levelName, uint32_t levelID) const {
    TrainerReplay replay;
    replay.framerate = framerate;
    replay.gameVersion = 22081;
    replay.description = source;
    replay.levelInfo = gdr::Level(levelName, levelID);
    for (auto const& in : inputs) {
        replay.inputs.push_back(gdr::Input<"">(static_cast<uint64_t>(std::max<int64_t>(in.frame, 0)), in.button, in.player2, in.down));
    }
    auto res = replay.exportData();
    if (res.isErr()) {
        return Err("Could not write macro: {}", res.unwrapErr());
    }
    GEODE_UNWRAP(file::createDirectoryAll(path.parent_path()));
    auto const& bytes = res.unwrap();
    return file::writeBinarySafe(path, ByteSpan(bytes.data(), bytes.size()));
}

std::filesystem::path chartsDir() {
    return Mod::get()->getSaveDir() / "charts";
}

std::string levelKey(GJGameLevel* level) {
    int id = level->m_levelID.value();
    if (id > 0) return std::to_string(id);

    // Local / editor levels have no ID: use a sanitized name.
    std::string name = level->m_levelName;
    std::string safe;
    for (char c : name) {
        safe += (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_') ? c : '_';
    }
    if (safe.empty()) safe = "unnamed";
    return "local_" + safe;
}

std::filesystem::path chartPathFor(GJGameLevel* level) {
    return chartsDir() / (levelKey(level) + ".gdr2");
}

std::filesystem::path pathFileFor(GJGameLevel* level) {
    return chartsDir() / (levelKey(level) + ".ctpath");
}

// ---------------------------------------------------------------- ChartPath

namespace {
    constexpr char kPathMagic[8] = {'C', 'T', 'P', 'A', 'T', 'H', '0', '1'};
}

CCPoint ChartPath::at(int64_t tick, bool player2) const {
    if (points.empty()) return {0.f, 0.f};
    auto const& p = points[static_cast<size_t>(std::clamp<int64_t>(tick, 0, length() - 1))];
    return player2 ? CCPoint{p.x2, p.y2} : CCPoint{p.x1, p.y1};
}

void ChartPath::set(int64_t tick, PathPoint p) {
    if (tick < 0) return;
    auto idx = static_cast<size_t>(tick);
    if (idx >= points.size()) {
        // Fill any skipped ticks with the last known point so the line stays continuous.
        PathPoint fill = points.empty() ? p : points.back();
        points.resize(idx + 1, fill);
    }
    points[idx] = p;
}

void ChartPath::truncate(int64_t tick) {
    if (tick < length()) points.resize(static_cast<size_t>(std::max<int64_t>(tick, 0)));
}

Result<ChartPath> ChartPath::load(std::filesystem::path const& path) {
    GEODE_UNWRAP_INTO(auto data, file::readBinary(path));
    size_t header = sizeof(kPathMagic) + 1 + sizeof(uint32_t);
    if (data.size() < header || std::memcmp(data.data(), kPathMagic, sizeof(kPathMagic)) != 0) {
        return Err("Invalid path file");
    }
    ChartPath out;
    out.dual = data[sizeof(kPathMagic)] != 0;
    uint32_t count;
    std::memcpy(&count, data.data() + sizeof(kPathMagic) + 1, sizeof(count));
    if (data.size() < header + size_t(count) * sizeof(PathPoint)) return Err("Truncated path file");
    out.points.resize(count);
    std::memcpy(out.points.data(), data.data() + header, size_t(count) * sizeof(PathPoint));
    return Ok(std::move(out));
}

Result<> ChartPath::save(std::filesystem::path const& path) const {
    ByteVector data;
    data.insert(data.end(), kPathMagic, kPathMagic + sizeof(kPathMagic));
    data.push_back(dual ? 1 : 0);
    uint32_t count = static_cast<uint32_t>(points.size());
    auto countBytes = reinterpret_cast<uint8_t const*>(&count);
    data.insert(data.end(), countBytes, countBytes + sizeof(count));
    auto bytes = reinterpret_cast<uint8_t const*>(points.data());
    data.insert(data.end(), bytes, bytes + points.size() * sizeof(PathPoint));
    GEODE_UNWRAP(file::createDirectoryAll(path.parent_path()));
    return file::writeBinarySafe(path, ByteSpan(data.data(), data.size()));
}
