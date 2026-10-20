OVERGROWTH ONLINE {VERSION} - PUBLIC TEST BUILD
=============================================

An unofficial, community-made continuation of Overgrowth that brings the
game online. Not affiliated with or endorsed by Wolfire Games.

This is a test build: expect bugs.

REQUIREMENTS
  Overgrowth on Steam, Windows 64-bit, Steam running. Everyone you play
  with needs this same version.

INSTALL
  1. Close Overgrowth.
  2. Run install.bat. It finds Overgrowth through Steam; if it can't:
       install.bat "D:\SteamLibrary\steamapps\common\Overgrowth"
  3. Start Overgrowth from Steam as usual.

  The installer replaces Overgrowth.exe with a small launcher and keeps
  the original as Overgrowth_stock.exe. Hold Shift while the game starts
  to get the normal game. Overgrowth Online keeps its own settings and mods
  in Documents\Wolfire\OvergrowthOnline; your normal game is not touched.

MANUAL INSTALL (if install.bat doesn't work)
  1. Close Overgrowth. Right-click the downloaded zip > Properties, tick
     Unblock, OK; then extract it.
  2. Find the game folder: in Steam, right-click Overgrowth > Manage >
     Browse local files.
  3. Copy the OvergrowthOnline folder from this package into the game
     folder (replace an older one; delete an old OverhaulOnline folder).
  4. In the game folder: if there is no Overgrowth_stock.exe yet, rename
     Overgrowth.exe to Overgrowth_stock.exe. It must stay the real game
     (about 13 MB), never the small launcher. Then copy
     launcher\Overgrowth.exe from this package into the game folder.
  5. Copy mod\overhaul from this package to
     Documents\Wolfire\OvergrowthOnline\Data\Mods\overhaul (make the
     folders; replace an older overhaul folder).
  6. Optional, first time only: copy your normal settings,
     Documents\Wolfire\Overgrowth\Data\config.txt, to
     Documents\Wolfire\OvergrowthOnline\Data\config.txt.
  7. Start Overgrowth from Steam. In Main menu > Mods, make sure Overhaul
     is enabled.

PLAYING
  Main menu > Multiplayer. Connecting by IP needs UDP port 25744 open on
  the host, and shares your IP address with them.

VULKAN (TEST)
  Settings > Graphics > Renderer switches between OpenGL (the default) and
  Vulkan (restart the game after changing it). Vulkan is new: it needs a
  graphics card and driver with Vulkan 1.3. If it can't start, or the game
  dies while it starts, the next start uses OpenGL again. The first time a
  level is loaded with Vulkan its shaders are compiled, which adds a few
  seconds; later loads use the saved results.

MODS
  Other mods are not guaranteed to work online. The Multiplayer menu opens
  only when every enabled mod declares online support; most Workshop mods
  don't, so disable them to play online. Everyone in a game needs the same
  mods, in the same versions. Mods that replace the character scripts
  (aschar.as, enemycontrol.as) break online play. The Overhaul mod that
  comes with it is not required, but everyone must have it on or off alike.

UNINSTALL
  Close Overgrowth and run uninstall.bat. Steam's "Verify integrity of game
  files" also restores the original game.
  By hand: in the game folder delete Overgrowth.exe (the launcher) and
  the OvergrowthOnline folder, and rename Overgrowth_stock.exe back to
  Overgrowth.exe. Documents\Wolfire\OvergrowthOnline holds its settings and
  mods; delete it too if you don't want them.

REPORTING BUGS
  github.com/crxhvrd/overgrowth-online/issues
  Say what happened and whether you were hosting, and attach
  Documents\Wolfire\OvergrowthOnline\logfile.txt. After a crash, also the
  newest crash_* files (.dmp and .txt) from
  Documents\Wolfire\OvergrowthOnline\crashes.
