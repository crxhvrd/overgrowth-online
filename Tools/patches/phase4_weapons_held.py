"""Phase 4 (combat), part 3: held weapons. A held or stuck item follows its
holder's hand on every machine (on proxies from the streamed bones); nobody
streams or applies a network position for it. The host relays pickups and
drops from clients to everyone, and arbitrates: an item already in someone
else's hand on the host is given back to them. Run from the repo root."""
S = "Source/"


def patch(p, pairs):
    s = open(S + p, encoding="utf-8").read()
    for o, n in pairs:
        assert s.count(o) == 1, (p, o[:90], s.count(o))
        s = s.replace(o, n)
    open(S + p, "w", encoding="utf-8", newline="").write(s)


# Proxies: weapons follow the streamed hand
patch("Objects/movementobject.cpp", [
("""        rigged_object_->UpdateCollisionObjects();
    }

    char_sphere->SetPosition(position);""", """        rigged_object_->UpdateCollisionObjects();
        rigged_object_->UpdateAttachedItems();  // held and stuck weapons follow the streamed bones
    }

    char_sphere->SetPosition(position);"""),
])

# Clients: no network position for an item in someone's hand or stuck in someone
patch("Objects/itemobject.cpp", [
("""    if (online->IsClient()) {
        std::list<ItemObjectFrame>& frames = incoming_online_item_frames;""", """    if (online->IsClient()) {
        std::list<ItemObjectFrame>& frames = incoming_online_item_frames;

        // Overhaul Online: held or stuck, it follows its character here
        if (IsHeld() || IsStuckInCharacter()) {
            frames.clear();
            return;
        }
"""),
])

# Host: don't stream those either
patch("Online/online.cpp", [
("""                    case _item_object:
                        Send<OnlineMessages::ItemUpdate>(static_cast<ItemObject*>(o));
                        break;""", """                    case _item_object: {
                        ItemObject* item = static_cast<ItemObject*>(o);
                        if (!item->IsHeld() && !item->IsStuckInCharacter()) {  // those follow their character everywhere
                            Send<OnlineMessages::ItemUpdate>(item);
                        }
                        break;
                    }"""),
])

# Host: relay attaches and detaches from clients; refuse an item someone else holds
patch("Online/Message/attach_to_message.cpp", [
("""    if (parent != nullptr && child != nullptr) {
        if (t->attach) {""", """    Online* online = Online::Instance();

    // Overhaul Online: the host decides who holds an item. A client grabbing
    // one that is already in someone else's hand here gets told who has it.
    if (online->IsHosting() && parent != nullptr && child != nullptr && t->attach &&
        child->GetType() == EntityType::_item_object && parent->GetType() == EntityType::_movement_object) {
        int holder = static_cast<ItemObject*>(child)->HeldByWhom();
        Peer* peer = online->GetPeerFromID(from);
        if (holder != -1 && holder != parent_object_id && peer != nullptr) {
            online->SendTo<OnlineMessages::AttachToMessage>(peer->conn_id, parent_object_id, child_object_id, 0, false, false);
            online->SendTo<OnlineMessages::AttachToMessage>(peer->conn_id, holder, child_object_id, t->bone_id, true, t->mirrored);
            return;
        }
    }

    if (online->IsHosting()) {
        online->RelayFromPeer(ref, from);  // everyone else sees the pickup or the drop too
    }

    if (parent != nullptr && child != nullptr) {
        if (t->attach) {"""),
])

patch("Online/online.h", [
("""    void SendToOwnerOf(ObjectID id, const OnlineMessageRef& ref, bool peers_locked);""",
 """    void SendToOwnerOf(ObjectID id, const OnlineMessageRef& ref, bool peers_locked);
    void RelayFromPeer(const OnlineMessageRef& ref, PeerID from);  // host, inside message handling: to every other client"""),
])

patch("Online/online.cpp", [
("""void Online::SendHitRequest(""", """// Host, inside message handling (peers lock held): pass a client's message on
// to every other client
void Online::RelayFromPeer(const OnlineMessageRef& ref, PeerID from) {
    if (!IsHosting() || online_session == nullptr) {
        return;
    }

    for (const Peer& peer : online_session->connected_peers) {
        if (peer.peer_id != from) {
            SendTo(peer.conn_id, ref);
        }
    }
}

void Online::SendHitRequest("""),
])
print("ok")
