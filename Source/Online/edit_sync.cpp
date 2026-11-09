//-----------------------------------------------------------------------------
//           Name: edit_sync.cpp
//      Developer: Overgrowth Online (crxhvrd)
//    Description: Building a level together (see edit_sync.h)
//        License: Apache-2.0, like the rest of the engine (see LICENSE)
//-----------------------------------------------------------------------------
#include "edit_sync.h"

#include <Online/online.h>
#include <Main/scenegraph.h>
#include <Editors/map_editor.h>
#include <Editors/sky_editor.h>
#include <Editors/actors_editor.h>
#include <Editors/save_state.h>
#include <Game/level.h>
#include <Graphics/sky.h>
#include <Objects/object.h>
#include <Scripting/scriptparams.h>
#include <Logging/logdata.h>

#include <cstring>
#include <functional>
#include <set>

namespace {

// ---- descriptions as bytes: [fields][children], every length checked when read (network input)
void PutU32(std::string* out, uint32_t v) {
    out->append(reinterpret_cast<const char*>(&v), 4);
}

bool GetU32(const char*& p, const char* end, uint32_t* v) {
    if (end - p < 4) {
        return false;
    }
    memcpy(v, p, 4);
    p += 4;
    return true;
}

void PackDescInto(const EntityDescription& desc, std::string* out) {
    PutU32(out, (uint32_t)desc.fields.size());
    for (const EntityDescriptionField& field : desc.fields) {
        PutU32(out, (uint32_t)field.type);
        PutU32(out, (uint32_t)field.data.size());
        if (!field.data.empty()) {
            out->append(field.data.data(), field.data.size());
        }
    }
    PutU32(out, (uint32_t)desc.children.size());
    for (const EntityDescription& child : desc.children) {
        PackDescInto(child, out);
    }
}

bool UnpackDescFrom(const char*& p, const char* end, EntityDescription* desc, int depth) {
    uint32_t fields = 0, children = 0;
    if (depth > 16 || !GetU32(p, end, &fields) || fields > 4096) {
        return false;
    }
    desc->fields.resize(fields);
    for (EntityDescriptionField& field : desc->fields) {
        uint32_t type = 0, size = 0;
        if (!GetU32(p, end, &type) || !GetU32(p, end, &size) || (uint32_t)(end - p) < size) {
            return false;
        }
        field.type = (int)type;
        field.data.assign(p, p + size);
        p += size;
    }
    if (!GetU32(p, end, &children) || children > 65536) {
        return false;
    }
    desc->children.resize(children);
    for (EntityDescription& child : desc->children) {
        if (!UnpackDescFrom(p, end, &child, depth + 1)) {
            return false;
        }
    }
    return true;
}

// ---- the object ids a description holds: its own, its children's, and its connections
void MapIDs(EntityDescription& desc, const std::function<int(int)>& map) {
    if (EntityDescriptionField* f = desc.GetField(EDF_ID)) {
        int id = -1;
        f->ReadInt(&id);
        f->data.clear();
        f->WriteInt(map(id));
    }
    const EntityDescriptionFieldType int_vecs[] = {EDF_CONNECTIONS, EDF_CONNECTIONS_FROM, EDF_HOTSPOT_CONNECTED_TO, EDF_HOTSPOT_CONNECTED_FROM};
    for (EntityDescriptionFieldType type : int_vecs) {
        if (EntityDescriptionField* f = desc.GetField(type)) {
            std::vector<int> ids;
            f->ReadIntVec(&ids);
            for (int& id : ids) {
                id = map(id);
            }
            f->data.clear();
            f->WriteIntVec(ids);
        }
    }
    if (EntityDescriptionField* f = desc.GetField(EDF_ITEM_CONNECTIONS)) {
        std::vector<ItemConnectionData> items;
        f->ReadItemConnectionDataVec(&items);
        for (ItemConnectionData& item : items) {
            item.id = map(item.id);
        }
        f->data.clear();
        f->WriteItemConnectionDataVec(items);
    }
    if (EntityDescriptionField* f = desc.GetField(EDF_NAV_MESH_CONNECTIONS)) {
        std::vector<NavMeshConnectionData> links;
        f->ReadNavMeshConnectionDataVec(&links);
        for (NavMeshConnectionData& link : links) {
            link.other_object_id = map(link.other_object_id);
        }
        f->data.clear();
        f->WriteNavMeshConnectionDataVec(links);
    }
    for (EntityDescription& child : desc.children) {
        MapIDs(child, map);
    }
}

int DescID(const EntityDescription& desc) {
    int id = -1;
    if (const EntityDescriptionField* f = desc.GetField(EDF_ID)) {
        f->ReadInt(&id);
    }
    return id;
}

void CollectIDs(const EntityDescription& desc, std::vector<int>* ids) {
    ids->push_back(DescID(desc));
    for (const EntityDescription& child : desc.children) {
        CollectIDs(child, ids);
    }
}

// What each machine works out itself when it draws: a reflection capture's average colours, read
// back from its own render. Never compared or sent, else every render looks like an edit (and the
// copy made from it renders again, on every machine, forever)
void StripRendered(EntityDescription& desc) {
    int type = -1;
    if (const EntityDescriptionField* f = desc.GetField(EDF_ENTITY_TYPE)) {
        f->ReadInt(&type);
    }
    if (type == _reflection_capture_object) {
        for (auto it = desc.fields.begin(); it != desc.fields.end();) {
            it = it->type == EDF_GI_COEFFICIENTS ? desc.fields.erase(it) : std::next(it);
        }
    }
    for (EntityDescription& child : desc.children) {
        StripRendered(child);
    }
}

}  // namespace

