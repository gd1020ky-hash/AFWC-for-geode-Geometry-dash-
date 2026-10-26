#pragma once
// Standard C++20 only: no game memory offsets, SDK or platform dependencies.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace forge {
constexpr unsigned TickRate = 240;
constexpr std::uint32_t MaxFrame = 2'000'000;
constexpr std::size_t MaxEvents = 200'000;
struct Event {
    std::uint32_t frame = 0;
    int button = 1;
    bool player1 = true;
    bool down = false;
};
struct Macro {
    std::string levelKey;
    std::uint32_t endFrame = 0;
    std::vector<Event> events;
};
inline std::size_t key(Event const& e) {
    return static_cast<std::size_t>((e.player1 ? 0 : 3) + e.button - 1);
}
inline void validate(Macro const& m) {
    if (m.levelKey.size() != 16 || m.levelKey.find_first_not_of("0123456789abcdef") != std::string::npos)
        throw std::runtime_error("Invalid level fingerprint");
    if (m.endFrame > MaxFrame || m.events.empty() || m.events.size() > MaxEvents)
        throw std::runtime_error("Macro size/frame limits exceeded, or no inputs recorded");
    std::array<bool, 6> held{};
    std::uint32_t previous = 0;
    for (auto const& e : m.events) {
        if (e.button < 1 || e.button > 3 || e.frame < previous || e.frame > m.endFrame)
            throw std::runtime_error("Invalid or unordered input");
        if (held[key(e)] == e.down)
            throw std::runtime_error("Duplicate press or unpaired release");
        held[key(e)] = e.down;
        previous = e.frame;
    }
    if (std::any_of(held.begin(), held.end(), [](bool b) { return b; }))
        throw std::runtime_error("Macro ends with a held input");
}
inline std::string fingerprint(std::string const& bytes) {
    std::uint64_t h = 14695981039346656037ULL;
    for (unsigned char c : bytes) { h ^= c; h *= 1099511628211ULL; }
    std::ostringstream o;
    o << std::hex << std::setw(16) << std::setfill('0') << h;
    return o.str();
}
inline std::string encode(Macro const& m) {
    validate(m);
    std::ostringstream o;
    o << "FORGE_MACRO 1 " << TickRate << '\n' << m.levelKey << '\n'
      << m.endFrame << ' ' << m.events.size() << '\n';
    for (auto const& e : m.events)
        o << e.frame << ' ' << e.button << ' ' << e.player1 << ' ' << e.down << '\n';
    return o.str();
}
inline Macro decode(std::string const& text) {
    if (text.size() > 8 * 1024 * 1024) throw std::runtime_error("Macro file is too large");
    std::istringstream in(text);
    std::string magic;
    long long version, rate, end, count;
    Macro m;
    if (!(in >> magic >> version >> rate >> m.levelKey >> end >> count) ||
        magic != "FORGE_MACRO" || version != 1 || rate != TickRate ||
        end < 0 || end > MaxFrame || count <= 0 || count > static_cast<long long>(MaxEvents))
        throw std::runtime_error("Unsupported or malformed macro header");
    m.endFrame = static_cast<std::uint32_t>(end);
    for (long long i = 0; i < count; ++i) {
        long long f, button, p1, down;
        if (!(in >> f >> button >> p1 >> down) || f < 0 || f > MaxFrame ||
            button < 1 || button > 3 || p1 < 0 || p1 > 1 || down < 0 || down > 1)
            throw std::runtime_error("Malformed macro event");
        m.events.push_back({static_cast<std::uint32_t>(f), static_cast<int>(button), p1 == 1, down == 1});
    }
    std::string extra;
    if (in >> extra) throw std::runtime_error("Unexpected trailing macro data");
    validate(m);
    return m;
}
inline std::size_t nthJump(Macro const& m, int number) {
    if (number < 1) throw std::runtime_error("Press number must be positive");
    for (std::size_t i = 0; i < m.events.size(); ++i)
        if (m.events[i].down && m.events[i].button == 1 && m.events[i].player1 && --number == 0) return i;
    throw std::runtime_error("That player-1 jump press does not exist in this macro");
}
inline Macro shiftedTrial(Macro const& m, std::size_t press, int offset) {
    validate(m);
    if (press >= m.events.size() || !m.events[press].down) throw std::runtime_error("Not a press");
    std::size_t release = press + 1;
    while (release < m.events.size() && key(m.events[release]) != key(m.events[press])) ++release;
    if (release == m.events.size() || m.events[release].down) throw std::runtime_error("No matching release");
    // Preserve the hold duration and disallow crossing other edges of this same control.
    long long begin = static_cast<long long>(m.events[press].frame) + offset;
    long long end = static_cast<long long>(m.events[release].frame) + offset;
    if (begin < 0 || end > m.endFrame) throw std::runtime_error("Shift would leave the recorded run");
    for (std::size_t i = press; i > 0; --i) {
        auto const& prior = m.events[i - 1];
        if (key(prior) == key(m.events[press])) {
            if (begin <= prior.frame) throw std::runtime_error("Shift crosses an earlier edge");
            break;
        }
    }
    for (std::size_t i = release + 1; i < m.events.size(); ++i) {
        auto const& next = m.events[i];
        if (key(next) == key(m.events[press])) {
            if (end >= next.frame) throw std::runtime_error("Shift crosses a later edge");
            break;
        }
    }
    auto result = m;
    result.events[press].frame = static_cast<std::uint32_t>(begin);
    result.events[release].frame = static_cast<std::uint32_t>(end);
    std::stable_sort(result.events.begin(), result.events.end(), [](auto const& a, auto const& b) {
        return a.frame < b.frame;
    });
    validate(result);
    return result;
}
inline std::string object(int id, double x, double y, double scale = 1) {
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(scale))
        throw std::runtime_error("Non-finite object coordinate");
    std::ostringstream o;
    o.imbue(std::locale::classic());
    o << std::fixed << std::setprecision(3)
      << "1," << id << ",2," << x << ",3," << y << ",32," << scale << ';';
    return o.str();
}
inline std::string layout(int blocks, double start) {
    if (blocks < 16 || blocks > 240 || !std::isfinite(start) || start < 0 || start > 200000)
        throw std::runtime_error("Layout limits: 16..240 blocks, start X 0..200000");
    std::string out;
    for (int i = 0; i < blocks; ++i) {
        double x = start + i * 30;
        out += object(1, x, 15);
        // Spaced, isolated spikes: a draft cube layout, not a playability guarantee.
        if (i > 12 && i < blocks - 8 && i % 12 == 0) out += object(8, x, 45);
    }
    return out;
}
inline std::uint32_t randomStep(std::uint32_t& state) {
    state = state * 1664525U + 1013904223U;
    return state;
}
inline std::string decoration(int count, double start, std::uint32_t seed) {
    if (count < 1 || count > 200 || !std::isfinite(start) || start < 0 || start > 200000)
        throw std::runtime_error("Decoration limits exceeded");

    // Compose a deterministic glow ribbon: two smooth wave frequencies create a
    // readable flow, small seeded jitter keeps it organic, and recurring scale
    // accents create a beat. This remains a one-object-per-point bounded pass.
    double phaseOffset = (randomStep(seed) % 6283) / 1000.0;
    std::string out;
    for (int i = 0; i < count; ++i) {
        auto positionNoise = randomStep(seed);
        auto sizeNoise = randomStep(seed);
        double phase = i * 0.23 + phaseOffset;
        double flow = std::sin(phase) * 62.0 + std::sin(phase * 0.47 + phaseOffset * 1.7) * 24.0;
        double jitter = static_cast<int>(positionNoise % 25) - 12;
        double y = 330.0 + flow + jitter;
        double scale = 0.34 + (sizeNoise % 30) / 100.0;
        if (i % 16 == 0) scale = 1.0;
        else if (i % 8 == 0) scale = 0.82;
        else if (i % 4 == 0) scale = 0.66;
        out += object(211, start + i * 54, y, scale);
    }
    return out;
}
} // namespace forge
