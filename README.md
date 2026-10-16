# Overgrowth Online

Overgrowth Online is an unofficial continuation of [Overgrowth], built on Wolfire Games' open-source engine: online co-op and PvP through Steam, a modernised renderer, and the Overgrowth Online mod with smarter AI, a new HUD, weather, a trainer and large battles.

Only code is here. The game's data (art, levels, sounds) comes from your own copy of Overgrowth, which you can buy from [Wolfire Games]. Overgrowth Online is not affiliated with or endorsed by Wolfire Games.

## Features

### Online play
- **Story co-op** through the Overgrowth and Lugaru campaigns, up to 8 players.
- **Arena PvP** on Waterfall Cave, Magma Arena, Rock Cave and Stucco Courtyard: rounds, points, weapon matches.
- **Steam friends:** friends-only lobbies, invites, *Join a friend* from the main menu, or *Join Game* from the Steam friends list.
- **Your character:** a name, any rabbit, cat, dog, rat or wolf, and your own fur and clothes colours.
- **Co-op that holds together:**
  - Each player runs their own character and the host runs the enemies and the story.
  - Hits are decided on the defender's machine.
  - Dialogues play for everyone, and guests follow the host to the next level.
  - Downed players can be revived.
- **Matched sessions:** players are matched on the same version, and host and guests load levels at the same time.

### The Overgrowth Online mod (F9 in any level)
- **AI:** enemies built around the jump kick that read and punish your habits, fight in the air, use the terrain, feint, and fight as teams.
- **GUI:** health panel, minimap, campaign objectives (the next checkpoint or the enemies to defeat), names and health above characters, damage numbers, combat log.
- **Camera:** third-person tuning, a dynamic cinematic camera, first person (V), cinematic finishers.
- **Weather:** any time of day, moving sun, moon, clouds, rain, storms, snow.
- **Trainer:** god mode, strength and speed, play as any character, weapons, teleport, spawning, game speed and gravity.
- **Large Battles** (single player): 2 to 8 teams of up to 60 fighters on 128 maps.

The mod also runs in the normal game. Online play needs the Overgrowth Online build.

### Renderer
- **OpenGL 4.6 core**, up from 3.2. Legacy fixed-function code is removed, along with fixes to the GL state handling.
- **Where it's going:** a modern resource layer, SPIR-V shaders, then Vulkan with ray tracing and upscaling.

## Requirements
- Windows 10 or 11, 64-bit.
- Overgrowth on Steam (the public 1.4 version), with Steam running.
- A graphics card and driver with OpenGL 4.6 (practically every GPU since 2012 with a current driver).

## Installing
From a package (`OvergrowthOnline_<version>.zip`, see [Packaging](#packaging)):

1. Close Overgrowth.
2. Run `install.bat`. It finds Overgrowth through Steam; if it can't, pass the folder: `install.bat "D:\SteamLibrary\steamapps\common\Overgrowth"`.
3. Start Overgrowth from Steam as usual. Hold **Shift** while it starts to get the normal game.

Overgrowth Online keeps its settings and mods in `Documents\Wolfire\OvergrowthOnline`, so the normal game is not changed. `uninstall.bat` brings back the normal `Overgrowth.exe`. Steam's *Verify integrity of game files* also removes the launcher.

## Building
You need Visual Studio 2022 (x64), CMake and Overgrowth installed. The scripts in `Tools/` contain the paths of the machine they were written on (Visual Studio in `R:\vs`, Overgrowth in `R:\SteamLibrary\...`). Change those paths first.

| Build | Scripts | Output |
|---|---|---|
| Steam build | `Tools\configure_steam.bat`, then `Tools\build_steam.bat` | `Build_steam\Release\OvergrowthOnline.exe` |
| Test build (direct connection, no Steam) | `Tools\configure.bat`, then `Tools\build.bat` | `Build\Release\OvergrowthOnline.exe` |

- **Steamworks SDK:** the Steam build needs it, and it is not included. Unzip it so that `Proprietary\steamworks\sdk\public\steam\steam_api.h` exists.
- **New source files:** run the configure script again after adding source files. CMake collects them when it configures.
- **Other platforms:** Wolfire's general guide is in [COMPILING.md]. Overgrowth Online is built and tested on Windows only.

### Packaging
`python -I Tools/package/make_package.py` turns the Steam build into `dist/OvergrowthOnline_<version>.zip`. It holds:
- the exe and its DLLs,
- the launcher,
- the mod,
- the install scripts and a player README.

It never includes game assets. The repository paths are set at the top of the script.

### Tests
`Tools/ogmp/` holds automated suites for co-op, PvP, combat, stress and the renderer. They drive real game instances through a local test bridge (`--test-bridge`).

## Repository layout
| Path | Contents |
|---|---|
| `Source/` | the engine; online play in `Source/Online` and `Source/Steam` |
| `Data/` | the engine's scripts and shaders, used on top of the game's own data |
| `mod/overgrowth-online/` | the Overgrowth Online mod |
| `Tools/` | build, packaging, launcher and test tooling |
| `Libraries/`, `Projects/` | third-party libraries and the CMake project |

## License
Overgrowth Online is based on [Overgrowth's source code](https://github.com/WolfireGames/overgrowth), released by Wolfire Games. Unless otherwise noted, the code in this repository is licensed under `Apache-2.0`; read [LICENSE] for the full license.

If you distribute this code or programs built from it:
- include the `LICENSE` file,
- keep the copyright notices,
- mark the files you change.

Third-party libraries keep their own licenses: see `Libraries/` and `Tools/package/THIRD_PARTY.txt`.

Distributing any of the original Overgrowth assets needs explicit written permission from Wolfire Games.

[Overgrowth]: https://overgrowth.wolfire.com
[Wolfire Games]: https://wolfire.com
[COMPILING.md]: COMPILING.md
[LICENSE]: LICENSE
