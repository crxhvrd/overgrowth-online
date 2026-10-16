"""Phase 6 (applied by hand, recorded here): on clients a loose item's drawn
transform followed the host's stream (ItemUpdate), but its physics body was
simulated locally: a character walking into a sword moved the body only
there (0.6 m apart in arena_pickups), and pick-ups and collisions look at the
body. ItemObject::Update now also places the body at each streamed pose on clients
(asleep, not simulated: the host runs loose items), as the inverse of Draw's
host transform (body - R * display_phys_offset_); SetPhysicsTransform takes
the editor's transform (phys_offset_ = old_center + display offset: 0.33 m
off along a sword)."""
