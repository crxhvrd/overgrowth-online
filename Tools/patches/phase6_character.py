"""Phase 6, part 5: your online character (co-op).

- The player's choice lives in the config (the co-op menu writes it):
  mp_character = a character file ("" = the level's own / the default),
  mp_fur / mp_cloth = "r g b" ("" = the character's own colours).
- Once per level each machine puts its player's choice on its own
  character: the stock SwitchCharacter, then the palette channels the
  character calls "fur" and "cloth" (what RandomizeColors uses).
- A character whose file changes on the machine that runs it is announced
  (AvatarCharacter, level persistent). Elsewhere the proxy is rebuilt as that
  character (without SwitchCharacter's random colours, which would be sent
  from every machine). The host applies a client's and sends it on (stored
  for late joiners).
- The host now passes a client's palette on the same way (Send: kept for
  late joiners) instead of relaying it.
Run from the repo root."""
S = "Source/"


def patch(p, pairs):
    s = open(S + p, encoding="utf-8").read()
    for o, n in pairs:
        assert s.count(o) == 1, (p, o[:90], s.count(o))
        s = s.replace(o, n)
    open(S + p, "w", encoding="utf-8", newline="").write(s)


# ---- the message ---------------------------------------------------------------------
patch("Online/Message/combat_messages.h", [
("""// The host settled a race for an item: the named character lets go of it
class ItemDrop : public OnlineMessageBase {""", """// A character's file changed on the machine that runs it (a player's chosen
// character); level persistent, so late joiners get it too
class AvatarCharacter : public OnlineMessageBase {
   public:
    CommonObjectID character = -1;
    std::string path;

    AvatarCharacter();
    AvatarCharacter(CommonObjectID character, const std::string& path);

    static binn* Serialize(void* object);
    static void Deserialize(void* object, binn* l);
    static void Execute(const OnlineMessageRef& ref, void* object, PeerID peer);
    static void* Construct(void* mem);
    static void Destroy(void* object);
};

// The host settled a race for an item: the named character lets go of it
class ItemDrop : public OnlineMessageBase {"""),
])

patch("Online/Message/combat_messages.cpp", [
("""ItemDrop::ItemDrop() : OnlineMessageBase(OnlineMessageCategory::LEVEL_TRANSIENT) {
}""", """AvatarCharacter::AvatarCharacter() : OnlineMessageBase(OnlineMessageCategory::LEVEL_PERSISTENT) {
}

AvatarCharacter::AvatarCharacter(CommonObjectID character, const std::string& path)
    : OnlineMessageBase(OnlineMessageCategory::LEVEL_PERSISTENT), character(character), path(path) {
}

binn* AvatarCharacter::Serialize(void* object) {
    AvatarCharacter* m = static_cast<AvatarCharacter*>(object);
    binn* l = binn_object();
    binn_object_set_int32(l, "c", m->character);
    binn_object_set_std_string(l, "p", m->path);
    return l;
}

void AvatarCharacter::Deserialize(void* object, binn* l) {
    AvatarCharacter* m = static_cast<AvatarCharacter*>(object);
    binn_object_get_int32(l, "c", &m->character);
    binn_object_get_std_string(l, "p", &m->path);
}

void AvatarCharacter::Execute(const OnlineMessageRef& ref, void* object, PeerID peer) {
    AvatarCharacter* m = static_cast<AvatarCharacter*>(object);
    Online::Instance()->ReceiveAvatarCharacter(m->character, m->path);
}

void* AvatarCharacter::Construct(void* mem) {
    return new (mem) AvatarCharacter();
}

void AvatarCharacter::Destroy(void* object) {
    static_cast<AvatarCharacter*>(object)->~AvatarCharacter();
}

ItemDrop::ItemDrop() : OnlineMessageBase(OnlineMessageCategory::LEVEL_TRANSIENT) {
}"""),
])

# ---- Online ------------------------------------------------------------------------
patch("Online/online.h", [
("""    ItemHit,
    ItemDrop>""", """    ItemHit,
    ItemDrop,
    AvatarCharacter>"""),
("""    void ReceiveItemDrop(CommonObjectID character, CommonObjectID item);""",
 """    void ReceiveItemDrop(CommonObjectID character, CommonObjectID item);

    // Your online character (co-op menu: mp_character, mp_fur, mp_cloth)
    void SendAvatarCharacter(ObjectID character, const string& path);
    void ReceiveAvatarCharacter(CommonObjectID character, const string& path);  // queued to LateUpdate
    void ApplyOwnCharacter(SceneGraph* sg);  // once per level, on our own character
    vector<std::pair<CommonObjectID, string> > pending_avatar_characters;
    string own_character_applied;  // "<level>|<character id>\""""),
])

