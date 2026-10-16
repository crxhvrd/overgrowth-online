//-----------------------------------------------------------------------------
//           Name: character_snapshots.cpp
//      Developer: Overgrowth Online (crxhvrd)
//    Description: See character_snapshots.h
//        License: Apache-2.0, like the rest of the engine (see LICENSE)
//-----------------------------------------------------------------------------
#include "character_snapshots.h"

#include <Online/online.h>

#include <cstring>

namespace OnlineMessages {
CharacterSnapshots::CharacterSnapshots() : OnlineMessageBase(OnlineMessageCategory::LEVEL_TRANSIENT) {
    reliable_delivery = false;
}

CharacterSnapshots::CharacterSnapshots(const std::vector<uint8_t>& records) : OnlineMessageBase(OnlineMessageCategory::LEVEL_TRANSIENT),
                                                                              records(records) {
    reliable_delivery = false;
}

binn* CharacterSnapshots::Serialize(void* object) {
    CharacterSnapshots* cs = static_cast<CharacterSnapshots*>(object);
    binn* l = binn_object();
    binn_object_set_blob(l, "r", cs->records.data(), (int)cs->records.size());
    return l;
}

void CharacterSnapshots::Deserialize(void* object, binn* l) {
    CharacterSnapshots* cs = static_cast<CharacterSnapshots*>(object);
    void* data_ptr = nullptr;
    int data_size = 0;

    if (binn_object_get_blob(l, "r", &data_ptr, &data_size) && data_ptr != nullptr && data_size > 0) {
        cs->records.resize(data_size);
        memcpy(cs->records.data(), data_ptr, data_size);
    } else {
        cs->records.clear();
    }
}

void CharacterSnapshots::Execute(const OnlineMessageRef& ref, void* object, PeerID peer) {
    CharacterSnapshots* cs = static_cast<CharacterSnapshots*>(object);
    Online::Instance()->ReceiveSnapshots(peer, cs->records.data(), cs->records.size());
}

void* CharacterSnapshots::Construct(void* mem) {
    return new (mem) CharacterSnapshots();
}

void CharacterSnapshots::Destroy(void* object) {
    CharacterSnapshots* cs = static_cast<CharacterSnapshots*>(object);
    cs->~CharacterSnapshots();
}
}  // namespace OnlineMessages
