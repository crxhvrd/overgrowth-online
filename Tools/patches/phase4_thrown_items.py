"""Phase 4 (combat), part 4: thrown weapons. Items stay host-simulated (the
host is the item authority): a client's throw goes to the host as a launch
(transform, velocity, spin, thrower) and flies there; a thrown item that hits
a character another machine runs is resolved by that machine (HitByItem),
and a weapon that sticks is stuck in the victim on every machine. Run from the
repo root."""
S = "Source/"


def patch(p, pairs):
    s = open(S + p, encoding="utf-8").read()
    for o, n in pairs:
        assert s.count(o) == 1, (p, o[:90], s.count(o))
        s = s.replace(o, n)
    open(S + p, "w", encoding="utf-8", newline="").write(s)


patch("Online/Message/combat_messages.h", [
("""// A character handed to another machine for a throw, choke or drag (active),""", """// A client threw an item: the host flies it from here on
class ItemLaunch : public OnlineMessageBase {
   public:
    CommonObjectID item = -1;
    CommonObjectID thrower = -1;
    vec3 position;
    vec3 rotation_x, rotation_y;  // the transform's first two columns
    vec3 linear_velocity;
    vec3 angular_velocity;
    bool straight = false;

    ItemLaunch();

    static binn* Serialize(void* object);
    static void Deserialize(void* object, binn* l);
    static void Execute(const OnlineMessageRef& ref, void* object, PeerID peer);
    static void* Construct(void* mem);
    static void Destroy(void* object);
};

// The host's thrown item hit a character: everyone sticks it in (type 2);
// the character's owner takes the hit
class ItemHit : public OnlineMessageBase {
   public:
    CommonObjectID character = -1;
    CommonObjectID item = -1;
    vec3 point;
    std::string material;
    int32_t type = 0;  // 0 blunt, 1 cut, 2 stuck
    vec3 velocity;
    vec3 stick_start, stick_end;
    vec3 stick_pos, stick_x, stick_y;

    ItemHit();

    static binn* Serialize(void* object);
    static void Deserialize(void* object, binn* l);
    static void Execute(const OnlineMessageRef& ref, void* object, PeerID peer);
    static void* Construct(void* mem);
    static void Destroy(void* object);
};

// A character handed to another machine for a throw, choke or drag (active),"""),
])