void EditSync::PackDesc(const EntityDescription& desc, std::string* out) {
    out->clear();
    PackDescInto(desc, out);
}

bool EditSync::UnpackDesc(const std::string& bytes, EntityDescription* desc) {
    const char* p = bytes.data();
    const char* end = p + bytes.size();
    *desc = EntityDescription();
    return UnpackDescFrom(p, end, desc, 0) && p == end;
}

std::string EditSync::Pack(const std::vector<Entry>& entries) {
    std::string out;
    PutU32(&out, (uint32_t)entries.size());
    for (const Entry& e : entries) {
        out.push_back((char)e.kind);
        PutU32(&out, (uint32_t)e.id);
        PutU32(&out, (uint32_t)e.bytes.size());
        out += e.bytes;
    }
    return out;
}

bool EditSync::Unpack(const std::string& blob, std::vector<Entry>* entries) {
    const char* p = blob.data();
    const char* end = p + blob.size();
    uint32_t count = 0;
    if (!GetU32(p, end, &count) || count > 100000) {
        return false;
    }
    entries->clear();
    for (uint32_t i = 0; i < count; ++i) {
        Entry e;
        uint32_t id = 0, size = 0;
        if (end - p < 1) {
            return false;
        }
        e.kind = (uint8_t)*p++;
        if (!GetU32(p, end, &id) || !GetU32(p, end, &size) || (uint32_t)(end - p) < size) {
            return false;
        }
        e.id = (int32_t)id;
        e.bytes.assign(p, size);
        p += size;
        if (e.kind < kChanged || e.kind > kRestore) {
            return false;
        }
        entries->push_back(std::move(e));
    }
    return p == end;
}

// ---- what this machine has ----------------------------------------------------------------

std::string EditSync::DescribeOne(Object* obj) {
    EntityDescription desc;
    obj->GetDesc(desc);
    StripRendered(desc);
    std::string bytes;
    PackDesc(desc, &bytes);
    return bytes;
}

// The objects the undo history keeps (top-level, not excluded), less the players' characters
// (the online layer runs those); the sky editor's state; the level's parameters
void EditSync::Describe(SceneGraph* sg, std::unordered_map<int, std::string>* objects, std::string* sky, std::string* params) {
    Online* online = Online::Instance();
    objects->clear();
    for (Object* obj : sg->objects_) {
        if (obj->parent || obj->exclude_from_undo || online->IsPlayerCharacter(obj->GetID())) {
            continue;
        }
        (*objects)[obj->GetID()] = DescribeOne(obj);
    }
    DescribeLevel(sg, sky, params);
}

