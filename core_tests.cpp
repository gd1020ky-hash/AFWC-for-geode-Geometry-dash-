#include "ForgeCore.hpp"
#include <cassert>
#include <functional>
#include <iostream>
using namespace forge;
static void rejects(std::function<void()> f) {
    bool threw = false; try { f(); } catch (std::exception const&) { threw = true; }
    assert(threw);
}
int main() {
    Macro m{fingerprint("test-level"), 600, {{10,1,true,true},{20,1,true,false},{100,1,true,true},{110,1,true,false}}};
    auto decoded = decode(encode(m));
    assert(decoded.levelKey == m.levelKey && decoded.events.size() == 4);
    assert(nthJump(m, 2) == 2);
    auto shifted = shiftedTrial(m, 2, -5);
    assert(shifted.events[2].frame == 95 && shifted.events[3].frame == 105);
    rejects([&] { shiftedTrial(m, 0, -11); });
    rejects([&] { shiftedTrial(m, 2, -90); });
    rejects([&] { decode(encode(m) + "junk"); });
    rejects([&] { auto bad=m; bad.events[1].down=true; validate(bad); });
    rejects([&] { auto bad=m; bad.events.pop_back(); validate(bad); });
    rejects([&] { auto bad=m; bad.events[0].button=4; validate(bad); });
    rejects([&] { decode("FORGE_MACRO 1 240 abc -1 3"); });
    assert(fingerprint("abc") == "e71fa2190541574b");
    auto autoDecor = decoration(30,600,42);
    assert(autoDecor == decoration(30,600,42));
    assert(autoDecor != decoration(30,600,43));
    assert(std::count(autoDecor.begin(), autoDecor.end(), ';') == 30);
    assert(autoDecor.find(",32,1.000;") != std::string::npos);
    assert(layout(48,600).find("1,8,") != std::string::npos);
    rejects([] { layout(100000,0); });
    rejects([] { decoration(1, std::numeric_limits<double>::quiet_NaN(),0); });
    std::cout << "Forge portable core tests passed\n";
}