patch("Online/Message/combat_messages.cpp", [
("""OwnerOverride::OwnerOverride() : OnlineMessageBase(OnlineMessageCategory::LEVEL_TRANSIENT) {
}""", """ItemLaunch::ItemLaunch() : OnlineMessageBase(OnlineMessageCategory::LEVEL_TRANSIENT) {
}

binn* ItemLaunch::Serialize(void* object) {
    ItemLaunch* m = static_cast<ItemLaunch*>(object);
    binn* l = binn_object();
    binn_object_set_int32(l, "i", m->item);
    binn_object_set_int32(l, "t", m->thrower);
    binn_object_set_vec3(l, "p", m->position);
    binn_object_set_vec3(l, "x", m->rotation_x);
    binn_object_set_vec3(l, "y", m->rotation_y);
    binn_object_set_vec3(l, "v", m->linear_velocity);
    binn_object_set_vec3(l, "w", m->angular_velocity);
    binn_object_set_bool(l, "s", m->straight);
    return l;
}

void ItemLaunch::Deserialize(void* object, binn* l) {
    ItemLaunch* m = static_cast<ItemLaunch*>(object);
    BOOL straight = FALSE;
    binn_object_get_int32(l, "i", &m->item);
    binn_object_get_int32(l, "t", &m->thrower);
    binn_object_get_vec3(l, "p", &m->position);
    binn_object_get_vec3(l, "x", &m->rotation_x);
    binn_object_get_vec3(l, "y", &m->rotation_y);
    binn_object_get_vec3(l, "v", &m->linear_velocity);
    binn_object_get_vec3(l, "w", &m->angular_velocity);
    binn_object_get_bool(l, "s", &straight);
    m->straight = straight != FALSE;
}

void ItemLaunch::Execute(const OnlineMessageRef& ref, void* object, PeerID peer) {
    Online::Instance()->ReceiveItemLaunch(static_cast<ItemLaunch*>(object), peer);
}

void* ItemLaunch::Construct(void* mem) {
    return new (mem) ItemLaunch();
}

void ItemLaunch::Destroy(void* object) {
    static_cast<ItemLaunch*>(object)->~ItemLaunch();
}

ItemHit::ItemHit() : OnlineMessageBase(OnlineMessageCategory::LEVEL_TRANSIENT) {
}

binn* ItemHit::Serialize(void* object) {
    ItemHit* m = static_cast<ItemHit*>(object);
    binn* l = binn_object();
    binn_object_set_int32(l, "c", m->character);
    binn_object_set_int32(l, "i", m->item);
    binn_object_set_vec3(l, "p", m->point);
    binn_object_set_std_string(l, "m", m->material);
    binn_object_set_int32(l, "t", m->type);
    binn_object_set_vec3(l, "v", m->velocity);
    binn_object_set_vec3(l, "ss", m->stick_start);
    binn_object_set_vec3(l, "se", m->stick_end);
    binn_object_set_vec3(l, "sp", m->stick_pos);
    binn_object_set_vec3(l, "sx", m->stick_x);
    binn_object_set_vec3(l, "sy", m->stick_y);
    return l;
}

void ItemHit::Deserialize(void* object, binn* l) {
    ItemHit* m = static_cast<ItemHit*>(object);
    binn_object_get_int32(l, "c", &m->character);
    binn_object_get_int32(l, "i", &m->item);
    binn_object_get_vec3(l, "p", &m->point);
    binn_object_get_std_string(l, "m", &m->material);
    binn_object_get_int32(l, "t", &m->type);
    binn_object_get_vec3(l, "v", &m->velocity);
    binn_object_get_vec3(l, "ss", &m->stick_start);
    binn_object_get_vec3(l, "se", &m->stick_end);
    binn_object_get_vec3(l, "sp", &m->stick_pos);
    binn_object_get_vec3(l, "sx", &m->stick_x);
    binn_object_get_vec3(l, "sy", &m->stick_y);
}

void ItemHit::Execute(const OnlineMessageRef& ref, void* object, PeerID peer) {
    Online::Instance()->ReceiveItemHit(static_cast<ItemHit*>(object));
}

void* ItemHit::Construct(void* mem) {
    return new (mem) ItemHit();
}

void ItemHit::Destroy(void* object) {
    static_cast<ItemHit*>(object)->~ItemHit();
}

OwnerOverride::OwnerOverride() : OnlineMessageBase(OnlineMessageCategory::LEVEL_TRANSIENT) {
}"""),
])

patch("Online/online.h", [
("""    HitResult,
    OwnerOverride>""", """    HitResult,
    OwnerOverride,
    ItemLaunch,
    ItemHit>"""),
("""    void UpdateTakeOvers(SceneGraph* scenegraph);""", """    void UpdateTakeOvers(SceneGraph* scenegraph);

    // Thrown weapons: the host flies them; hits on characters run elsewhere go to their owners
    vector<ObjectID> pending_item_launches;  // client, main thread: sent at the end of the step
    std::atomic<uint64_t> item_launches{0};
    std::atomic<uint64_t> item_hits{0};
    void QueueItemLaunch(ObjectID item);
    void SendPendingItemLaunches(SceneGraph* scenegraph);
    void ReceiveItemLaunch(OnlineMessages::ItemLaunch* m, PeerID from);
    void SendItemHit(ObjectID character, ObjectID item, const vec3& point, const string& material, int type, const vec3& velocity,
                     const vec3& stick_start, const vec3& stick_end, const mat4& stick_transform);
    void ReceiveItemHit(OnlineMessages::ItemHit* m);"""),
])

