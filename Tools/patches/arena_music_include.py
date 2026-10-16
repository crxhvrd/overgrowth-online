"""Online arena, part 1: the arena music in its own include.

multi_arena.as's MusicLoad + MusicSegmentHandler (lines 31-314, unchanged)
move to multi_arena/multi_arena_music.as, so online_arena.as plays the same
music. Run from the repo root."""
P = "Data/Scripts/multi_arena.as"
s = open(P, encoding="utf-8").read()
start = s.index('MusicLoad ml("Data/Music/arena.xml");')
end = s.index("MusicSegmentHandler msh;")
block = s[start:end].rstrip() + "\n"
assert block.count("class MusicSegmentHandler") == 1 and block.count("class CountdownTimer") == 1
head = """//-----------------------------------------------------------------------------
//           Name: multi_arena_music.as
//      Developer: Wolfire Games LLC
//    Script Type: Level (include)
//    Description: The arena music (moved out of multi_arena.as unchanged, for
//                 online_arena.as too). Include music_load.as before it.
//        License: Read below
//-----------------------------------------------------------------------------
//
//   Copyright 2022 Wolfire Games LLC
//
//   Licensed under the Apache License, Version 2.0 (the "License");
//   you may not use this file except in compliance with the License.
//   You may obtain a copy of the License at
//
//       http://www.apache.org/licenses/LICENSE-2.0
//
//   Unless required by applicable law or agreed to in writing, software
//   distributed under the License is distributed on an "AS IS" BASIS,
//   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//   See the License for the specific language governing permissions and
//   limitations under the License.
//
//-----------------------------------------------------------------------------

"""
open("Data/Scripts/multi_arena/multi_arena_music.as", "w", encoding="utf-8", newline="").write(head + block)
s = s[:start] + '#include "multi_arena/multi_arena_music.as"\n\n' + s[end:]
s = s.replace('#include "music_load.as"\n\n#include "multi_arena/multi_arena_music.as"', '#include "music_load.as"\n#include "multi_arena/multi_arena_music.as"')
open(P, "w", encoding="utf-8", newline="").write(s)
print("moved", block.count("\n"), "lines")
