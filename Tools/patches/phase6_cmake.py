"""Phase 6, part 2: the Steam build.

- ENABLE_STEAMWORKS keeps our network framework (Network/GNS) and compiles it
  against the Steamworks SDK's sockets (OVERHAUL_STEAM_SOCKETS): one
  transport, IP connections and Steam P2P/relays. Wolfire's Network/Steam
  framework is no longer selected.
- The SDK path is a cache setting, STEAMWORKS_SDK (default
  Proprietary/steamworks/sdk, the zip's "sdk" folder), not steamworks_1.51.
The GNS build (Build/, the automated tests) is unchanged.
Run from the repo root."""
P = "Projects/CMakeLists.txt"
s = open(P, encoding="utf-8").read()


def rep(o, n, count=1):
    global s
    assert s.count(o) == count, (o[:80], s.count(o))
    s = s.replace(o, n)


rep("""IF(ENABLE_STEAMWORKS)
    ADD_DEFINITIONS(-DENABLE_STEAMWORKS=1)
    ADD_DEFINITIONS(-DUSE_STEAM_NETWORK_FRAMEWORK)
ELSEIF(ENABLE_GAMENETWORKINGSOCKETS)""", """IF(ENABLE_STEAMWORKS)
    # Overhaul Online: our framework (Network/GNS) on Steam's sockets
    ADD_DEFINITIONS(-DENABLE_STEAMWORKS=1)
    ADD_DEFINITIONS(-DUSE_GNS_NETWORK_FRAMEWORK)
    ADD_DEFINITIONS(-DOVERHAUL_STEAM_SOCKETS)
ELSEIF(ENABLE_GAMENETWORKINGSOCKETS)""")

rep("""SET(PLIBDIR "${CMAKE_SOURCE_DIR}/../Proprietary/Libraries")""",
    """SET(PLIBDIR "${CMAKE_SOURCE_DIR}/../Proprietary/Libraries")
SET(STEAMWORKS_SDK "${CMAKE_SOURCE_DIR}/../Proprietary/steamworks/sdk" CACHE PATH "The Steamworks SDK (the zip's sdk folder)")""")

n = s.count("${PLIBDIR}/steamworks_1.51/sdk")
assert n >= 5, n
s = s.replace("${PLIBDIR}/steamworks_1.51/sdk", "${STEAMWORKS_SDK}")
open(P, "w", encoding="utf-8", newline="").write(s)
print("ok", n, "SDK paths")
