//-----------------------------------------------------------------------------
//           Name: character_snapshots.h
//      Developer: Overgrowth Online (crxhvrd)
//    Description: A bundle of packed character records (Online/char_snapshot.h)
//                 from a character's owner, or relayed by the host. Replaces
//                 Wolfire's MovementObjectUpdate (one string-keyed message per
//                 character, all bones, about 120 Hz).
//        License: Apache-2.0, like the rest of the engine (see LICENSE)
//-----------------------------------------------------------------------------
#pragma once

#include "online_message_base.h"

#include <cstdint>
#include <vector>

namespace OnlineMessages {
class CharacterSnapshots : public OnlineMessageBase {
   public:
    std::vector<uint8_t> records;

   public:
    CharacterSnapshots();
    CharacterSnapshots(const std::vector<uint8_t>& records);

    static binn* Serialize(void* object);
    static void Deserialize(void* object, binn* l);
    static void Execute(const OnlineMessageRef& ref, void* object, PeerID peer);
    static void* Construct(void* mem);
    static void Destroy(void* object);
};
}  // namespace OnlineMessages