void EditSync::DescribeLevel(SceneGraph* sg, std::string* sky, std::string* params) {
    sky->clear();
    if (sg->map_editor && sg->map_editor->sky_editor_) {
        std::list<SavedChunk> chunks;
        sg->map_editor->sky_editor_->SaveHistoryState(chunks, 0);
        if (!chunks.empty()) {
            PackDesc(chunks.back().desc, sky);
        }
    }
    params->clear();
    if (sg->level) {
        EntityDescription desc;
        desc.AddScriptParams(EDF_SCRIPT_PARAMS, sg->level->script_params().GetParameterMap());
        PackDesc(desc, params);
    }
}

void EditSync::Reset(SceneGraph* sg) {
    pending_.clear();
    undo_.clear();
    redo_.clear();
    edited_.clear();
    sky_edited_ = params_edited_ = false;
    have_known_ = false;
    if (sg != nullptr) {
        Rebaseline(sg);
    }
}

void EditSync::Rebaseline(SceneGraph* sg) {
    Describe(sg, &known_, &known_sky_, &known_params_);
    have_known_ = true;
}

void EditSync::MarkSaved() {
    edited_.clear();
    sky_edited_ = params_edited_ = false;
}

void EditSync::NoteEdited(const Entry& e) {
    switch (e.kind) {
        case kNew:
            edited_[e.id] = kNew;
            break;
        case kChanged: {
            auto found = edited_.find(e.id);
            if (found == edited_.end() || found->second != kNew) {
                edited_[e.id] = kChanged;
            }
            break;
        }
        case kRemoved: {
            auto found = edited_.find(e.id);
            if (found != edited_.end() && found->second == kNew) {
                edited_.erase(found);  // (made and gone in the same session: nothing for the file)
            } else {
                edited_[e.id] = kRemoved;
            }
            break;
        }
        case kSky:
            sky_edited_ = true;
            break;
        case kLevelParams:
            params_edited_ = true;
            break;
    }
}

// ---- sending -------------------------------------------------------------------------------

void EditSync::Scan(SceneGraph* sg) {
    Online* online = Online::Instance();
    if (sg == nullptr || !online->IsActive() || !online->MayUseEditor()) {
        return;
    }
    // Only while building: in play things move by themselves (characters, items, scripts' params),
    // and that isn't editing. Nor just after the switch, while the level's reset settles
    if (!online->BuildMode() || online->EditRebaselinePending()) {
        return;
    }
    if (!have_known_) {
        Rebaseline(sg);
        return;
    }
    ++scans;
    std::unordered_map<int, std::string> now;
    std::string sky, params;
    Describe(sg, &now, &sky, &params);

    std::vector<Entry> entries;
    std::vector<int> new_ids;
    for (const auto& obj : now) {
        auto found = known_.find(obj.first);
        if (found == known_.end()) {
            entries.push_back({kNew, obj.first, obj.second});
            new_ids.push_back(obj.first);
        } else if (found->second != obj.second) {
            entries.push_back({kChanged, obj.first, obj.second});
        }
    }
    for (const auto& obj : known_) {
        if (now.find(obj.first) == now.end()) {
            entries.push_back({kRemoved, obj.first, std::string()});
        }
    }
    if (sky != known_sky_) {
        entries.push_back({kSky, 0, sky});
    }
    if (params != known_params_) {
        entries.push_back({kLevelParams, 0, params});
    }
    if (entries.empty()) {
        return;
    }

    // The step to undo this (and redo it), in the host's ids
    {
        Step step;
        for (const Entry& e : entries) {
            int host_id = (int)online->GetOriginalID(e.id);
            auto old = known_.find(e.id);
            switch (e.kind) {
                case kChanged:
                    step.back.push_back({kChanged, host_id, HostDesc(old->second)});
                    step.forth.push_back({kChanged, host_id, HostDesc(e.bytes)});
                    break;
                case kRemoved:
                    step.back.push_back({kRestore, host_id, HostDesc(old->second)});
                    step.forth.push_back({kRemoved, host_id, std::string()});
                    break;
                case kNew:
                    if (online->IsHosting()) {
                        step.back.push_back({kRemoved, e.id, std::string()});
                        step.forth.push_back({kRestore, e.id, e.bytes});
                    } else {
                        ++step.pending_new;
                    }
                    break;
                case kSky:
                    step.back.push_back({kSky, 0, known_sky_});
                    step.forth.push_back({kSky, 0, e.bytes});
                    break;
                case kLevelParams:
                    step.back.push_back({kLevelParams, 0, known_params_});
                    step.forth.push_back({kLevelParams, 0, e.bytes});
                    break;
            }
        }
        undo_.push_back(step);
        if (undo_.size() > 100) {
            undo_.erase(undo_.begin());
        }
        redo_.clear();
    }

    if (online->IsHosting()) {
        known_.swap(now);
        known_sky_ = sky;
        known_params_ = params;
        for (const Entry& e : entries) {
            NoteEdited(e);
        }
        online->TallyEdits(0, entries);
        online->SendEditBatch(0, Pack(entries), -1);
    } else {
        // What we sent is what we have now; our new objects come back from the host, with its
        // ids (removed here until then: no two objects ever share an id)
        known_ = now;
        known_sky_ = sky;
        known_params_ = params;
        for (int id : new_ids) {
            known_.erase(id);
        }
        for (Entry& e : entries) {
            if (e.kind == kChanged || e.kind == kNew) {
                EntityDescription desc;
                UnpackDesc(e.bytes, &desc);
                MapIDs(desc, [online](int id) { return (int)online->GetOriginalID(id); });
                PackDesc(desc, &e.bytes);
            }
            if (e.kind == kChanged || e.kind == kRemoved) {
                e.id = (int)online->GetOriginalID(e.id);
            }
        }
        online->SendEditBatch(online->OwnApiPlayerID(), Pack(entries), 0);
        applying = true;
        for (int id : new_ids) {
            Remove(sg, id);
            online->UnlinkEditedObject(id);
        }
        applying = false;
    }
    ++batches_sent;
    LOGI << "Overgrowth Online editing: sent " << entries.size() << " change(s)" << std::endl;
}

