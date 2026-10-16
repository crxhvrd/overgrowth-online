"""Phase 6 (applied by hand, recorded here): held and stuck items were drawn
in the wrong place on clients. ItemObject::Draw refreshes its transform from
the physics body only off clients; a client's transform came from the host's
ItemUpdate stream, which stops for held/stuck items (they follow their
character), while the character's attachment code moves the body. So on a
client a held weapon stayed drawn where it lay (friend: arena weapons
invisible on the guest's screen). Draw now uses the body for held and stuck
items on clients too. Test bridge: items report draw_pos."""
