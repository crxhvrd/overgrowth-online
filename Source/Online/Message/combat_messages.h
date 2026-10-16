//-----------------------------------------------------------------------------
//           Name: combat_messages.h
//      Developer: Overgrowth Online (crxhvrd)
//    Description: Combat between characters run by different machines.
//                 HitRequest: an attacker's machine asks the defender's owner
//                 to resolve a hit (block, dodge, damage) on its own state.
//                 HitResult: the owner's answer, back to the attacker's owner
//                 (blocked or dodged attacks stagger the attacker). Both are
//                 relayed by the host when neither end is the host.
//        License: Apache-2.0, like the rest of the engine (see LICENSE)
//-----------------------------------------------------------------------------
#pragma once

#include "online_message_base.h"

#include <Online/online_datastructures.h>
#include <Math/vec3.h>

#include <string>

namespace OnlineMessages {
class HitRequest : public OnlineMessageBase {
   public:
    CommonObjectID attacker = -1;
    CommonObjectID target = -1;
    std::string type;         // blockprepare, attackimpact, attackblocked, grabbed
    std::string attack_path;
    vec3 dir;
    vec3 pos;
    float damage_mult = 1.0f;
    float knockback_mult = 1.0f;
    float max_range = 0.0f;   // the attacker's reach, for the defender's own range check
    uint32_t src_ms = 0;      // the attacker's time (OnlineClockMs): held until the defender shows it

    HitRequest();
    HitRequest(CommonObjectID attacker, CommonObjectID target, const std::string& type, const std::string& attack_path,
               vec3 dir, vec3 pos, float damage_mult, float knockback_mult, float max_range);

    static binn* Serialize(void* object);
    static void Deserialize(void* object, binn* l);
    static void Execute(const OnlineMessageRef& ref, void* object, PeerID peer);
    static void* Construct(void* mem);
    static void Destroy(void* object);
};

class HitResult : public OnlineMessageBase {
   public:
    CommonObjectID attacker = -1;
    CommonObjectID target = -1;
    std::string type;
    int32_t result = 0;  // aschar.as: _miss 0, _going_to_block 1, _hit 2, _block_impact 3, _invalid 4, _going_to_dodge 5

    HitResult();
    HitResult(CommonObjectID attacker, CommonObjectID target, const std::string& type, int32_t result);

    static binn* Serialize(void* object);
    static void Deserialize(void* object, binn* l);
    static void Execute(const OnlineMessageRef& ref, void* object, PeerID peer);
    static void* Construct(void* mem);
    static void Destroy(void* object);
};
// A client threw an item: the host flies it from here on
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

// A character's file changed on the machine that runs it (a player's chosen
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

// A character handed to another machine for a throw, choke or drag (active),
// or handed back (not active). Clients send theirs to the host, which keeps
// the table and tells everyone.
class OwnerOverride : public OnlineMessageBase {
   public:
    CommonObjectID character = -1;
    int32_t owner = 0;  // PlayerID
    bool active = false;
    float max_seconds = 0.0f;

    OwnerOverride();
    OwnerOverride(CommonObjectID character, int32_t owner, bool active, float max_seconds);

    static binn* Serialize(void* object);
    static void Deserialize(void* object, binn* l);
    static void Execute(const OnlineMessageRef& ref, void* object, PeerID peer);
    static void* Construct(void* mem);
    static void Destroy(void* object);
};
}  // namespace OnlineMessages
