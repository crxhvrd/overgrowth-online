//-----------------------------------------------------------------------------
//           Name: combat_messages.cpp
//      Developer: Overgrowth Online (crxhvrd)
//    Description: See combat_messages.h
//        License: Apache-2.0, like the rest of the engine (see LICENSE)
//-----------------------------------------------------------------------------
#include "combat_messages.h"

#include <Online/online.h>
#include <Utility/binn_util.h>

namespace OnlineMessages {
HitRequest::HitRequest() : OnlineMessageBase(OnlineMessageCategory::LEVEL_TRANSIENT) {
}

HitRequest::HitRequest(CommonObjectID attacker, CommonObjectID target, const std::string& type, const std::string& attack_path,
                       vec3 dir, vec3 pos, float damage_mult, float knockback_mult, float max_range)
    : OnlineMessageBase(OnlineMessageCategory::LEVEL_TRANSIENT),
      attacker(attacker),
      target(target),
      type(type),
      attack_path(attack_path),
      dir(dir),
      pos(pos),
      damage_mult(damage_mult),
      knockback_mult(knockback_mult),
      max_range(max_range) {
}

binn* HitRequest::Serialize(void* object) {
    HitRequest* m = static_cast<HitRequest*>(object);
    binn* l = binn_object();
    binn_object_set_int32(l, "a", m->attacker);
    binn_object_set_int32(l, "t", m->target);
    binn_object_set_std_string(l, "y", m->type);
    binn_object_set_std_string(l, "p", m->attack_path);
    binn_object_set_vec3(l, "d", m->dir);
    binn_object_set_vec3(l, "o", m->pos);
    binn_object_set_float(l, "dm", m->damage_mult);
    binn_object_set_float(l, "km", m->knockback_mult);
    binn_object_set_float(l, "r", m->max_range);
    binn_object_set_uint32(l, "s", m->src_ms);
    return l;
}

void HitRequest::Deserialize(void* object, binn* l) {
    HitRequest* m = static_cast<HitRequest*>(object);
    binn_object_get_int32(l, "a", &m->attacker);
    binn_object_get_int32(l, "t", &m->target);
    binn_object_get_std_string(l, "y", &m->type);
    binn_object_get_std_string(l, "p", &m->attack_path);
    binn_object_get_vec3(l, "d", &m->dir);
    binn_object_get_vec3(l, "o", &m->pos);
    binn_object_get_float(l, "dm", &m->damage_mult);
    binn_object_get_float(l, "km", &m->knockback_mult);
    binn_object_get_float(l, "r", &m->max_range);
    binn_object_get_uint32(l, "s", &m->src_ms);
}

void HitRequest::Execute(const OnlineMessageRef& ref, void* object, PeerID peer) {
    Online::Instance()->ReceiveHitRequest(ref, static_cast<HitRequest*>(object), peer);
}

void* HitRequest::Construct(void* mem) {
    return new (mem) HitRequest();
}

void HitRequest::Destroy(void* object) {
    static_cast<HitRequest*>(object)->~HitRequest();
}

HitResult::HitResult() : OnlineMessageBase(OnlineMessageCategory::LEVEL_TRANSIENT) {
}

HitResult::HitResult(CommonObjectID attacker, CommonObjectID target, const std::string& type, int32_t result)
    : OnlineMessageBase(OnlineMessageCategory::LEVEL_TRANSIENT),
      attacker(attacker),
      target(target),
      type(type),
      result(result) {
}

binn* HitResult::Serialize(void* object) {
    HitResult* m = static_cast<HitResult*>(object);
    binn* l = binn_object();
    binn_object_set_int32(l, "a", m->attacker);
    binn_object_set_int32(l, "t", m->target);
    binn_object_set_std_string(l, "y", m->type);
    binn_object_set_int32(l, "r", m->result);
    return l;
}

void HitResult::Deserialize(void* object, binn* l) {
    HitResult* m = static_cast<HitResult*>(object);
    binn_object_get_int32(l, "a", &m->attacker);
    binn_object_get_int32(l, "t", &m->target);
    binn_object_get_std_string(l, "y", &m->type);
    binn_object_get_int32(l, "r", &m->result);
}

void HitResult::Execute(const OnlineMessageRef& ref, void* object, PeerID peer) {
    Online::Instance()->ReceiveHitResult(ref, static_cast<HitResult*>(object), peer);
}

void* HitResult::Construct(void* mem) {
    return new (mem) HitResult();
}

void HitResult::Destroy(void* object) {
    static_cast<HitResult*>(object)->~HitResult();
}
ItemLaunch::ItemLaunch() : OnlineMessageBase(OnlineMessageCategory::LEVEL_TRANSIENT) {
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

AvatarCharacter::AvatarCharacter() : OnlineMessageBase(OnlineMessageCategory::LEVEL_PERSISTENT) {
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
}

OwnerOverride::OwnerOverride(CommonObjectID character, int32_t owner, bool active, float max_seconds)
    : OnlineMessageBase(OnlineMessageCategory::LEVEL_TRANSIENT),
      character(character),
      owner(owner),
      active(active),
      max_seconds(max_seconds) {
}

binn* OwnerOverride::Serialize(void* object) {
    OwnerOverride* m = static_cast<OwnerOverride*>(object);
    binn* l = binn_object();
    binn_object_set_int32(l, "c", m->character);
    binn_object_set_int32(l, "o", m->owner);
    binn_object_set_bool(l, "a", m->active);
    binn_object_set_float(l, "s", m->max_seconds);
    return l;
}

void OwnerOverride::Deserialize(void* object, binn* l) {
    OwnerOverride* m = static_cast<OwnerOverride*>(object);
    BOOL active = FALSE;
    binn_object_get_int32(l, "c", &m->character);
    binn_object_get_int32(l, "o", &m->owner);
    binn_object_get_bool(l, "a", &active);
    binn_object_get_float(l, "s", &m->max_seconds);
    m->active = active != FALSE;
}

void OwnerOverride::Execute(const OnlineMessageRef& ref, void* object, PeerID peer) {
    OwnerOverride* m = static_cast<OwnerOverride*>(object);
    Online::Instance()->ReceiveOwnerOverride(m->character, (PlayerID)m->owner, m->active, m->max_seconds, peer);
}

void* OwnerOverride::Construct(void* mem) {
    return new (mem) OwnerOverride();
}

void OwnerOverride::Destroy(void* object) {
    static_cast<OwnerOverride*>(object)->~OwnerOverride();
}
}  // namespace OnlineMessages
