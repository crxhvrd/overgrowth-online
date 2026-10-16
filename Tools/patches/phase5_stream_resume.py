"""Phase 5, part 9: a slow stream is not a still character.

With budgets a far character may come at 1-2 Hz while it moves; the buffer
used to read any gap over 250 ms as "it stood still". Now the owner marks
the first record after it sent nothing because nothing moved (flags bit 4,
Frame::resumed), and the buffer holds the old pose only then, or after a
silence of over a second. Run from the repo root."""
S = "Source/"


def patch(p, pairs):
    s = open(S + p, encoding="utf-8").read()
    for o, n in pairs:
        assert s.count(o) == 1, (p, o[:90], s.count(o))
        s = s.replace(o, n)
    open(S + p, "w", encoding="utf-8", newline="").write(s)


patch("Online/char_snapshot.h", [
("""    bool has_morphs = false;
    std::vector<Morph> morphs;  // only the ones that changed, or all of them now and then""",
 """    bool has_morphs = false;
    std::vector<Morph> morphs;  // only the ones that changed, or all of them now and then
    bool resumed = false;       // the first record after the owner sent nothing because nothing moved"""),
("""//   flags byte: 1 = morphs, 2 = vitals""", """//   flags byte: 1 = morphs, 2 = vitals, 4 = resumed after standing still"""),
("""    static constexpr double kHoldGapMs = 250.0;         // a longer silence = the character stood still""",
 """    static constexpr double kHoldGapMs = 250.0;         // a resumed record after a longer gap: it stood still
    static constexpr double kSilenceMs = 1000.0;        // a gap this long counts as standing still anyway"""),
])

patch("Online/char_snapshot.cpp", [
("""    Put<uint8_t>(out, (uint8_t)((morphs ? 1 : 0) | (f.vitals.valid ? 2 : 0)));""",
 """    Put<uint8_t>(out, (uint8_t)((morphs ? 1 : 0) | (f.vitals.valid ? 2 : 0) | (f.resumed ? 4 : 0)));"""),
("""    f.has_morphs = (flags & 1) != 0;""", """    f.has_morphs = (flags & 1) != 0;
    f.resumed = (flags & 4) != 0;"""),
("""        double gap = src - last;
        if (gap > kHoldGapMs) {""", """        double gap = src - last;
        if (gap > kHoldGapMs && (f.resumed || gap > kSilenceMs)) {"""),
])

patch("Online/online.h", [
("""        CharSnapshot::Vitals last_vitals;
        int vitals_version = 0;""", """        CharSnapshot::Vitals last_vitals;
        int vitals_version = 0;
        bool idle = false;  // captures skipped since the last record"""),
])

patch("Online/online.cpp", [
("""            if (!morphs_moved && owned.has_last && CharSnapshot::Similar(frame, owned.last_sent)) {
                ++snapshot_idle_skips;
                continue;
            }""", """            if (!morphs_moved && owned.has_last && CharSnapshot::Similar(frame, owned.last_sent)) {
                ++snapshot_idle_skips;
                owned.idle = true;
                continue;
            }
            frame.resumed = owned.idle;  // it stood still until now: receivers hold, not glide
            owned.idle = false;"""),
])
print("ok")
