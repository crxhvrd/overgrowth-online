//-----------------------------------------------------------------------------
//           Name: edit_messages.h
//      Developer: Overgrowth Online (crxhvrd)
//    Description: Building a level together (Online/edit_sync.h,
//                 docs_mp_editing_plan.md): a batch of edits, and the host's
//                 build state (build mode, who may build).
//        License: Apache-2.0, like the rest of the engine (see LICENSE)
//-----------------------------------------------------------------------------
#pragma once

#include "online_message_base.h"

#include <Math/vec3.h>

#include <cstdint>
#include <string>
#include <vector>

namespace OnlineMessages {
// Edits: a builder's to the host, or the host's to everyone (EditSync::Pack)
class EditBatch : public OnlineMessageBase {
   public:
    int32_t origin;  // whose edit this is (the script API's player ids; -1: the host catching a guest up)
    std::string blob;

    EditBatch(int32_t origin, const std::string& blob);

    static binn* Serialize(void* object);
    static void Deserialize(void* object, binn* source);
    static void Execute(const OnlineMessageRef& ref, void* object, PeerID from);
    static void* Construct(void* mem);
    static void Destroy(void* object);
};

// The host's: whether the session is building (paused, builders in the editor) and who may build
class BuildStateMessage : public OnlineMessageBase {
   public:
    bool building;
    std::vector<int32_t> builders;
    // The level's save state: seconds since the host saved it / autosaved it (-1: not since it
    // loaded), and how many objects changed since
    int32_t saved_ago = -1, autosaved_ago = -1, unsaved = 0;

    BuildStateMessage(bool building, const std::vector<int>& builders, int saved_ago, int autosaved_ago, int unsaved);

    static binn* Serialize(void* object);
    static void Deserialize(void* object, binn* source);
    static void Execute(const OnlineMessageRef& ref, void* object, PeerID from);
    static void* Construct(void* mem);
    static void Destroy(void* object);
};
// A builder in the editor: where its camera is and what it has selected (shared ids), a few times a
// second; editing false once it leaves the editor. Guests send theirs to the host, which stamps the
// sender and passes it on
class BuilderPresence : public OnlineMessageBase {
   public:
    int32_t player;
    vec3 camera;
    bool editing;
    std::vector<int32_t> selection;

    BuilderPresence(int32_t player, const vec3& camera, bool editing, const std::vector<int>& selection);

    static binn* Serialize(void* object);
    static void Deserialize(void* object, binn* source);
    static void Execute(const OnlineMessageRef& ref, void* object, PeerID from);
    static void* Construct(void* mem);
    static void Destroy(void* object);
};

// A part of the host's level file (zstd, at most 256 KB a part), for a level the guests can't have:
// one from the host's write folder (its own, saved). Sent before the message naming the level
class LevelFileChunk : public OnlineMessageBase {
   public:
    std::string name;  // the level as the host names it (PCSFileTransferMetadataMessage)
    int32_t part, parts;
    std::string bytes;

    LevelFileChunk(const std::string& name, int part, int parts, const std::string& bytes);

    static binn* Serialize(void* object);
    static void Deserialize(void* object, binn* source);
    static void Execute(const OnlineMessageRef& ref, void* object, PeerID from);
    static void* Construct(void* mem);
    static void Destroy(void* object);
};

// A builder's request to the host: 1 = save the level (the host saves, or is asked to)
class BuildRequest : public OnlineMessageBase {
   public:
    int32_t what;

    explicit BuildRequest(int32_t what);

    static binn* Serialize(void* object);
    static void Deserialize(void* object, binn* source);
    static void Execute(const OnlineMessageRef& ref, void* object, PeerID from);
    static void* Construct(void* mem);
    static void Destroy(void* object);
};
}  // namespace OnlineMessages