patch("Online/online.cpp", [
# The host keeps a client's palette for late joiners: Send (stored), not a relay
("""                    type == message_handler.GetMessageType<OnlineMessages::MaterialSoundEvent>() ||
                    type == message_handler.GetMessageType<OnlineMessages::SetAvatarPalette>()) {
                    RelayFromPeer(it.message, it.from);
                    ++sounds_from_clients;
                }""", """                    type == message_handler.GetMessageType<OnlineMessages::MaterialSoundEvent>()) {
                    RelayFromPeer(it.message, it.from);
                    ++sounds_from_clients;
                } else if (type == message_handler.GetMessageType<OnlineMessages::SetAvatarPalette>()) {
                    Send(it.message);  // to everyone, and kept for late joiners (its sender ignores its own)
                }"""),
# LateUpdate: queued characters, our own choice
("""        CoopUpdate(scenegraph);
""", """        CoopUpdate(scenegraph);

        // Characters: the ones announced, then our own choice
        for (const auto& c : pending_avatar_characters) {
            ObjectID id = GetObjectID(c.first);
            Object* o = scenegraph != nullptr ? scenegraph->GetObjectFromID(id) : nullptr;
            if (o != nullptr && o->GetType() == _movement_object) {
                MovementObject* mo = static_cast<MovementObject*>(o);
                if (mo->network_proxy) {
                    mo->ApplyNetworkCharacter(c.second);
                }
            }
            if (IsHosting()) {
                Send<OnlineMessages::AvatarCharacter>(c.first, c.second);  // on to everyone, kept for late joiners
            }
        }
        pending_avatar_characters.clear();
        ApplyOwnCharacter(scenegraph);
"""),
("""// ---- Co-op rules (story campaigns) -------------------------------------------""",
 """// ---- Your online character -----------------------------------------------------

void Online::SendAvatarCharacter(ObjectID character, const string& path) {
    if (IsActive() && host_started_level) {
        Send<OnlineMessages::AvatarCharacter>(GetOriginalID(character), path);
    }
}

void Online::ReceiveAvatarCharacter(CommonObjectID character, const string& path) {
    pending_avatar_characters.push_back(std::make_pair(character, path));
}

static bool ParseColor(const string& s, vec3* c) {
    float r = 0.0f, g = 0.0f, b = 0.0f;
    if (sscanf(s.c_str(), "%f %f %f", &r, &g, &b) != 3) {
        return false;
    }
    *c = vec3(std::max(0.0f, std::min(1.0f, r)), std::max(0.0f, std::min(1.0f, g)), std::max(0.0f, std::min(1.0f, b)));
    return true;
}

void Online::ApplyOwnCharacter(SceneGraph* sg) {
    vector<ObjectID> mine = GetLocalAvatarIDs();
    if (sg == nullptr || !host_started_level || mine.empty()) {
        return;
    }
    Object* o = sg->GetObjectFromID(mine[0]);
    if (o == nullptr || o->GetType() != _movement_object) {
        return;
    }
    MovementObject* mo = static_cast<MovementObject*>(o);
    string key = level_name + "|" + to_string((int)mine[0]);
    if (mo->network_proxy || key == own_character_applied) {
        return;
    }
    own_character_applied = key;

    // The character (stock aschar.as SwitchCharacter: drops weapons, rebuilds, random colours)
    string path = config["mp_character"].str();
    if (!path.empty() && path != mo->character_path && FileExists(path, kAnyPath) &&
        mo->HasFunction("void SwitchCharacter(string)")) {
        LOGI << "Overhaul Online: playing as " << path << std::endl;
        mo->Execute("SwitchCharacter(\\"" + path + "\\");");
    }

    // The colours, by what each palette channel is (fur, cloth)
    vec3 fur, cloth;
    bool has_fur = ParseColor(config["mp_fur"].str(), &fur);
    bool has_cloth = ParseColor(config["mp_cloth"].str(), &cloth);
    if ((has_fur || has_cloth) && mo->HasFunction("void RandomizeColors()")) {
        string code = "Object@ obj = ReadObjectFromID(this_mo.GetID()); for(int i = 0; i < 4; ++i) { string ch = character_getter.GetChannel(i);";
        if (has_fur) {
            code += " if(ch == \\"fur\\") { obj.SetPaletteColor(i, vec3(" + to_string(fur.x()) + ", " + to_string(fur.y()) + ", " + to_string(fur.z()) + ")); }";
        }
        if (has_cloth) {
            code += " if(ch == \\"cloth\\") { obj.SetPaletteColor(i, vec3(" + to_string(cloth.x()) + ", " + to_string(cloth.y()) + ", " + to_string(cloth.z()) + ")); }";
        }
        mo->Execute(code + " }");
    }
}

// ---- Co-op rules (story campaigns) -------------------------------------------"""),
])

# ---- MovementObject ---------------------------------------------------------------------
patch("Objects/movementobject.h", [
("""    void ApplyNetworkVitals(const CharSnapshot::Vitals& v);""", """    void ApplyNetworkVitals(const CharSnapshot::Vitals& v);
    // Overhaul Online: become the character the owner switched to (no random colours)
    void ApplyNetworkCharacter(const std::string& path);"""),
])

patch("Objects/movementobject.cpp", [
("""void MovementObject::RecreateRiggedObject(std::string _char_path) {
    character_path = _char_path;""", """void MovementObject::RecreateRiggedObject(std::string _char_path) {
    // Overhaul Online: the machine that runs it tells the others it changed character
    if (!network_proxy && !character_path.empty() && _char_path != character_path) {
        Online::Instance()->SendAvatarCharacter(GetID(), _char_path);
    }
    character_path = _char_path;"""),
("""// Overhaul Online: the script variables that carry a character's game state""",
 """void MovementObject::ApplyNetworkCharacter(const std::string& path) {
    if (path == character_path) {
        return;
    }
    if (HasFunction("void CheckSpecies()") && HasFunction("void CacheSkeletonInfo()") && HasFunction("void ResetLayers()")) {
        // aschar.as SwitchCharacter without dropping weapons (the stream says
        // what it holds) or random colours (the owner's come with its palette)
        as_context->Execute("this_mo.char_path = \\"" + path + "\\"; character_getter.Load(this_mo.char_path); CheckSpecies(); "
                            "this_mo.RecreateRiggedObject(this_mo.char_path); ResetLayers(); CacheSkeletonInfo();");
    } else {
        RecreateRiggedObject(path);
    }
}

// Overhaul Online: the script variables that carry a character's game state"""),
])
print("ok")
