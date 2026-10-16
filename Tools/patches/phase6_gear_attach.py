"""Phase 6: gear on other players (backpacks and other env objects attached to
a character's bones) floats beside them.

Wolfire moved attached env objects with the bones on the host only; clients
took the host's transforms (EnvObjectUpdate, on the host's tick, applied one
per frame). Those come from the host's picture of the character, at another
time than the one the client draws (the playback buffer; for a client's own
character the host's picture is a trip behind), so the gear drifted off the
body when anyone moved: a pack beside the head.
- Every machine places attached gear from the bones it draws (the stored
  rel_mat comes with the level, so clients have it): MovementObject and
  RiggedObject drop their "not on a client" guards.
- The host no longer sends gear (an env object whose parent is a character),
  and a client ignores any it gets.
Run from the repo root."""
S = "Source/"


def patch(p, pairs):
    s = open(S + p, encoding="utf-8").read()
    for o, n in pairs:
        assert s.count(o) == 1, (p, o[:90], s.count(o))
        s = s.replace(o, n)
    open(S + p, "w", encoding="utf-8", newline="").write(s)


patch("Objects/riggedobject.cpp", [
("""                    if (!online->IsClient()) {
                        obj->SetTranslation(total_mat.GetTranslationPart());
                        obj->SetRotation(QuaternionFromMat4(total_mat.GetRotationPart()));
                    }""",
 """                    // Overhaul Online: everywhere, from the bones drawn here (the
                    // host's transforms came at another time: gear floated off)
                    obj->SetTranslation(total_mat.GetTranslationPart());
                    obj->SetRotation(QuaternionFromMat4(total_mat.GetRotationPart()));"""),
])

patch("Objects/movementobject.cpp", [
("""    // Update attached objects

    if (!online->IsClient()) {
        int num_attached_env_objects = rigged_object_->children.size();""",
 """    // Update attached objects (Overhaul Online: on clients too, RiggedObject places them)

    {
        int num_attached_env_objects = rigged_object_->children.size();"""),
])

patch("Objects/envobject.cpp", [
("""    if (online->IsClient()) {
        list<OnlineMessageRef>& frames = incoming_online_env_update;
        if (frames.size() > 0) {""",
 """    if (online->IsClient()) {
        list<OnlineMessageRef>& frames = incoming_online_env_update;
        if (parent != nullptr && parent->GetType() == _movement_object) {
            frames.clear();  // Overhaul Online: gear follows its character's bones here
        } else if (frames.size() > 0) {"""),
])

patch("Online/online.cpp", [
("""                    case _env_object:
                        if (o->online_transform_dirty) {""",
 """                    case _env_object:
                        // (gear on a character follows its bones everywhere)
                        if (o->online_transform_dirty && (o->parent == nullptr || o->parent->GetType() != _movement_object)) {"""),
])
print("ok")
# Follow-up (applied by hand): a proxy's bones come from the stream, which set
# only the scaled draw matrices; the gear's frames (display_bone_transforms)
# stayed at the level-start pose, so gear stayed behind on clients (12 m in
# coop_gear). RiggedObject::NetworkAttachMatrix builds them from each streamed
# bone (unscaled, as PreDrawFrame does), set in MovementObject::ApplyNetworkPose.
