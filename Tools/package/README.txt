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

PLAYING
  Main menu > Multiplayer. Connecting by IP needs UDP port 25744 open on
  the host, and shares your IP address with them.

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

REPORTING BUGS
  github.com/crxhvrd/overgrowth-online/issues
  Say what happened and whether you were hosting, and attach
  Documents\Wolfire\OvergrowthOnline\logfile.txt. After a crash, also the
  newest OvergrowthOnline.exe.*.dmp from %LOCALAPPDATA%\CrashDumps.