std::string EditSync::CatchUpBlob(SceneGraph* sg) {
    std::vector<Entry> entries;
    for (const auto& edit : edited_) {
        if (edit.second == kRemoved) {
            if (!sg->DoesObjectWithIdExist(edit.first)) {
                entries.push_back({kRemoved, edit.first, std::string()});
            }
        } else if (sg->DoesObjectWithIdExist(edit.first)) {
            entries.push_back({edit.second, edit.first, DescribeOne(sg->GetObjectFromID(edit.first))});
        }
    }
    if (sky_edited_) {
        entries.push_back({kSky, 0, known_sky_});
    }
    if (params_edited_) {
        entries.push_back({kLevelParams, 0, known_params_});
    }
    return entries.empty() ? std::string() : Pack(entries);
}

// ---- applying ------------------------------------------------------------------------------

void EditSync::Queue(int origin, int from_peer, const std::string& blob) {
    pending_.push_back({origin, from_peer, blob});
}

void EditSync::ApplyQueued(SceneGraph* sg) {
    if (pending_.empty() || sg == nullptr) {
        return;
    }
    std::vector<Pending> batches;
    batches.swap(pending_);
    for (Pending& batch : batches) {
        std::vector<Entry> entries;
        if (!Unpack(batch.blob, &entries)) {
            LOGW << "Overgrowth Online editing: a batch that doesn't read (" << batch.blob.size() << " bytes), dropped" << std::endl;
            continue;
        }
        if (Online::Instance()->IsHosting()) {
            ApplyAsHost(sg, batch.from_peer, entries);
        } else {
            ApplyAsGuest(sg, batch.origin, entries);
        }
        ++batches_applied;
    }
}

void EditSync::Remove(SceneGraph* sg, int local_id) {
    if (sg->DoesObjectWithIdExist(local_id)) {
        sg->map_editor->RemoveObject(sg->GetObjectFromID(local_id), sg, true);
    }
}

