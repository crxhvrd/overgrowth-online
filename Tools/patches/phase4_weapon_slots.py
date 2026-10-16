"""Phase 4 (combat), part 5: who holds what. A character's weapon slots
travel with its vitals; every other machine writes them into the proxy's
script (AI and the hit code read them) and brings the engine's attachments in
line (a pick-up, a draw, a sheathe, a drop or a throw shows everywhere, late
joiners included, lost packets heal). The host settles races: a character
claiming an item someone else already holds there is told to drop it. Run
from the repo root."""
S = "Source/"


def patch(p, pairs):
    s = open(S + p, encoding="utf-8").read()
    for o, n in pairs:
        assert s.count(o) == 1, (p, o[:90], s.count(o))
        s = s.replace(o, n)
    open(S + p, "w", encoding="utf-8", newline="").write(s)


patch("Online/char_snapshot.h", [
("""    float block_stunned = 0.0f;  // after a blocked attack: throwable by whoever blocked it
    int32_t block_stunned_by = -1;
};""", """    float block_stunned = 0.0f;  // after a blocked attack: throwable by whoever blocked it
    int32_t block_stunned_by = -1;
    uint8_t primary_slot = 1;      // aschar.as weapon_slots: held left/right, sheathed left/right, scabbards
    int32_t slots[6] = {-1, -1, -1, -1, -1, -1};  // common item ids
};"""),
("""//    uint8 block_stunned (x100 s); int32 block_stunned_by]""",
 """//    uint8 block_stunned (x100 s); int32 block_stunned_by;
//    uint8 primary slot, uint8 filled slots, per: uint8 slot, int32 item]"""),
])

patch("Online/char_snapshot.cpp", [
("""        Put<uint8_t>(out, (uint8_t)std::max(0.0f, std::min(255.0f, std::round(v.block_stunned * 100.0f))));
        Put<int32_t>(out, v.block_stunned_by);
    }""", """        Put<uint8_t>(out, (uint8_t)std::max(0.0f, std::min(255.0f, std::round(v.block_stunned * 100.0f))));
        Put<int32_t>(out, v.block_stunned_by);
        Put<uint8_t>(out, v.primary_slot);
        uint8_t filled = 0;
        for (int i = 0; i < 6; ++i) {
            filled += v.slots[i] != -1 ? 1 : 0;
        }
        Put<uint8_t>(out, filled);
        for (int i = 0; i < 6; ++i) {
            if (v.slots[i] != -1) {
                Put<uint8_t>(out, (uint8_t)i);
                Put<int32_t>(out, v.slots[i]);
            }
        }
    }"""),
("""        v.block_stunned = stun / 100.0f;""", """        v.block_stunned = stun / 100.0f;
        uint8_t filled;
        if (!Get(data, size, offset, v.primary_slot) || !Get(data, size, offset, filled) || filled > 6) {
            return false;
        }
        for (int i = 0; i < filled; ++i) {
            uint8_t slot;
            int32_t item;
            if (!Get(data, size, offset, slot) || !Get(data, size, offset, item) || slot >= 6) {
                return false;
            }
            v.slots[slot] = item;
        }"""),
])

patch("Objects/movementobject.h", [
("""    void SetNetworkProxy(bool proxy);  // owned here or shown from the owner's stream""",
 """    void SetNetworkProxy(bool proxy);  // owned here or shown from the owner's stream
    void ReconcileNetworkItems(const CharSnapshot::Vitals& v);
    double net_drop_order_ms = -1e9;"""),
])

patch("Objects/movementobject.cpp", [
("""                                      "duck_amount", "target_id", "tethered", "tether_id", "block_stunned",
                                      "block_stunned_by_id"};""", """                                      "duck_amount", "target_id", "tethered", "tether_id", "block_stunned",
                                      "block_stunned_by_id", "weapon_slots", "primary_weapon_slot"};"""),
("""    v.block_stunned = ASGetFloatVar("block_stunned");
    int stunner = ASGetIntVar("block_stunned_by_id");
    v.block_stunned_by = stunner == -1 ? -1 : online->GetOriginalID(stunner);
}""", """    v.block_stunned = ASGetFloatVar("block_stunned");
    int stunner = ASGetIntVar("block_stunned_by_id");
    v.block_stunned_by = stunner == -1 ? -1 : online->GetOriginalID(stunner);
    v.primary_slot = (uint8_t)ASGetIntVar("primary_weapon_slot");
    for (int i = 0; i < 6; ++i) {
        int item = ASGetArrayIntVar("weapon_slots", i);
        v.slots[i] = item == -1 ? -1 : online->GetOriginalID(item);
    }
}"""),
("""    ASSetFloatVar("block_stunned", v.block_stunned);
    ASSetIntVar("block_stunned_by_id", v.block_stunned_by == -1 ? -1 : online->GetObjectID(v.block_stunned_by));
""", """    ASSetFloatVar("block_stunned", v.block_stunned);
    ASSetIntVar("block_stunned_by_id", v.block_stunned_by == -1 ? -1 : online->GetObjectID(v.block_stunned_by));
    ReconcileNetworkItems(v);
"""),
("""// Overhaul Online: the pose we stream for a character we simulate""", """// Overhaul Online: a proxy holds what its owner says it holds. Slots 0-3 are
// the hands and the sheaths (aschar.as weapon_slots); anything attached here
// that the owner no longer lists is let go. The host settles races: an item
// already in someone else's hand there is not taken, its claimant's machine
// is told to drop it.
void MovementObject::ReconcileNetworkItems(const CharSnapshot::Vitals& v) {
    Online* online = Online::Instance();
    int slots[6];

    for (int i = 0; i < 6; ++i) {
        slots[i] = v.slots[i] == -1 ? -1 : online->GetObjectID(v.slots[i]);
        ASSetArrayIntVar("weapon_slots", i, slots[i]);
    }
    ASSetIntVar("primary_weapon_slot", v.primary_slot);

    // Let go of what it no longer has (stuck weapons are another list)
    std::vector<int> attached;
    for (const AttachedItem& a : rigged_object_->attached_items.items) {
        attached.push_back(a.item->GetID());
    }
    for (int id : attached) {
        if (std::find(slots, slots + 6, id) == slots + 6) {
            ASDetachItem(id);
        }
    }

    // Take up what it has: hands (grip) and sheaths
    for (int i = 0; i < 4; ++i) {
        Object* o = slots[i] == -1 ? nullptr : scenegraph_->GetObjectFromID(slots[i]);
        if (o == nullptr || o->GetType() != _item_object) {
            continue;
        }

        ItemObject* item = static_cast<ItemObject*>(o);
        bool grip = i < 2;
        bool mirrored = (i == 0) || (i == 3);  // held left; sheathed right (aschar.as HandleEditorAttachment)
        int holder = item->HeldByWhom();

        if (holder == GetID() && item->state() == (grip ? ItemObject::kWielded : ItemObject::kSheathed)) {
            continue;
        }

        if (holder != -1 && holder != GetID()) {
            // Someone here already holds it: the host's word decides
            if (online->IsHosting() && OnlineClockMs() - net_drop_order_ms > 1000.0) {
                net_drop_order_ms = OnlineClockMs();
                online->SendItemDrop(GetID(), item->GetID());
            }
            continue;
        }

        AttachItemToSlot(item->GetID(), grip ? _at_grip : _at_sheathe, mirrored);
    }
}

// Overhaul Online: the pose we stream for a character we simulate"""),
])

