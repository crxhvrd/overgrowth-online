//-----------------------------------------------------------------------------
//           Name: vk_queries.cpp
//      Developer: Overgrowth Online (crxhvrd)
//    Description: See vk_queries.h
//        License: Apache-2.0, like the rest of the engine (see LICENSE)
//-----------------------------------------------------------------------------
#include <volk.h>

#include "vk_queries.h"

#include <Graphics/Vulkan/vk_backend.h>
#include <Graphics/render_backend.h>
#include <Logging/logdata.h>

#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace VkQueries {
namespace {

// The pools are split in kRegions, one per frame in turn (more than the frames in
// flight: a region is reset only when its last frame is long finished)
const uint32_t kRegions = 8;
const uint32_t kOcclusionPerRegion = 512;
const uint32_t kTimestampsPerRegion = 8192;  // (the frame timers time every object: Slaver Path's frame takes over 1024)

struct Pool {
    VkQueryPool pool = VK_NULL_HANDLE;
    uint32_t per_region = 0;
    uint32_t used[kRegions] = {};
};

struct Query {
    bool timestamp = false;
    std::vector<uint32_t> slots;  // this use's (an occlusion query across passes: one per pass)
    uint64_t frame = 0;           // the frame its slots are from
    uint64_t serial = 0;          // the submit that finishes them
    bool has_value = false;       // read already (or nothing to read)
    uint64_t value = 0;
};

Pool occlusion, timestamps;
bool made = false, usable = false, shut_down = false;
uint64_t frame = 0;
bool in_pass = false;
GLuint active = 0;  // the occlusion query begun and not ended
bool slot_open = false;
bool precise = false;
double timestamp_period = 1.0;
uint64_t timestamp_mask = 0;
// (never destroyed: the engine's static destructors may call in at exit)
std::unordered_map<GLuint, Query>& queries = *new std::unordered_map<GLuint, Query>();

void LogOnce(const char* what) {
    static std::set<std::string> logged;
    if (logged.insert(what).second) {
        LOGE << "Vulkan queries: " << what << std::endl;
    }
}

uint32_t Region() {
    return (uint32_t)(frame % kRegions);
}

void ResetRegion(uint32_t region) {
    VkCommandBuffer cmd = VkBackend::TransferCommands();  // (outside a pass)
    vkCmdResetQueryPool(cmd, occlusion.pool, region * occlusion.per_region, occlusion.per_region);
    occlusion.used[region] = 0;
    if (timestamps.pool != VK_NULL_HANDLE) {
        vkCmdResetQueryPool(cmd, timestamps.pool, region * timestamps.per_region, timestamps.per_region);
        timestamps.used[region] = 0;
    }
}

uint32_t Allocate(Pool& p) {
    const uint32_t r = Region();
    if (p.used[r] >= p.per_region) {
        return UINT32_MAX;
    }
    return r * p.per_region + p.used[r]++;
}

void Read(Query& q) {
    if (q.has_value) {
        return;
    }
    uint64_t sum = 0;
    for (uint32_t slot : q.slots) {
        uint64_t v = 0;
        vkGetQueryPoolResults(VkBackend::Device(), q.timestamp ? timestamps.pool : occlusion.pool, slot, 1, sizeof(v), &v, sizeof(v),
                              VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);
        sum += q.timestamp ? (uint64_t)((double)(v & timestamp_mask) * timestamp_period) : v;
    }
    q.value = sum;
    q.has_value = true;
}

void FrameBegin() {
    if (shut_down) {
        return;
    }
    ++frame;
    // What this region still holds (finished: kRegions frames ago) is read before the reset
    const uint32_t r = Region();
    for (auto& kv : queries) {
        Query& q = kv.second;
        if (!q.has_value && !q.slots.empty() && q.frame % kRegions == r && !(kv.first == active)) {
            Read(q);
        }
    }
    ResetRegion(r);
}

void Shutdown() {
    if (occlusion.pool != VK_NULL_HANDLE) {
        vkDestroyQueryPool(VkBackend::Device(), occlusion.pool, NULL);
    }
    if (timestamps.pool != VK_NULL_HANDLE) {
        vkDestroyQueryPool(VkBackend::Device(), timestamps.pool, NULL);
    }
    occlusion.pool = timestamps.pool = VK_NULL_HANDLE;
    shut_down = true;
}

bool Ensure() {
    if (made) {
        return usable;
    }
    made = true;
    VkQueryPoolCreateInfo info = {VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
    info.queryType = VK_QUERY_TYPE_OCCLUSION;
    info.queryCount = kRegions * kOcclusionPerRegion;
    if (vkCreateQueryPool(VkBackend::Device(), &info, NULL, &occlusion.pool) != VK_SUCCESS) {
        LogOnce("no occlusion query pool (every query reads as visible)");
        return false;
    }
    occlusion.per_region = kOcclusionPerRegion;
    const uint32_t bits = VkBackend::TimestampValidBits();
    if (bits > 0) {
        info.queryType = VK_QUERY_TYPE_TIMESTAMP;
        info.queryCount = kRegions * kTimestampsPerRegion;
        if (vkCreateQueryPool(VkBackend::Device(), &info, NULL, &timestamps.pool) == VK_SUCCESS) {
            timestamps.per_region = kTimestampsPerRegion;
        } else {
            timestamps.pool = VK_NULL_HANDLE;
        }
        timestamp_mask = bits >= 64 ? ~0ull : ((1ull << bits) - 1);
        timestamp_period = VkBackend::Properties().limits.timestampPeriod;
    }
    precise = VkBackend::Features().occlusionQueryPrecise == VK_TRUE;
    VkBackend::OnFrameBegin(&FrameBegin);
    VkBackend::OnShutdown(&Shutdown);
    usable = true;
    ResetRegion(Region());  // (this frame's, now)
    return true;
}

void StartSlot() {
    auto it = queries.find(active);
    if (it == queries.end()) {
        return;
    }
    const uint32_t slot = Allocate(occlusion);
    if (slot == UINT32_MAX) {
        LogOnce("more occlusion queries in a frame than the pool holds (the rest read as visible)");
        it->second.has_value = true;
        it->second.value = 1000000;
        active = 0;
        return;
    }
    vkCmdBeginQuery(VkBackend::Commands(), occlusion.pool, slot, precise ? VK_QUERY_CONTROL_PRECISE_BIT : 0);
    it->second.slots.push_back(slot);
    slot_open = true;
}

void EndSlot() {
    auto it = queries.find(active);
    if (it == queries.end() || it->second.slots.empty()) {
        slot_open = false;
        return;
    }
    vkCmdEndQuery(VkBackend::Commands(), occlusion.pool, it->second.slots.back());
    it->second.serial = VkBackend::RecordingSerial();
    slot_open = false;
}

}  // namespace

GLuint Create(bool timestamp) {
    const GLuint id = VkStub::NewId();
    if (!shut_down) {
        queries[id].timestamp = timestamp;
    }
    return id;
}

void Delete(GLuint query) {
    if (shut_down) {
        return;
    }
    if (query == active) {
        EndOcclusion();
    }
    queries.erase(query);
}

void BeginOcclusion(GLuint query) {
    if (shut_down || !Ensure()) {
        return;
    }
    if (active != 0) {
        EndOcclusion();  // (GL: an error; the other one ends)
    }
    Query& q = queries[query];
    q.timestamp = false;
    q.slots.clear();
    q.frame = frame;
    q.serial = VkBackend::RecordingSerial();
    q.has_value = false;
    q.value = 0;
    active = query;
    if (in_pass) {
        StartSlot();
    }
}

void EndOcclusion() {
    if (shut_down || active == 0) {
        return;
    }
    if (slot_open) {
        EndSlot();
    }
    auto it = queries.find(active);
    if (it != queries.end() && it->second.slots.empty()) {
        it->second.has_value = true;  // (nothing drawn while it ran: no samples)
    }
    active = 0;
}

void WriteTimestamp(GLuint query) {
    if (shut_down || !Ensure()) {
        return;
    }
    Query& q = queries[query];
    q.timestamp = true;
    q.slots.clear();
    q.frame = frame;
    q.serial = VkBackend::RecordingSerial();
    q.has_value = false;
    q.value = 0;
    const uint32_t slot = timestamps.pool != VK_NULL_HANDLE ? Allocate(timestamps) : UINT32_MAX;
    if (slot == UINT32_MAX) {
        if (timestamps.pool != VK_NULL_HANDLE) {
            LogOnce("more timestamps in a frame than the pool holds (the rest read 0)");
        }
        q.has_value = true;
        return;
    }
    vkCmdWriteTimestamp2(VkBackend::Commands(), VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, timestamps.pool, slot);
    q.slots.push_back(slot);
}

bool Available(GLuint query) {
    if (shut_down || !usable) {
        return true;
    }
    auto it = queries.find(query);
    if (it == queries.end() || it->second.has_value) {
        return true;
    }
    if (query == active) {
        return false;
    }
    return VkBackend::CompletedSerial() >= it->second.serial;
}

uint64_t Result(GLuint query) {
    if (shut_down || !usable) {
        return 1000000;  // (no pools: visible)
    }
    auto it = queries.find(query);
    if (it == queries.end()) {
        return 0;
    }
    Query& q = it->second;
    if (q.has_value) {
        return q.value;
    }
    if (query == active) {
        LogOnce("a result asked for before its query ended");
        return 0;
    }
    if (VkBackend::CompletedSerial() < q.serial) {
        VkBackend::WaitSerial(q.serial);  // (submits it first if it's still being recorded)
    }
    Read(q);
    return q.value;
}

void OnPassBegin() {
    in_pass = true;
    if (active != 0 && !shut_down) {
        StartSlot();
    }
}

void OnPassEnd() {
    if (slot_open && !shut_down) {
        EndSlot();
    }
    in_pass = false;
}

}  // namespace VkQueries