Object* EditSync::Create(SceneGraph* sg, EntityDescription& desc, bool select) {
    Object* obj = CreateObjectFromDesc(desc);
    if (obj == nullptr) {
        LOGE << "Overgrowth Online editing: an object that couldn't be made from its description" << std::endl;
        return nullptr;
    }
    if (!ActorsEditor_AddEntity(sg, obj, NULL, false, true)) {
        LOGE << "Overgrowth Online editing: an object that couldn't be added" << std::endl;
        return nullptr;
    }
    if (select) {
        obj->Select(true);
    }
    return obj;
}

void EditSync::ApplySky(SceneGraph* sg, const std::string& bytes) {
    if (!sg->map_editor || !sg->map_editor->sky_editor_) {
        return;
    }
    SavedChunk chunk;
    chunk.type = ChunkType::SKY_EDITOR;
    if (UnpackDesc(bytes, &chunk.desc) && sg->map_editor->sky_editor_->ReadChunk(chunk) && sg->sky) {
        sg->sky->LightingChanged(sg->terrain_object_ != NULL);
    }
}

void EditSync::ApplyParams(SceneGraph* sg, const std::string& bytes) {
    EntityDescription desc;
    if (!UnpackDesc(bytes, &desc)) {
        return;
    }
    if (const EntityDescriptionField* f = desc.GetField(EDF_SCRIPT_PARAMS)) {
        ScriptParamMap spm;
        ReadScriptParametersFromRAM(spm, f->data);
        SceneGraph::ApplyScriptParams(sg, spm);
    }
}

