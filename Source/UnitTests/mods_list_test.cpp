//-----------------------------------------------------------------------------
//           Name: mods_list_test.cpp
//      Developer: Overgrowth Online (crxhvrd)
//    Description: Unit tests for the mods list a lobby publishes for the server
//                 browser, and how it is compared with a player's mods
//                 (OnlineUtility::MakeModsList, CompareMods)
//        License: Apache-2.0, like the rest of the engine (see LICENSE)
//-----------------------------------------------------------------------------
#include <Online/online_utility.h>

#include <tut/tut.hpp>

#include <string>
#include <vector>

namespace tut {
struct datamodslist {
    int placeholder;
};

typedef test_group<datamodslist> tg_modslist;
tg_modslist test_group_modslist("ModsList");

typedef tg_modslist::object modslist;

typedef std::vector<OnlineUtility::LocalMod> Mods;

template <>
template <>
void modslist::test<1>() {
    set_test_name("the list: active mods only, by id, whichever way they were found");
    Mods mods = {{"z-mod", "Zed", true}, {"crxhvrd-overhaul", "Overhaul", true}, {"off-mod", "Off", false}};
    ensure_equals(OnlineUtility::MakeModsList(mods), std::string("crxhvrd-overhaul=Overhaul|z-mod=Zed"));
    ensure_equals(OnlineUtility::MakeModsList(Mods()), std::string(""));
}

template <>
template <>
void modslist::test<2>() {
    set_test_name("the same mods: nothing stands between");
    Mods mine = {{"a", "Alpha", true}, {"b", "Beta", true}};
    OnlineUtility::ModDifference d = OnlineUtility::CompareMods("a=Alpha|b=Beta", mine);
    ensure_equals(d.theirs, std::string("Alpha|Beta"));
    ensure_equals(d.install, std::string(""));
    ensure_equals(d.enable, std::string(""));
    ensure_equals(d.disable, std::string(""));
}

template <>
template <>
void modslist::test<3>() {
    set_test_name("install, turn on, turn off: each by its name");
    Mods mine = {{"shared", "Shared", true}, {"off-here", "My Off Name", false}, {"extra", "Extra", true}};
    OnlineUtility::ModDifference d = OnlineUtility::CompareMods("missing=Missing Mod|off-here=Their Name|shared=Shared", mine);
    ensure_equals(d.install, std::string("Missing Mod"));
    ensure_equals(d.enable, std::string("My Off Name"));
    ensure_equals(d.disable, std::string("Extra"));
    ensure_equals(d.theirs, std::string("Missing Mod|Their Name|Shared"));
}

template <>
template <>
void modslist::test<4>() {
    set_test_name("a mod found twice: the copy that's on counts");
    Mods mine = {{"twice", "Twice", false}, {"twice", "Twice", true}};
    OnlineUtility::ModDifference d = OnlineUtility::CompareMods("twice=Twice", mine);
    ensure_equals(d.enable, std::string(""));
    ensure_equals(d.disable, std::string(""));
}

template <>
template <>
void modslist::test<5>() {
    set_test_name("too many for a lobby: what doesn't fit is counted, and nothing is called extra");
    Mods mods;
    for (int i = 0; i < 100; ++i) {
        mods.push_back({"mod-" + std::to_string(100 + i), "Name " + std::to_string(i), true});
    }
    std::string list = OnlineUtility::MakeModsList(mods, 200);
    ensure("under the limit", list.size() <= 200);
    size_t plus = list.rfind("|+");
    ensure("says how many more", plus != std::string::npos);

    int shown = 1;
    for (size_t i = 0; i < plus; ++i) {
        shown += list[i] == '|';
    }
    ensure_equals(list.substr(plus + 2), std::to_string(100 - shown));

    OnlineUtility::ModDifference d = OnlineUtility::CompareMods(list, mods);
    ensure_equals(d.install, std::string(""));
    ensure_equals(d.disable, std::string(""));
    ensure("counts the rest", d.theirs.find("more") != std::string::npos);
}

template <>
template <>
void modslist::test<6>() {
    set_test_name("a lobby's text is anyone's: it can't break a row or run on");
    Mods mine;
    std::string long_name(300, 'x');
    OnlineUtility::ModDifference d = OnlineUtility::CompareMods(
        "a=Tab\there\nnew|=no id|b|c=" + long_name + "|||+|d=%s%n", mine);
    ensure("no tab", d.theirs.find('\t') == std::string::npos);
    ensure("no line break", d.theirs.find('\n') == std::string::npos);
    ensure("an entry without an id is dropped", d.theirs.find("no id") == std::string::npos);
    ensure("no name: the id", d.theirs.find("|b|") != std::string::npos);
    ensure("long names are cut", d.theirs.size() < 150);
    ensure("plain text stays plain", d.theirs.find("%s%n") != std::string::npos);
}

template <>
template <>
void modslist::test<7>() {
    set_test_name("names are cut between characters, not inside one");
    Mods mods = {{"a", std::string(47, 'x') + "\xD0\x96\xD0\x96", true}};  // (the 48th byte starts a two-byte letter)
    std::string list = OnlineUtility::MakeModsList(mods);
    ensure_equals(list, "a=" + std::string(47, 'x'));
}

template <>
template <>
void modslist::test<8>() {
    set_test_name("no list at all (a host with no mods): only what's on here is in the way");
    Mods mine = {{"mine", "Mine", true}, {"off", "Off", false}};
    OnlineUtility::ModDifference d = OnlineUtility::CompareMods("", mine);
    ensure_equals(d.theirs, std::string(""));
    ensure_equals(d.disable, std::string("Mine"));
}
}  // namespace tut
