"""Online arena: guests' camera behind the map and no control (friend test).

The arenas have no characters. The host's own is made by online_arena.as in
Init, during the load, and ASCreateObject shares script objects only once the
level has started (host_started_level), so guests never got a CreateEntity for
it: no id translation, and GetObjectID's fallback (the same number) sent the
host character's poses, team and scoreboard id to whatever guest object had
that number. Meanwhile a guest's AvatarControlManager, finding no controllable
character before its own arrived, made the single-player stand-in (a Turner
at the level camera: behind the map) and took it. Order decided which: in the
tests the stand-in got the host's number and passed for the host's character;
the friend's own character got it ("Dreamt (you)" twice on the scoreboard).
- AvatarControlManager: no stand-in on a client (its character comes from the
  host).
- Online_AnnounceObject(id, path) (applied with the path by hand after this): the host's level script shares what it made while
  loading (CreateEntity, kept for late joiners); online_arena.as announces its
  own character before setting its team.
Run from the repo root."""
S = "Source/"
D = "Data/Scripts/"


def patch(p, pairs):
    s = open(p, encoding="utf-8").read()
    for o, n in pairs:
        assert s.count(o) == 1, (p, o[:90], s.count(o))
        s = s.replace(o, n)
    open(p, "w", encoding="utf-8", newline="").write(s)


patch(S + "Game/avatar_control_manager.cpp", [
("""        // If there is no controllable avatars in the scene, create one at the camera position and use it.
        if (engine->num_avatars == 0) {""",
 """        // If there is no controllable avatars in the scene, create one at the camera position and use it.
        // Overhaul Online: not on a client, its character comes from the host (arenas have none at first)
        if (engine->num_avatars == 0 && !online->IsClient()) {"""),
])

patch(S + "Scripting/angelscript/asfuncs.cpp", [
("""int ASCreateObject2(const std::string& path) {""",
 """// Overhaul Online (host): share an object a level script made while the level
// loaded (ASCreateObject shares only after the level started), so clients and
// late joiners have it under its id
void AS_Online_AnnounceObject(int id) {
    Online* online = Online::Instance();
    Object* obj = the_scenegraph->GetObjectFromID(id);
    if (online->IsHosting() && !online->host_started_level && obj != nullptr &&
        (obj->GetType() == _movement_object || obj->GetType() == _item_object)) {
        online->Send<OnlineMessages::CreateEntity>(obj->obj_file, obj->GetTranslation(), id);
    }
}

int ASCreateObject2(const std::string& path) {"""),
("""    context->RegisterGlobalFunction("void Online_PlaceAvatar(int char_id, const vec3 &in pos, const vec3 &in facing)", asFUNCTION(AS_Online_PlaceAvatar), asCALL_CDECL);""",
 """    context->RegisterGlobalFunction("void Online_PlaceAvatar(int char_id, const vec3 &in pos, const vec3 &in facing)", asFUNCTION(AS_Online_PlaceAvatar), asCALL_CDECL);
    context->RegisterGlobalFunction("void Online_AnnounceObject(int id)", asFUNCTION(AS_Online_AnnounceObject), asCALL_CDECL);"""),
])

patch(D + "online_arena.as", [
("""        obj.SetRotation(quaternion(r.x, r.y, r.z, r.a));
        obj.SetPlayer(true);
        ScriptParams@ params = obj.GetScriptParams();""",
 """        obj.SetRotation(quaternion(r.x, r.y, r.z, r.a));
        obj.SetPlayer(true);
        Online_AnnounceObject(id);  // made while loading: guests get it now, before its team
        ScriptParams@ params = obj.GetScriptParams();"""),
])
print("ok")