// A builder's batch: checked, applied in order (removals, changes, the sky and the level's
// parameters, new objects), and the result sent to everyone, the builder too. New objects get
// fresh ids, except ids this batch removed (a group taking objects in keeps their ids)
void EditSync::ApplyAsHost(SceneGraph* sg, int from_peer, std::vector<Entry>& entries) {
    Online* online = Online::Instance();
    const int builder = from_peer < 0 ? 0 : online->ApiPlayerID((PeerID)from_peer);  // (-1: the host's own undo)
    if (builder != 0 && (!online->IsBuilder(builder) || !online->BuildMode())) {
        ++refused;
        std::vector<Entry> fix;  // (the host's state of what it named: its view snaps back)
        for (const Entry& e : entries) {
            if (e.kind == kChanged || e.kind == kRemoved) {
                if (sg->DoesObjectWithIdExist(e.id)) {
                    fix.push_back({kChanged, e.id, DescribeOne(sg->GetObjectFromID(e.id))});
                } else {
                    fix.push_back({kRemoved, e.id, std::string()});
                }
            }
        }
        if (!fix.empty()) {
            online->SendEditBatch(-1, Pack(fix), builder);
        }
        online->TellPlayer(builder, online->BuildMode() ? "The host hasn't let you build: your change was put back" : "Build mode is off: your change was put back");
        LOGW << "Overgrowth Online editing: player " << builder << " isn't building now; " << entries.size() << " change(s) refused" << std::endl;
        return;
    }

    // Claims: another builder holding an object (selected first) keeps it; this one's change or
    // removal of it is refused, and its copy snaps back
    std::vector<Entry> fix;
    std::map<int, int> held;  // holder -> how many of this builder's changes it held
    for (Entry& e : entries) {
        if ((e.kind == kChanged || e.kind == kRemoved) && online->ClaimedByOther(e.id, builder)) {
            if (sg->DoesObjectWithIdExist(e.id)) {
                fix.push_back({kChanged, e.id, DescribeOne(sg->GetObjectFromID(e.id))});
            }
            ++held[online->ClaimHolder(e.id)];
            e.kind = 0;  // (skipped below)
            ++refused;
        }
    }
    if (!fix.empty()) {
        online->SendEditBatch(-1, Pack(fix), builder);
    }
    for (const auto& h : held) {
        std::string who = online->ApiPlayerName(h.first);
        if (who.empty()) {
            who = "Another builder";
        }
        online->TellPlayer(builder, h.second == 1 ? who + " has the object you changed selected: your change was put back"
                                                  : who + " has " + std::to_string(h.second) + " of the objects you changed selected: those changes were put back");
    }

    applying = true;
    std::vector<Entry> result;
    std::set<int> removed;
    bool made = false;
    for (const Entry& e : entries) {
        if (e.kind == kRemoved && sg->DoesObjectWithIdExist(e.id)) {
            Remove(sg, e.id);
            known_.erase(e.id);
            removed.insert(e.id);
            result.push_back({kRemoved, e.id, std::string()});
        }
    }
    for (const Entry& e : entries) {
        if (e.kind != kChanged) {
            continue;
        }
        EntityDescription desc;
        if (!UnpackDesc(e.bytes, &desc) || !sg->DoesObjectWithIdExist(e.id) || DescID(desc) != e.id) {
            if (!sg->DoesObjectWithIdExist(e.id)) {
                result.push_back({kRemoved, e.id, std::string()});  // (gone meanwhile: the builder's copy goes too)
            }
            continue;
        }
        bool selected = sg->GetObjectFromID(e.id)->Selected();
        Remove(sg, e.id);
        if (Object* obj = Create(sg, desc, selected)) {
            known_[obj->GetID()] = DescribeOne(obj);
            result.push_back({kChanged, obj->GetID(), known_[obj->GetID()]});
            made = true;
        }
    }
    for (const Entry& e : entries) {
        if (e.kind == kSky) {
            ApplySky(sg, e.bytes);
            result.push_back(e);
        } else if (e.kind == kLevelParams) {
            ApplyParams(sg, e.bytes);
            result.push_back(e);
        }
    }
    std::vector<EntityDescription> fresh;
    for (const Entry& e : entries) {
        if (e.kind == kNew || e.kind == kRestore) {
            fresh.emplace_back();
            if (!UnpackDesc(e.bytes, &fresh.back())) {
                fresh.pop_back();
            } else if (e.kind == kRestore) {
                std::vector<int> ids;
                CollectIDs(fresh.back(), &ids);
                removed.insert(ids.begin(), ids.end());  // (an undone removal: its old ids back where free)
            }
        }
    }
    if (!fresh.empty()) {
        // (the kept ids first, then fresh ones around them and each other)
        std::map<int, int> remap;
        std::set<int> taken;
        std::vector<int> ids;
        for (const EntityDescription& desc : fresh) {
            CollectIDs(desc, &ids);
        }
        for (int id : ids) {
            if (removed.count(id) && !sg->DoesObjectWithIdExist(id)) {
                remap[id] = id;
                taken.insert(id);
            }
        }
        for (int id : ids) {
            if (remap.find(id) == remap.end()) {
                remap[id] = sg->GetFreeIDExcept(taken);
                taken.insert(remap[id]);
            }
        }
        for (EntityDescription& desc : fresh) {
            MapIDs(desc, [&remap](int id) {
                auto found = remap.find(id);
                return found != remap.end() ? found->second : id;
            });
            if (Object* obj = Create(sg, desc, false)) {
                known_[obj->GetID()] = DescribeOne(obj);
                result.push_back({kNew, obj->GetID(), known_[obj->GetID()]});
                made = true;
            }
        }
    }
    DescribeLevel(sg, &known_sky_, &known_params_);  // (as they are now, for our own next scan)
    applying = false;
    if (made) {
        sg->SendMessageToAllObjects(OBJECT_MSG::FINALIZE_LOADED_CONNECTIONS);
    }
    for (const Entry& e : result) {
        NoteEdited(e);
    }
    online->TallyEdits(builder, result);
    if (!result.empty()) {
        online->SendEditBatch(builder, Pack(result), -1);
    }
}