patch("Online/online.cpp", [
("""    } else if (IsClient()) {
        // Ownership: the host does not steer our character by our camera""", """    } else if (IsClient()) {
        SendPendingItemLaunches(scenegraph_);

        // Ownership: the host does not steer our character by our camera"""),
("""vector<ObjectID> Online::GetLocalAvatarIDs() const {""", r"""// ---- thrown weapons ---------------------------------------------------------------

void Online::QueueItemLaunch(ObjectID item) {
    if (IsClient() && std::find(pending_item_launches.begin(), pending_item_launches.end(), item) == pending_item_launches.end()) {
        pending_item_launches.push_back(item);
    }
}

// Client, end of the step: the throw's velocity and spin are set by now
void Online::SendPendingItemLaunches(SceneGraph* sg) {
    for (ObjectID id : pending_item_launches) {
        Object* o = sg->GetObjectFromID(id);
        if (o == nullptr || o->GetType() != _item_object) {
            continue;
        }

        ItemObject* item = static_cast<ItemObject*>(o);
        if (item->IsHeld()) {
            continue;
        }

        OnlineMessageRef ref = message_handler.Create<OnlineMessages::ItemLaunch>();
        OnlineMessages::ItemLaunch* m = static_cast<OnlineMessages::ItemLaunch*>(ref.GetData());
        mat4 t = item->GetPhysicsTransform();
        m->item = GetOriginalID(id);
        m->thrower = item->last_held_char_id_ == -1 ? -1 : GetOriginalID(item->last_held_char_id_);
        m->position = t.GetTranslationPart();
        m->rotation_x = t.GetColumnVec3(0);
        m->rotation_y = t.GetColumnVec3(1);
        m->linear_velocity = item->GetLinearVelocity();
        m->angular_velocity = item->GetAngularVelocity();
        m->straight = item->IsThrownStraight();
        Send(ref);
        ++item_launches;
    }

    pending_item_launches.clear();
}

void Online::ReceiveItemLaunch(OnlineMessages::ItemLaunch* m, PeerID from) {
    if (!IsHosting() || !host_started_level) {
        return;
    }

    SceneGraph* sg = Engine::Instance()->GetSceneGraph();
    Object* o = sg != nullptr ? sg->GetObjectFromID(GetObjectID(m->item)) : nullptr;
    if (o == nullptr || o->GetType() != _item_object) {
        return;
    }

    ItemObject* item = static_cast<ItemObject*>(o);
    if (item->IsHeld()) {
        return;  // someone here holds it: the drop never reached us, or it was taken back
    }

    vec3 z = normalize(cross(m->rotation_x, m->rotation_y));
    mat4 t;
    t.SetColumn(0, m->rotation_x);
    t.SetColumn(1, m->rotation_y);
    t.SetColumn(2, z);
    t.SetTranslationPart(m->position);
    item->SetPhysicsTransform(t);
    item->SetLinearVelocity(m->linear_velocity);
    item->SetAngularVelocity(m->angular_velocity);
    if (m->thrower != -1) {
        item->SetHolderID(GetObjectID(m->thrower));
    }
    if (m->straight) {
        item->SetThrownStraight();
    } else {
        item->SetThrown();
    }
    ++item_launches;
}

// Host: our thrown item hit a character. Everyone sticks it in; its owner takes the hit.
void Online::SendItemHit(ObjectID character, ObjectID item, const vec3& point, const string& material, int type, const vec3& velocity,
                         const vec3& stick_start, const vec3& stick_end, const mat4& stick_transform) {
    if (!IsHosting()) {
        return;
    }

    OnlineMessageRef ref = message_handler.Create<OnlineMessages::ItemHit>();
    OnlineMessages::ItemHit* m = static_cast<OnlineMessages::ItemHit*>(ref.GetData());
    m->character = GetOriginalID(character);
    m->item = GetOriginalID(item);
    m->point = point;
    m->material = material;
    m->type = type;
    m->velocity = velocity;
    m->stick_start = stick_start;
    m->stick_end = stick_end;
    m->stick_pos = stick_transform.GetTranslationPart();
    m->stick_x = stick_transform.GetColumnVec3(0);
    m->stick_y = stick_transform.GetColumnVec3(1);
    Send(ref);
    ++item_hits;
}

void Online::ReceiveItemHit(OnlineMessages::ItemHit* m) {
    SceneGraph* sg = Engine::Instance()->GetSceneGraph();
    if (sg == nullptr || !host_started_level) {
        return;
    }

    ObjectID char_id = GetObjectID(m->character);
    ObjectID item_id = GetObjectID(m->item);
    Object* co = sg->GetObjectFromID(char_id);
    Object* io = sg->GetObjectFromID(item_id);
    if (co == nullptr || io == nullptr || co->GetType() != _movement_object || io->GetType() != _item_object) {
        return;
    }

    MovementObject* mo = static_cast<MovementObject*>(co);
    ItemObject* item = static_cast<ItemObject*>(io);

    if (m->type == 2 && !item->IsHeld() && item->StuckInWhom() == -1) {
        vec3 z = normalize(cross(m->stick_x, m->stick_y));
        mat4 t;
        t.SetColumn(0, m->stick_x);
        t.SetColumn(1, m->stick_y);
        t.SetColumn(2, z);
        t.SetTranslationPart(m->stick_pos);
        mo->rigged_object()->StickItem(item_id, m->stick_start, m->stick_end, t);
    }

    if (IsLocalAvatar(char_id)) {
        item->SetLinearVelocity(m->velocity);  // its script weighs the blow by the item's speed
        mo->HitByItem(item_id, m->point, m->material, m->type);
        ++item_hits;
    }
}

vector<ObjectID> Online::GetLocalAvatarIDs() const {"""),
])

