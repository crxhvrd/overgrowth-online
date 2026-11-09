//-----------------------------------------------------------------------------
//           Name: edit_messages.cpp
//      Developer: Overgrowth Online (crxhvrd)
//    Description: Building a level together (see edit_messages.h)
//        License: Apache-2.0, like the rest of the engine (see LICENSE)
//-----------------------------------------------------------------------------
#include "edit_messages.h"

#include <Online/online.h>

#include <cmath>

namespace OnlineMessages {
EditBatch::EditBatch(int32_t origin, const std::string& blob)
    : OnlineMessageBase(OnlineMessageCategory::LEVEL_TRANSIENT), origin(origin), blob(blob) {
}

binn* EditBatch::Serialize(void* object) {
    EditBatch* m = static_cast<EditBatch*>(object);
    binn* l = binn_object();
    binn_object_set_int32(l, "o", m->origin);
    binn_object_set_blob(l, "b", (void*)m->blob.data(), (int)m->blob.size());
    return l;
}

void EditBatch::Deserialize(void* object, binn* l) {
    EditBatch* m = static_cast<EditBatch*>(object);
    binn_object_get_int32(l, "o", &m->origin);
    void* data = nullptr;
    int size = 0;
    if (binn_object_get_blob(l, "b", &data, &size) && data != nullptr && size > 0) {
        m->blob.assign(static_cast<const char*>(data), (size_t)size);
    } else {
        m->blob.clear();
    }
}

// Queued: applied in Online::LateUpdate (applying runs level scripts; here the peers are locked)
void EditBatch::Execute(const OnlineMessageRef& ref, void* object, PeerID peer) {
    EditBatch* m = static_cast<EditBatch*>(object);
    Online* online = Online::Instance();
    if (online->IsHosting()) {
        online->edit_sync.Queue(online->ApiPlayerID(peer), (int)peer, m->blob);  // (the sender as the host knows it)
    } else {
        online->edit_sync.Queue(m->origin, (int)peer, m->blob);
    }
}

void* EditBatch::Construct(void* mem) {
    return new (mem) EditBatch(-1, std::string());
}

void EditBatch::Destroy(void* object) {
    static_cast<EditBatch*>(object)->~EditBatch();
}

BuildStateMessage::BuildStateMessage(bool building, const std::vector<int>& list, int saved_ago, int autosaved_ago, int unsaved)
    : OnlineMessageBase(OnlineMessageCategory::LEVEL_TRANSIENT), building(building), builders(list.begin(), list.end()), saved_ago(saved_ago), autosaved_ago(autosaved_ago), unsaved(unsaved) {
}

binn* BuildStateMessage::Serialize(void* object) {
    BuildStateMessage* m = static_cast<BuildStateMessage*>(object);
    binn* l = binn_object();
    binn_object_set_bool(l, "on", m->building);
    binn* list = binn_list();
    for (int32_t id : m->builders) {
        binn_list_add_int32(list, id);
    }
    binn_object_set_list(l, "who", list);
    binn_free(list);
    binn_object_set_int32(l, "sv", m->saved_ago);
    binn_object_set_int32(l, "as", m->autosaved_ago);
    binn_object_set_int32(l, "un", m->unsaved);
    return l;
}

void BuildStateMessage::Deserialize(void* object, binn* l) {
    BuildStateMessage* m = static_cast<BuildStateMessage*>(object);
    BOOL on = FALSE;
    binn_object_get_bool(l, "on", &on);
    m->building = on != FALSE;
    m->saved_ago = m->autosaved_ago = -1;
    m->unsaved = 0;
    binn_object_get_int32(l, "sv", &m->saved_ago);
    binn_object_get_int32(l, "as", &m->autosaved_ago);
    binn_object_get_int32(l, "un", &m->unsaved);
    m->builders.clear();
    void* list = nullptr;
    if (binn_object_get_list(l, "who", &list) && list != nullptr) {
        int count = binn_count(list);
        for (int i = 1; i <= count && i <= 64; ++i) {
            int32_t id = 0;
            if (binn_list_get_int32(list, i, &id)) {
                m->builders.push_back(id);
            }
        }
    }
}

void BuildStateMessage::Execute(const OnlineMessageRef& ref, void* object, PeerID peer) {
    BuildStateMessage* m = static_cast<BuildStateMessage*>(object);
    Online::Instance()->ReceiveBuildState(m->building, std::vector<int>(m->builders.begin(), m->builders.end()), m->saved_ago, m->autosaved_ago, m->unsaved);
}

void* BuildStateMessage::Construct(void* mem) {
    return new (mem) BuildStateMessage(false, std::vector<int>(), -1, -1, 0);
}

void BuildStateMessage::Destroy(void* object) {
    static_cast<BuildStateMessage*>(object)->~BuildStateMessage();
}
BuilderPresence::BuilderPresence(int32_t player, const vec3& camera, bool editing, const std::vector<int>& list)
    : OnlineMessageBase(OnlineMessageCategory::LEVEL_TRANSIENT), player(player), camera(camera), editing(editing), selection(list.begin(), list.end()) {
}

binn* BuilderPresence::Serialize(void* object) {
    BuilderPresence* m = static_cast<BuilderPresence*>(object);
    binn* l = binn_object();
    binn_object_set_int32(l, "p", m->player);
    binn_object_set_vec3(l, "c", m->camera);
    binn_object_set_bool(l, "e", m->editing);
    binn* list = binn_list();
    for (int32_t id : m->selection) {
        binn_list_add_int32(list, id);
    }
    binn_object_set_list(l, "s", list);
    binn_free(list);
    return l;
}

void BuilderPresence::Deserialize(void* object, binn* l) {
    BuilderPresence* m = static_cast<BuilderPresence*>(object);
    binn_object_get_int32(l, "p", &m->player);
    binn_object_get_vec3(l, "c", &m->camera);
    BOOL editing = FALSE;
    binn_object_get_bool(l, "e", &editing);
    m->editing = editing != FALSE;
    m->selection.clear();
    void* list = nullptr;
    if (binn_object_get_list(l, "s", &list) && list != nullptr) {
        int count = binn_count(list);
        for (int i = 1; i <= count && i <= 4096; ++i) {
            int32_t id = 0;
            if (binn_list_get_int32(list, i, &id)) {
                m->selection.push_back(id);
            }
        }
    }
}

void BuilderPresence::Execute(const OnlineMessageRef& ref, void* object, PeerID peer) {
    BuilderPresence* m = static_cast<BuilderPresence*>(object);
    Online* online = Online::Instance();
    if (online->IsHosting()) {
        m->player = online->ApiPlayerID(peer);  // (as the host knows it: passed on stamped)
        if (!online->IsBuilder(m->player)) {
            return;
        }
    }
    if (std::isfinite(m->camera[0]) && std::isfinite(m->camera[1]) && std::isfinite(m->camera[2])) {
        online->ReceivePresence(m->player, m->camera, m->editing, std::vector<int>(m->selection.begin(), m->selection.end()));
    }
}

void* BuilderPresence::Construct(void* mem) {
    return new (mem) BuilderPresence(-1, vec3(0.0f), false, std::vector<int>());
}

void BuilderPresence::Destroy(void* object) {
    static_cast<BuilderPresence*>(object)->~BuilderPresence();
}

LevelFileChunk::LevelFileChunk(const std::string& name, int part, int parts, const std::string& bytes)
    : OnlineMessageBase(OnlineMessageCategory::TRANSIENT), name(name), part(part), parts(parts), bytes(bytes) {
}

binn* LevelFileChunk::Serialize(void* object) {
    LevelFileChunk* m = static_cast<LevelFileChunk*>(object);
    binn* l = binn_object();
    binn_object_set_str(l, "n", (char*)m->name.c_str());
    binn_object_set_int32(l, "i", m->part);
    binn_object_set_int32(l, "c", m->parts);
    binn_object_set_blob(l, "b", (void*)m->bytes.data(), (int)m->bytes.size());
    return l;
}

void LevelFileChunk::Deserialize(void* object, binn* l) {
    LevelFileChunk* m = static_cast<LevelFileChunk*>(object);
    char* name = nullptr;
    m->name = binn_object_get_str(l, "n", &name) && name != nullptr ? std::string(name) : std::string();
    m->part = m->parts = 0;
    binn_object_get_int32(l, "i", &m->part);
    binn_object_get_int32(l, "c", &m->parts);
    void* data = nullptr;
    int size = 0;
    if (binn_object_get_blob(l, "b", &data, &size) && data != nullptr && size > 0) {
        m->bytes.assign(static_cast<const char*>(data), (size_t)size);
    } else {
        m->bytes.clear();
    }
}

void LevelFileChunk::Execute(const OnlineMessageRef& ref, void* object, PeerID peer) {
    LevelFileChunk* m = static_cast<LevelFileChunk*>(object);
    Online* online = Online::Instance();
    if (!online->IsHosting()) {
        online->ReceiveLevelChunk(m->name, m->part, m->parts, m->bytes);
    }
}

void* LevelFileChunk::Construct(void* mem) {
    return new (mem) LevelFileChunk(std::string(), 0, 0, std::string());
}

void LevelFileChunk::Destroy(void* object) {
    static_cast<LevelFileChunk*>(object)->~LevelFileChunk();
}

BuildRequest::BuildRequest(int32_t what)
    : OnlineMessageBase(OnlineMessageCategory::LEVEL_TRANSIENT), what(what) {
}

binn* BuildRequest::Serialize(void* object) {
    BuildRequest* m = static_cast<BuildRequest*>(object);
    binn* l = binn_object();
    binn_object_set_int32(l, "w", m->what);
    return l;
}

void BuildRequest::Deserialize(void* object, binn* l) {
    BuildRequest* m = static_cast<BuildRequest*>(object);
    m->what = 0;
    binn_object_get_int32(l, "w", &m->what);
}

// (the host's: noted here, acted on in Online::LateUpdate; the peers are locked here)
void BuildRequest::Execute(const OnlineMessageRef& ref, void* object, PeerID peer) {
    BuildRequest* m = static_cast<BuildRequest*>(object);
    Online* online = Online::Instance();
    if (online->IsHosting()) {
        online->ReceiveBuildRequest(online->ApiPlayerID(peer), m->what);
    }
}

void* BuildRequest::Construct(void* mem) {
    return new (mem) BuildRequest(0);
}

void BuildRequest::Destroy(void* object) {
    static_cast<BuildRequest*>(object)->~BuildRequest();
}
}  // namespace OnlineMessages