# --- the drop order --------------------------------------------------------------------

patch("Online/Message/combat_messages.h", [
("""// A character handed to another machine for a throw, choke or drag (active),""", """// The host settled a race for an item: the named character lets go of it
class ItemDrop : public OnlineMessageBase {
   public:
    CommonObjectID character = -1;
    CommonObjectID item = -1;

    ItemDrop();
    ItemDrop(CommonObjectID character, CommonObjectID item);

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
}""", """ItemDrop::ItemDrop() : OnlineMessageBase(OnlineMessageCategory::LEVEL_TRANSIENT) {
}

ItemDrop::ItemDrop(CommonObjectID character, CommonObjectID item)
    : OnlineMessageBase(OnlineMessageCategory::LEVEL_TRANSIENT), character(character), item(item) {
}

binn* ItemDrop::Serialize(void* object) {
    ItemDrop* m = static_cast<ItemDrop*>(object);
    binn* l = binn_object();
    binn_object_set_int32(l, "c", m->character);
    binn_object_set_int32(l, "i", m->item);
    return l;
}

void ItemDrop::Deserialize(void* object, binn* l) {
    ItemDrop* m = static_cast<ItemDrop*>(object);
    binn_object_get_int32(l, "c", &m->character);
    binn_object_get_int32(l, "i", &m->item);
}

void ItemDrop::Execute(const OnlineMessageRef& ref, void* object, PeerID peer) {
    ItemDrop* m = static_cast<ItemDrop*>(object);
    Online::Instance()->ReceiveItemDrop(m->character, m->item);
}

void* ItemDrop::Construct(void* mem) {
    return new (mem) ItemDrop();
}

void ItemDrop::Destroy(void* object) {
    static_cast<ItemDrop*>(object)->~ItemDrop();
}

OwnerOverride::OwnerOverride() : OnlineMessageBase(OnlineMessageCategory::LEVEL_TRANSIENT) {
}"""),
])

patch("Online/online.h", [
("""    ItemLaunch,
    ItemHit>""", """    ItemLaunch,
    ItemHit,
    ItemDrop>"""),
("""    void ReceiveItemHit(OnlineMessages::ItemHit* m);""", """    void ReceiveItemHit(OnlineMessages::ItemHit* m);
    void SendItemDrop(ObjectID character, ObjectID item);
    void ReceiveItemDrop(CommonObjectID character, CommonObjectID item);"""),
])

patch("Online/online.cpp", [
("""vector<ObjectID> Online::GetLocalAvatarIDs() const {""", r"""// Host: the character's machine lets go of an item someone else got first
void Online::SendItemDrop(ObjectID character, ObjectID item) {
    if (IsHosting()) {
        SendToOwnerOf(character, message_handler.Create<OnlineMessages::ItemDrop>(GetOriginalID(character), GetOriginalID(item)), false);
    }
}

void Online::ReceiveItemDrop(CommonObjectID character, CommonObjectID item) {
    SceneGraph* sg = Engine::Instance()->GetSceneGraph();
    ObjectID char_id = GetObjectID(character);
    ObjectID item_id = GetObjectID(item);
    Object* co = sg != nullptr ? sg->GetObjectFromID(char_id) : nullptr;
    Object* io = sg != nullptr ? sg->GetObjectFromID(item_id) : nullptr;

    if (co == nullptr || io == nullptr || co->GetType() != _movement_object || io->GetType() != _item_object || !IsLocalAvatar(char_id)) {
        return;
    }

    MovementObject* mo = static_cast<MovementObject*>(co);
    if (static_cast<ItemObject*>(io)->HeldByWhom() == char_id) {
        mo->ASDetachItem(item_id);
        mo->Execute("NotifyItemDetach(" + to_string(item_id) + ");");
        LOGI << "Dropped item " << item_id << ": someone else got it first" << std::endl;
    }
}

vector<ObjectID> Online::GetLocalAvatarIDs() const {"""),
])
print("ok")