# Items: a client's throw is queued for the host; the host's hits on characters
# run elsewhere go to their owners
patch("Objects/itemobject.h", [
("""    void SetPhysicsTransform(const mat4& transform);""", """    void SetPhysicsTransform(const mat4& transform);
    bool IsThrownStraight() const { return thrown_ == kThrownStraight; }  // Overhaul Online"""),
])

patch("Objects/itemobject.cpp", [
("""void ItemObject::SetThrown() {
    thrown_ = kThrown;""", """void ItemObject::SetThrown() {
    thrown_ = kThrown;
    Online::Instance()->QueueItemLaunch(GetID());  // Overhaul Online: a client's throw flies on the host"""),
("""void ItemObject::SetThrownStraight() {
    thrown_ = kThrownStraight;""", """void ItemObject::SetThrownStraight() {
    thrown_ = kThrownStraight;
    Online::Instance()->QueueItemLaunch(GetID());"""),
("""        if (thrown_ != kSafe) {
            int accept_hit = 1;
            {
                accept_hit = mo->AboutToBeHitByItem(GetID());
            }""", """        if (thrown_ != kSafe) {
            int accept_hit = 1;
            // Overhaul Online: a character another machine runs takes the hit there
            bool remote_victim = mo->network_proxy && Online::Instance()->IsActive();
            vec3 stick_start, stick_end;
            mat4 stick_at;
            if (!remote_victim) {
                accept_hit = mo->AboutToBeHitByItem(GetID());
            }"""),
("""                        if (bone != -1) {
                            // stick_transform = mo->rigged_object()->skeleton_.physics_bones[bone].bullet_object->GetTransform();
                            mo->rigged_object()->StickItem(GetID(), start, end, stick_transform);
                        }""", """                        if (bone != -1) {
                            // stick_transform = mo->rigged_object()->skeleton_.physics_bones[bone].bullet_object->GetTransform();
                            mo->rigged_object()->StickItem(GetID(), start, end, stick_transform);
                        }
                        stick_start = start;
                        stick_end = end;
                        stick_at = stick_transform;"""),
("""                mo->HitByItem(GetID(), point, item_ref_->GetLineMaterial(line_id), type);""",
 """                if (Online::Instance()->IsHosting()) {
                    Online::Instance()->SendItemHit(mo->GetID(), GetID(), point, item_ref_->GetLineMaterial(line_id), type,
                                                    GetLinearVelocity(), stick_start, stick_end, stick_at);
                }
                if (!remote_victim) {
                    mo->HitByItem(GetID(), point, item_ref_->GetLineMaterial(line_id), type);
                }"""),
])

patch("Testing/test_bridge.cpp", [
("""        o["takeovers"] = (Json::UInt64)online->takeovers.load();""",
 """        o["takeovers"] = (Json::UInt64)online->takeovers.load();
        o["item_launches"] = (Json::UInt64)online->item_launches.load();
        o["item_hits"] = (Json::UInt64)online->item_hits.load();"""),
])
print("ok")
