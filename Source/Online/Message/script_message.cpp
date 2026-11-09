//-----------------------------------------------------------------------------
//           Name: script_message.cpp
//      Developer: Overgrowth Online (crxhvrd)
//    Description: The script API's messages (see script_message.h)
//        License: Apache-2.0, like the rest of the engine (see LICENSE)
//-----------------------------------------------------------------------------
#include "script_message.h"

#include <Online/online.h>

namespace OnlineMessages {
ScriptMessage::ScriptMessage(int32_t to, int32_t from, const std::string& channel, const std::string& text)
    : OnlineMessageBase(OnlineMessageCategory::LEVEL_TRANSIENT), to(to), from(from), channel(channel), text(text) {
}

binn* ScriptMessage::Serialize(void* object) {
    ScriptMessage* m = static_cast<ScriptMessage*>(object);
    binn* l = binn_object();
    binn_object_set_int32(l, "to", m->to);
    binn_object_set_int32(l, "from", m->from);
    binn_object_set_std_string(l, "ch", m->channel);
    binn_object_set_std_string(l, "text", m->text);
    return l;
}

void ScriptMessage::Deserialize(void* object, binn* l) {
    ScriptMessage* m = static_cast<ScriptMessage*>(object);
    binn_object_get_int32(l, "to", &m->to);
    binn_object_get_int32(l, "from", &m->from);
    binn_object_get_std_string(l, "ch", &m->channel);
    binn_object_get_std_string(l, "text", &m->text);
}

// On the host (from a guest): stamped with the guest's id, checked, and delivered here when it's
// for everyone or for the host; Online::CheckPendingMessages passes it on (the peers are locked
// there). On a guest (from the host): delivered. Scripts get it in Online::LateUpdate
void ScriptMessage::Execute(const OnlineMessageRef& ref, void* object, PeerID peer) {
    ScriptMessage* m = static_cast<ScriptMessage*>(object);
    Online* online = Online::Instance();
    if (online->IsHosting()) {
        m->from = online->ApiPlayerID(peer);
        if (!online->AcceptScriptMessage(peer, m->channel, m->text)) {
            m->to = kDropped;
            return;
        }
        if (m->to == kEveryone || m->to == 0) {
            online->QueueScriptDelivery(m->from, m->channel, m->text);
        }
    } else {
        online->QueueScriptDelivery(m->from, m->channel, m->text);
    }
}

void* ScriptMessage::Construct(void* mem) {
    return new (mem) ScriptMessage(kEveryone, 0, "", "");
}

void ScriptMessage::Destroy(void* object) {
    ScriptMessage* m = static_cast<ScriptMessage*>(object);
    m->~ScriptMessage();
}
}  // namespace OnlineMessages
