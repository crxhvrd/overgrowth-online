# Overgrowth Online

Overgrowth Online is an unofficial, community-made continuation of [Overgrowth], built on the open-source engine Wolfire Games released. It brings the game online and keeps developing the engine and the game further.

It is in active development.

Only code is here. The game's data (art, levels, sounds) comes from your own copy of Overgrowth, which you can buy from [Wolfire Games].

Overgrowth Online is not affiliated with or endorsed by Wolfire Games.

## Compared to Wolfire's multiplayer prototype

Wolfire's online prototype (2021) was left unfinished. Overgrowth Online started from it and reworked it:

| | Wolfire's prototype | Overgrowth Online |
|---|---|---|
| Your character | Runs on the host: every move waits for the network | Runs on your machine: the network adds no input lag |
| Other characters | Full poses about 120 times a second, about 5 Mbit/s per player | Compact streams, about 35 kbit/s per player in a two-player game, smooth on any ping |
| Hits | Decided on the host | Decided by the defender's own machine |
| Joining | Steam invites, or TCP by IP | Steam friends, a server browser, direct IP |
| Weapons | Pick-ups not shared | Held, thrown, stuck in a body or left on the ground: the same on every machine, tested with guests on different pings |
| Sounds | No footsteps from other players | Shared |
| Game modes | Any level, without co-op or PvP rules | Story co-op, arena PvP with bots |

## License

Based on [Overgrowth's source code](https://github.com/WolfireGames/overgrowth). Unless otherwise noted, the code is licensed under `Apache-2.0`; see [LICENSE]. Third-party libraries keep their own licenses.

[Overgrowth]: https://overgrowth.wolfire.com
[Wolfire Games]: https://wolfire.com
[LICENSE]: LICENSE
