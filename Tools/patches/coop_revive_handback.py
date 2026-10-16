"""Co-op revive (applied by hand, recorded here): Online::UpdateTakeOvers
handed a taken-over character back the moment it was free and settled. A
teammate's revive (coop_revive.as) gets the body up on the dragger's machine
and lets go: it was handed back at once, before the "awake" vitals reached
its machine, which took it back down. A body is now handed back after it has
been free and settled for a second (OwnerOverrideEntry::settled_ms)."""