// The host's word: applied unless it is already so (our own change coming back). Ids are the
// host's: an id free here is used as it is, a taken one gets a new id and a translation
void EditSync::ApplyAsGuest(SceneGraph* sg, int origin, std::vector<Entry>& entries) {
    Online* online = Online::Instance();
    const bool ours = origin == online->OwnApiPlayerID();
    auto to_local = [online](int id) { return (int)online->GetObjectID(id); };
    applying = true;
    bool made = false;
    for (const Entry& e : entries) {
        if (e.kind == kRemoved) {
            int local = to_local(e.id);
            Remove(sg, local);
            known_.erase(local);
            online->UnlinkEditedObject(local);
        }
    }
    for (const Entry& e : entries) {
        if (e.kind != kChanged) {
            continue;
        }
        EntityDescription desc;
        if (!UnpackDesc(e.bytes, &desc)) {
            continue;
        }
        MapIDs(desc, to_local);
        int local = DescID(desc);
        std::string bytes;
        PackDesc(desc, &bytes);
        bool exists = sg->DoesObjectWithIdExist(local);
        auto found = known_.find(local);
        if (exists && found != known_.end() && found->second == bytes) {
            continue;  // (already so)
        }
        bool selected = exists && sg->GetObjectFromID(local)->Selected();
        Remove(sg, local);
        if (Object* obj = Create(sg, desc, selected)) {
            known_[obj->GetID()] = DescribeOne(obj);
            made = true;
        }
    }
    for (const Entry& e : entries) {
        if (e.kind == kSky) {
            ApplySky(sg, e.bytes);
            known_sky_ = e.bytes;
        } else if (e.kind == kLevelParams) {
            ApplyParams(sg, e.bytes);
            known_params_ = e.bytes;
        }
    }
    for (const Entry& e : entries) {
        if (e.kind != kNew) {
            continue;
        }
        EntityDescription desc;
        if (!UnpackDesc(e.bytes, &desc)) {
            continue;
        }
        // A new object's ids: the host's where free here, else new ones; either way linked, so
        // the translation is never an old pair's
        std::vector<int> ids;
        CollectIDs(desc, &ids);
        std::map<int, int> remap;
        std::set<int> taken(ids.begin(), ids.end());  // (the host's ids, where free here, are kept)
        for (int id : ids) {
            int local = id;
            if (sg->DoesObjectWithIdExist(id)) {
                local = sg->GetFreeIDExcept(taken);
                taken.insert(local);
            }
            online->LinkEditedObject(id, local);
            remap[id] = local;
        }
        MapIDs(desc, [&remap, &to_local](int id) {
            auto found = remap.find(id);
            return found != remap.end() ? found->second : to_local(id);
        });
        if (Object* obj = Create(sg, desc, ours)) {
            known_[obj->GetID()] = DescribeOne(obj);
            made = true;
            if (ours) {
                NoteNewForUndo(e.id, e.bytes);
            }
        }
    }
    applying = false;
    if (made) {
        sg->SendMessageToAllObjects(OBJECT_MSG::FINALIZE_LOADED_CONNECTIONS);
    }
}

std::string EditSync::HostDesc(const std::string& local_bytes) {
    Online* online = Online::Instance();
    if (online->IsHosting()) {
        return local_bytes;
    }
    EntityDescription desc;
    if (!UnpackDesc(local_bytes, &desc)) {
        return local_bytes;
    }
    MapIDs(desc, [online](int id) { return (int)online->GetOriginalID(id); });
    std::string out;
    PackDesc(desc, &out);
    return out;
}

void EditSync::NoteNewForUndo(int host_id, const std::string& host_desc) {
    for (auto it = undo_.rbegin(); it != undo_.rend(); ++it) {
        if (it->pending_new > 0) {
            --it->pending_new;
            it->back.push_back({kRemoved, host_id, std::string()});
            it->forth.push_back({kRestore, host_id, host_desc});
            return;
        }
    }
}

// An undo or redo goes as an edit of its own: the host applies it (and refuses what another
// builder holds); everyone gets the result, so do we
void EditSync::Replay(SceneGraph* sg, const std::vector<Entry>& entries) {
    Online* online = Online::Instance();
    if (entries.empty() || !online->IsActive()) {
        return;
    }
    if (online->IsHosting()) {
        std::vector<Entry> copy = entries;
        ApplyAsHost(sg, -1, copy);
    } else {
        online->SendEditBatch(online->OwnApiPlayerID(), Pack(entries), 0);
    }
}

void EditSync::Undo(SceneGraph* sg) {
    if (undo_.empty() || undo_.back().pending_new > 0) {
        return;  // (nothing, or the last edit's answer still on its way)
    }
    Step step = undo_.back();
    undo_.pop_back();
    Replay(sg, step.back);
    redo_.push_back(step);
}

void EditSync::Redo(SceneGraph* sg) {
    if (redo_.empty()) {
        return;
    }
    Step step = redo_.back();
    redo_.pop_back();
    Replay(sg, step.forth);
    undo_.push_back(step);
}
