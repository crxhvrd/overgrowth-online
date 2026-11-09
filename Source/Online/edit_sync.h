//-----------------------------------------------------------------------------
//           Name: edit_sync.h
//      Developer: Overgrowth Online (crxhvrd)
//    Description: Building a level together (docs_mp_editing_plan.md). The host
//                 owns the level. After an editor operation the machine that
//                 edited compares every object's description with the last one
//                 it knows (bytes) and sends what differs: a builder to the host,
//                 which checks it, applies it and sends the result to everyone;
//                 the host to everyone. Ids in a batch are the host's; guests
//                 translate (Online::GetObjectID / GetOriginalID). The host also
//                 keeps which objects were edited since the level loaded or was
//                 saved: a guest who loads the level file gets those.
//        License: Apache-2.0, like the rest of the engine (see LICENSE)
//-----------------------------------------------------------------------------
#pragma once

#include <Game/EntityDescription.h>

#include <cstdint>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

class SceneGraph;
class Object;

class EditSync {
   public:
    enum Kind : uint8_t {
        kChanged = 1,
        kNew = 2,
        kRemoved = 3,
        kSky = 4,
        kLevelParams = 5,
        kRestore = 6  // (an undone removal: like kNew, the old ids kept where free)
    };
    struct Entry {
        uint8_t kind;
        int32_t id;         // the host's (a builder's kNew: its own, a placeholder)
        std::string bytes;  // a packed description (kChanged, kNew, kSky, kLevelParams)
    };

    // A level loaded: nothing edited yet
    void Reset(SceneGraph* sg);
    // This machine's editor opened: what's there now is what it compares against
    void Rebaseline(SceneGraph* sg);
    // The host saved: the file holds the edits
    void MarkSaved();
    // After an editor operation (and once a second with the editor open): send what changed
    void Scan(SceneGraph* sg);
    // A batch from the network (applied in Online::LateUpdate, outside message handling)
    void Queue(int origin, int from_peer, const std::string& blob);
    void ApplyQueued(SceneGraph* sg);
    // Host: the edits since the level loaded or was saved, for a guest who just loaded the file
    std::string CatchUpBlob(SceneGraph* sg);
    // Undo and redo of this machine's own edits (phase 3): the earlier descriptions sent as a new
    // edit; the host refuses what another builder holds
    void Undo(SceneGraph* sg);
    void Redo(SceneGraph* sg);
    bool CanUndo() const { return !undo_.empty(); }
    bool CanRedo() const { return !redo_.empty(); }

    static void PackDesc(const EntityDescription& desc, std::string* out);
    static bool UnpackDesc(const std::string& bytes, EntityDescription* desc);
    static std::string Pack(const std::vector<Entry>& entries);
    static bool Unpack(const std::string& blob, std::vector<Entry>* entries);

    bool applying = false;  // (our own removals and creations: no other online side effects)
    int scans = 0, batches_sent = 0, batches_applied = 0, refused = 0;  // (the test bridge's)
    int edited_count() const { return (int)edited_.size(); }
    int unsaved_count() const { return (int)edited_.size() + (sky_edited_ ? 1 : 0) + (params_edited_ ? 1 : 0); }

   private:
    struct Pending {
        int origin;
        int from_peer;
        std::string blob;
    };
    std::vector<Pending> pending_;
    bool have_known_ = false;
    std::unordered_map<int, std::string> known_;  // this machine's object id -> its description as last sent or received
    std::string known_sky_, known_params_;
    std::map<int, uint8_t> edited_;  // host: object id -> kNew / kChanged / kRemoved since load or save
    struct Step {
        std::vector<Entry> back, forth;  // (host ids)
        int pending_new = 0;             // (a guest's new objects: their host ids come with the answer)
    };
    std::vector<Step> undo_, redo_;
    void Replay(SceneGraph* sg, const std::vector<Entry>& entries);
    void NoteNewForUndo(int host_id, const std::string& host_desc);
    std::string HostDesc(const std::string& local_bytes);
    bool sky_edited_ = false, params_edited_ = false;

    void Describe(SceneGraph* sg, std::unordered_map<int, std::string>* objects, std::string* sky, std::string* params);
    void DescribeLevel(SceneGraph* sg, std::string* sky, std::string* params);
    std::string DescribeOne(Object* obj);
    void NoteEdited(const Entry& e);
    void ApplyAsHost(SceneGraph* sg, int from_peer, std::vector<Entry>& entries);
    void ApplyAsGuest(SceneGraph* sg, int origin, std::vector<Entry>& entries);
    void Remove(SceneGraph* sg, int local_id);
    Object* Create(SceneGraph* sg, EntityDescription& desc, bool select);
    void ApplySky(SceneGraph* sg, const std::string& bytes);
    void ApplyParams(SceneGraph* sg, const std::string& bytes);
};
