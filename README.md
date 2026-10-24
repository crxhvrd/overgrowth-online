# Overgrowth Online

Overgrowth Online is an unofficial, community-made continuation of [Overgrowth], built on the open-source engine Wolfire Games released. It brings the game online and keeps developing the engine and the game further.

It is in active development.

Only code is here. The game's data (art, levels, sounds) comes from your own copy of Overgrowth, which you can buy from [Wolfire Games].

Overgrowth Online is not affiliated with or endorsed by Wolfire Games.

## Rendering compared to the original game

The renderer is being rebuilt under the original look. Every step is checked against screenshots of the build before it; the look changes in the table are deliberate.

| | Original Overgrowth | Overgrowth Online |
|---|---|---|
| Graphics API | OpenGL 3.2 | OpenGL 4.6, or Vulkan 1.3 (Settings > Screen > Renderer). If Vulkan fails to start, the game goes back to OpenGL |
| Platforms | Windows, macOS, Linux | Windows |
| Graphics resources | One shared vertex array, filtering stored on each texture, hundreds of loose uniforms looked up by name | Direct state access, immutable textures, sampler objects, vertex layouts, uniform blocks with fixed bindings, per-frame data in one ring buffer |
| Shaders | GLSL compiled by the driver; a variant missed at load time compiled during play | The same GLSL, also built to SPIR-V. On Vulkan the game's shaders ship precompiled, mods' shaders compile once and are cached, and pipelines are prepared during the menus and at level load |
| Character skinning | On the CPU. The optional GPU path was slow on NVIDIA GPUs in big battles (10-12 ms a frame with 96 characters on an RTX 5070 Ti) | On the GPU, the bones read from a buffer filled each frame |
| Depth buffer | 24-bit | Reversed-Z with 32-bit float depth: more precision in the distance |
| Anti-aliasing | MSAA (2x to 8x) | MSAA, or TAA on either renderer, with a sharpness slider. Characters and items write their own motion vectors; the rest of the scene is reprojected from the camera |
| Upscaling | None | On Vulkan: NVIDIA DLSS on GeForce RTX GPUs and AMD FSR 3.1 on any GPU, at native resolution as anti-aliasing (DLAA, FSR Native AA) or in Quality, Balanced, Performance and Ultra Performance modes. They use the same motion vectors as TAA |
| Bloom | Per colour channel: sunsets and backlit ears glowed pure red | Keeps each pixel's colour |
| Noise (dithering, grain) | A sine-based hash that comes out differently on each GPU and driver | An integer hash (PCG2D): the same on every GPU, on OpenGL and Vulkan |
| Errors | OpenGL errors hidden in release builds (an unused display list built for every item) | None on the test levels, checked with an OpenGL debug context and the Vulkan validation layer |

Next: GPU culling with multi-draw indirect and compute particles, then Intel XeSS and ray tracing on Vulkan.

## Compared to Wolfire's multiplayer prototype

Wolfire's online prototype (2021) was left unfinished. Overgrowth Online started from it and reworked it:

| | Wolfire's prototype | Overgrowth Online |
|---|---|---|
| Your character | Runs on the host: every move waits for the network | Runs on your machine: the network adds no input lag |
| Other characters | Full poses about 120 times a second, about 5 Mbit/s per player | Compact streams, about 35 kbit/s per player in a two-player game, smooth on any ping |
| Guests' frame rate | About half the host's: every streamed character's morph targets were looked up per vertex, twice a frame | Close to the host's |
| Hits | Decided on the host | Decided by the defender's own machine |
| Joining | Steam invites, or TCP by IP | Steam friends, a server browser, direct IP |
| Weapons | Pick-ups not shared | Held, thrown, stuck in a body or left on the ground: the same on every machine, tested with guests on different pings |
| Sounds | No footsteps from other players | Shared |
| Game modes | Any level, without co-op or PvP rules | Story co-op, arena PvP with bots |
| Who may change what | Any connected player could delete, spawn or move any object and rewrite player states and settings | The host checks every message: a player can only affect their own character; hits and grabs must be in reach |
| Malformed packets | Could crash the game | Dropped unread; tested with forged attacks |

## Mods

Other mods are not guaranteed to work online and have not been tested.

- The Multiplayer menu opens only when every enabled mod declares online support (`<SupportsOnline>true</SupportsOnline>` in its `mod.xml`). Most Workshop mods don't, so they have to be disabled to play online. The declaration is the mod author's own claim; nothing checks it.
- Everyone in a game needs the same mods enabled. Only the mod names are compared, not their versions: different versions of the same mod can still join each other and fall out of sync.
- Mods that only add assets (characters, weapons, maps, textures) are the most likely to work. Mods that replace the character scripts (`aschar.as`, `enemycontrol.as`) break online play: the network code relies on hooks in those scripts.
- The Overhaul mod that comes with Overgrowth Online (AI, HUD, camera, weather, trainer, Large Battles) is not required for multiplayer, but everyone must have it either on or off.

## License

Based on [Overgrowth's source code](https://github.com/WolfireGames/overgrowth). Unless otherwise noted, the code is licensed under `Apache-2.0`; see [LICENSE]. Third-party libraries keep their own licenses.

[Overgrowth]: https://overgrowth.wolfire.com
[Wolfire Games]: https://wolfire.com
[LICENSE]: LICENSE
