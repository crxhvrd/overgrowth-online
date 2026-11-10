//-----------------------------------------------------------------------------
//           Name: online.h
//      Developer: Wolfire Games LLC
//    Modified By: Overgrowth Online (crxhvrd)
//    Description:
//        License: Read below
//-----------------------------------------------------------------------------
//
//   Copyright 2022 Wolfire Games LLC
//
//   Licensed under the Apache License, Version 2.0 (the "License");
//   you may not use this file except in compliance with the License.
//   You may obtain a copy of the License at
//
//       http://www.apache.org/licenses/LICENSE-2.0
//
//   Unless required by applicable law or agreed to in writing, software
//   distributed under the License is distributed on an "AS IS" BASIS,
//   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//   See the License for the specific language governing permissions and
//   limitations under the License.
//
//-----------------------------------------------------------------------------
#pragma once

#include <Online/online_datastructures.h>
#include <Online/online_session.h>
#include <Online/online_peer.h>
#include <Online/online_file_transfer_handler.h>
#include <Online/online_message_handler.h>
#include <Online/state_machine.h>

#include <Online/Message/audio_play_sound_group_relative_gain_message.h>
#include <Online/Message/audio_play_sound_group_gain_message.h>
#include <Online/Message/audio_play_sound_loop_message.h>
#include <Online/Message/audio_play_sound_loop_at_location_message.h>
#include <Online/Message/whoosh_sound_message.h>
#include <Online/Message/audio_play_group_priority_message.h>
#include <Online/Message/audio_play_sound_message.h>
#include <Online/Message/audio_play_sound_location_message.h>
#include <Online/Message/audio_play_sound_group_relative_message.h>
#include <Online/Message/audio_play_sound_group_message.h>
#include <Online/Message/audio_play_sound_group_voice_message.h>
#include <Online/Message/audio_play_sound_impact_item_message.h>
#include <Online/Message/pcs_build_version_request_message.h>
#include <Online/Message/pcs_build_version_message.h>
#include <Online/Message/pcs_loading_completed_message.h>
#include <Online/Message/pcs_client_parameters_message.h>
#include <Online/Message/pcs_session_parameters_message.h>
#include <Online/Message/pcs_assign_player_id.h>
#include <Online/Message/pcs_file_transfer_metadata_message.h>
#include <Online/Message/set_player_state.h>
#include <Online/Message/remove_player_state.h>
#include <Online/Message/sp_string_message.h>
#include <Online/Message/sp_union_message.h>
#include <Online/Message/sp_remove_message.h>
#include <Online/Message/sp_rename_message.h>
#include <Online/Message/chat_entry_message.h>
#include <Online/Message/set_object_enabled_message.h>
#include <Online/Message/player_input_message.h>
#include <Online/Message/attach_to_message.h>
#include <Online/Message/camera_transform_message.h>
#include <Online/Message/test_message.h>
#include <Online/Message/character_snapshots.h>
#include <Online/char_snapshot.h>
#include <Objects/riggedobject.h>
#include <Objects/movementobject.h>
#include <Online/Message/morph_target_update.h>
#include <Online/Message/item_update.h>
#include <Online/Message/env_object_update.h>
#include <Online/Message/cut_line.h>
#include <Online/Message/angelscript_data.h>
#include <Online/Message/angelscript_object_data.h>
#include <Online/Message/material_sound_event.h>
#include <Online/Message/host_session_flag.h>
#include <Online/Message/timed_slow_motion.h>
#include <Online/Message/create_entity.h>
#include <Online/Message/remove_object.h>
#include <Online/Message/set_avatar_palette.h>
#include <Online/Message/send_level_message.h>
#include <Online/Message/editor_transform_change.h>
#include <Online/Message/ping.h>
#include <Online/Message/pong.h>
#include <Online/Message/combat_messages.h>
#include <Online/Message/script_message.h>
#include <Online/Message/edit_messages.h>
#include <Online/edit_sync.h>
#include <set>

#include <Math/vec3.h>
#include <Math/mat4.h>

#include <Game/connection_closed_reason.h>
#include <Game/levelinfo.h>

#include <UserInput/input.h>
#include <Math/quaternions.h>
#include <Utility/block_allocator.h>
#include <Internal/filesystem.h>
#include <Network/net_framework.h>

#include <cstdint>
#include <list>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <queue>
#include <array>
#include <list>
#include <thread>
#include <mutex>
#include <atomic>
#include <deque>

class MovementObject;
class SceneGraph;
class EnvObject;
class Object;
class ItemObject;
class Engine;
struct PlayerInput;

struct ZSTD_CCtx_s;
typedef struct ZSTD_CCtx_s ZSTD_CCtx;
struct ZSTD_DCtx_s;
typedef struct ZSTD_DCtx_s ZSTD_DCtx;

using std::array;
using std::endl;
using std::list;
using std::lock_guard;
using std::map;
using std::mutex;
using std::numeric_limits;
using std::pair;
using std::runtime_error;
using std::string;
using std::stringstream;
using std::thread;
using std::to_string;
using std::unordered_map;
using std::unordered_set;
using std::vector;

using namespace OnlineMessages;

typedef OnlineMessageHandlerTemplate<
    TestMessage,
    AudioPlayGroupPriorityMessage,
    AudioPlaySoundLoopMessage,
    AudioPlaySoundGroupGainMessage,
    AudioPlaySoundLocationMessage,
    AudioPlaySoundMessage,
    AudioPlaySoundLoopAtLocationMessage,
    AudioPlaySoundGroupRelativeMessage,
    AudioPlaySoundGroupMessage,
    AudioPlaySoundGroupVoiceMessage,
    AudioPlaySoundImpactItemMessage,
    AudioPlaySoundGroupRelativeGainMessage,
    WhooshSoundMessage,
    ChatEntryMessage,
    SetObjectEnabledMessage,
    PlayerInputMessage,
    AttachToMessage,
    CameraTransformMessage,
    CharacterSnapshots,
    PCSBuildVersionRequestMessage,
    PCSBuildVersionMessage,
    PCSLoadingCompletedMessage,
    PCSSessionParametersMessage,
    PCSClientParametersMessage,
    PCSAssignPlayerID,
    PCSFileTransferMetadataMessage,
    SetPlayerState,
    RemovePlayerState,
    SPStringMessage,
    SPUnionMessage,
    SPRemoveMessage,
    SPRenameMessage,
    MorphTargetUpdate,
    ItemUpdate,
    EnvObjectUpdate,
    CutLine,
    AngelscriptData,
    AngelscriptObjectData,
    MaterialSoundEvent,
    HostSessionFlag,
    TimedSlowMotion,
    CreateEntity,
    RemoveObject,
    SetAvatarPalette,
    SendLevelMessage,
    EditorTransformChange,
    Ping,
    Pong,
    HitRequest,
    HitResult,
    OwnerOverride,
    ItemLaunch,
    ItemHit,
    ItemDrop,
    AvatarCharacter,
    ItemUnstick,
    ScriptMessage,
    EditBatch,
    BuildStateMessage,
    BuilderPresence,
    BuildRequest,
    LevelFileChunk>
    OnlineMessageHandler;

extern OnlineMessageHandler message_handler;

// Overgrowth Online: the Steam lobby to join at start ("+connect_lobby <id>": an accepted invite)
extern std::string g_launch_lobby;

class Online {
   public:
    int compression_level;

    // Test bridge: traffic counters and the network simulator. Latency and
    // jitter are one-way and applied by the sender (both peers run it); loss
    // drops unreliable messages only. Order per connection is kept.
    std::atomic<uint64_t> net_bytes_out{0};
    std::atomic<uint64_t> net_bytes_in{0};
    std::atomic<uint64_t> net_msgs_out{0};
    std::atomic<uint64_t> net_msgs_in{0};
    std::atomic<uint64_t> net_sim_dropped{0};
    std::atomic<int> net_sim_latency_ms{0};
    std::atomic<int> net_sim_jitter_ms{0};
    std::atomic<float> net_sim_loss{0.0f};
    // Per connection on top (a host with guests on different pings)
    std::atomic<int> net_sim_conn_latency_ms[256]{};
    std::atomic<int> net_sim_conn_jitter_ms[256]{};
    double test_nan_stream_until_ms = 0.0;  // test bridge: our captures go out non-finite until then
    // Test bridge: a broken package to the first peer (0 garbage, 1 compressed
    // garbage, 2 message type out of range up, 3 negative)
    void TestSendMalformed(int mode);
    uint64_t snapshot_non_finite = 0;        // records dropped for a NaN in them
    uint64_t snapshot_other_skeleton = 0;    // records of another body than this copy has (mid character switch)
    uint64_t net_malformed = 0;              // packages dropped unread
    uint64_t net_refused = 0;                // messages a guest may not send
    // A hotspot's script runs (enter/exit, its update: a lethal water's per-frame
    // kill): what it does to a character another machine runs is skipped (its
    // owner runs the same hotspot for it), and to one just revived (the
    // collision list is a frame old: it still "touches" the zone it was carried
    // out of)
    int hotspot_event_depth = 0;

    // The network thread queues connections to close; the main thread closes
    // them (LateUpdate): closing deletes the player's character
    std::mutex close_queue_lock;

    // Character streaming (ownership): the newest packed record of every
    // character we send: the ones we simulate (captured at 30 Hz) and, on the
    // host, the players' characters it relays. Main thread only.
    struct StreamRecord {
        std::vector<uint8_t> bytes;
        std::vector<uint8_t> lite;  // the same without vitals (our own captures)
        uint32_t src_ms = 0;
        uint64_t hash = 0;  // content without id and time: same hash = nothing moved
        vec3 position;
        PlayerID owner = 0;
        int vitals_version = 0;    // changes when its vitals do
        CommonObjectID target = -1;    // whom it fights (its target, who hit it last)
        CommonObjectID attacker = -1;
        bool player = false;  // a player's character: never thinned below half rate
    };
    struct StreamSent {
        uint32_t src_ms = 0;
        uint64_t hash = 0;
        double sent_ms = -1e9;
        int vitals_version = -1;
        int vitals_repeats = 0;
        double vitals_ms = -1e9;
    };
    struct StreamOwned {
        int captures = 0;
        vector<int> sent_morphs;  // the morph values last streamed (1/200 steps)
        CharSnapshot::Frame last_sent;  // the pose last streamed (idle characters are not resent)
        bool has_last = false;
        CharSnapshot::Vitals last_vitals;
        int vitals_version = 0;
        bool idle = false;  // captures skipped since the last record
    };
    std::atomic<uint64_t> snapshot_idle_skips{0};
    unordered_map<CommonObjectID, StreamRecord> stream_latest;
    map<std::pair<PeerID, CommonObjectID>, StreamSent> stream_sent;
    unordered_map<CommonObjectID, StreamOwned> stream_owned;
    double stream_next_capture_ms = 0.0;
    std::atomic<uint64_t> snapshot_records_out{0};
    std::atomic<uint64_t> snapshot_records_in{0};
    std::atomic<uint64_t> snapshot_bytes_out{0};
    std::atomic<uint64_t> hits_requested{0};   // attacks on characters other machines run
    std::atomic<uint64_t> hits_resolved{0};    // attacks on ours, decided here
    std::atomic<uint64_t> hits_out_of_range{0};  // the defender found the attacker out of reach
    std::atomic<uint64_t> hit_results{0};      // answers about our attacks
    std::atomic<uint64_t> takeovers{0};        // characters we took for a throw, choke or drag

    // Temporary ownership: characters handed to another machine for a throw,
    // choke or drag (main thread only)
    struct OwnerOverrideEntry {
        PlayerID owner = 0;
        double since_ms = 0.0;
        double max_ms = 0.0;
        double settled_ms = -1.0;  // ours: since when it is free and settled (-1 not)
    };
    unordered_map<CommonObjectID, OwnerOverrideEntry> owner_overrides;
    unordered_map<CommonObjectID, double> handed_back_ms;  // ours, just given back: not taken again at once
    std::atomic<uint64_t> snapshot_bones_captured{0};  // bones in captured records
    std::atomic<uint64_t> snapshot_positions_sent{0};  // of those, positions the joints did not predict

    // Test bridge: per tracked character, every step: our clock, the owner
    // time shown (proxies; ours = our clock), position, mean physics bone
    struct TrackSample {
        double clock_ms;
        double render_src_ms;
        vec3 position;
        vec3 bones;
    };
    unordered_map<ObjectID, vector<TrackSample>> track_samples;

    string level_name;
    string preloaded_level;      // Overgrowth Online (client): the level the host told us as it started loading
    bool preload_loaded = false;  // ... and we have finished loading it
    string campaign_id;
    bool host_started_level;

    OnlineSession* online_session;

   private:
    NetFramework net;

    OnlineFileTransferHandler online_file_transfer_handler;

    vector<char> compressed_serialization_buffer;
    thread socket_thread;
    unordered_map<string, uint32_t> states;  // This is angelscript states, these need to exist independent of Multiplayer to allow for states to be set while not hosting to transfer when starting to host
    unordered_map<CommonObjectID, ObjectID> to_object_id;
    unordered_map<ObjectID, CommonObjectID> to_common_id;

    bool attach_avatar_camera;
    MultiplayerMode mode;
    uint64_t initial_ts;
    uint32_t next_available_state_id;
    bool loading;

    PeerID next_free_peer_id;
    uint32_t tick_period;
    bool p2p_sockets_active = false;
    unordered_set<NetListenSocketID> listen_sockets;

    float no_data_interpstep_override = 0.5f;
    uint32_t last_update_wall_ticks = 0;
    uint32_t last_quarter_update_wall_ticks = 0;
    string default_hot_join_characte_path;

    ZSTD_CCtx* zstdCContext;
    ZSTD_DCtx* zstdDContext;

    map<string, KeyState> client_key_down_map_state;

   public:
    static Online* Instance() {
        static Online online;
        return &online;
    }

    Online();
    void Dispose();

    bool IsClient() const;
    bool IsActive() const;
    bool IsHosting() const;
    bool IsAwaitingShutdown() const;
    uint32_t GetPlayerCount();

    void StartMultiplayer(MultiplayerMode multiplayerMode);
    void StartHostingMultiplayer();

    void QueueStopMultiplayer();
    void StopMultiplayer();
    void StopListening();

    vector<PlayerState> GetPlayerStates();

    void CloseConnection(NetConnectionID conn_id, ConnectionClosedReason reason);

    bool TryGetPlayerState(PlayerState& player_state, ObjectID object_id) const;

    void AssignNewControllerForPlayer(PlayerID player_id);
    void ClearOnlineSession();

    template <typename Message, typename... Args>
    OnlineMessageRef& CreateMessage(Args... args);
    void Send(const OnlineMessageRef& message);
    void SendTo(NetConnectionID target, const OnlineMessageRef& message);
    template <typename Message, typename... Args>
    void Send(Args... args);
    template <typename Message, typename... Args>
    void SendTo(NetConnectionID target, Args... args);

    void SendIntFloatScriptParam(uint32_t i, const string& key, const ScriptParam& param);
    void SendStringScriptParam(uint32_t i, const string& key, const ScriptParam& param);
    void SendScriptParam(uint32_t id, const string& key, const ScriptParam& param);
    void SendScriptParamMap(uint32_t id, const ScriptParamMap& param);
    bool GetHostAllowsClientEditor() const;
    void SetIfHostAllowsClientEditor(bool mode);
    void SetDefaultHotJoinCharacter(const string& path);
    string GetDefaultHotJoinCharacter() const;
    void SessionStarted(bool host_started_the_level);
    bool ForceMapStartOnLoad() const;
    bool AllClientsReady();
    ScriptParams* GetScriptParamsFromID(ObjectID id);
    void UpdateMovementObjectFromID(uint32_t id);
    void SocketMessageApplyLevelChange();
    bool SetAvatarCameraAttachedMode(bool mode);
    bool IsAvatarCameraAttached() const;
    void SendLevelMessage(const string& msg);
    // Overgrowth Online: level-script channel ("ohl" host -> clients, "ohr" client -> host)
    string TranslateIDs(const string& msg, bool to_common) const;
    bool SendLevelRequest(const string& msg);  // client: true if it went to the host
    double last_role_message_ms = -1e9;
    bool sent_role = false;
    bool own_state_missing_logged = false;  // (GetOwnPlayerState: said once while there's none)
    // (the script API: what scripts sent, waiting for the update; what came for them; the players
    // they were last told about; per guest, the host's message count this second)
    struct ScriptApiOutgoing {
        int to;
        std::string channel, text;
    };
    struct ScriptApiDelivery {
        int from;
        std::string channel, text;
    };
    std::vector<ScriptApiOutgoing> script_api_outgoing;
    std::vector<ScriptApiDelivery> script_api_deliveries;
    std::map<int, std::string> script_api_players;
    std::map<PeerID, std::pair<double, int>> script_api_rate;
    int script_api_dropped = 0;
    // (building together: the state, the guests caught up with the edits, the editor's last state here)
    bool time_frozen_ = false;  // (the host's Freeze time: paused for everyone)
    std::vector<int> builders_;
    bool build_state_dirty_ = false;
    double last_build_state_ms_ = -1e9;
    double last_edit_scan_ms_ = 0.0;
    bool editor_was_open_ = false;
    std::set<PeerID> edit_caught_up_;
    double last_presence_ms_ = -1e9;
    double last_autosave_ms_ = 0.0;
    int autosave_mark_ = 0;
    bool presence_sent_editing_ = false;
    // (the level's save state: the host's own, or the host's last word on a guest)
    double saved_ms_ = -1.0, autosaved_ms_ = -1.0;  // -1: not since the level loaded
    int guest_saved_ago_ = -1, guest_autosaved_ago_ = -1, guest_unsaved_ = 0;
    double guest_save_state_ms_ = 0.0;
    // (edit lines for the chat: per player, what it placed, changed and removed lately)
    struct EditTally {
        std::set<int> placed, changed, removed;
        bool sky = false, params = false;
        double first_ms = 0.0, last_ms = 0.0;
    };
    std::map<int, EditTally> edit_tallies_;
    // (reflection captures re-rendered once edits pause; the navmesh rebuilt going to Play)
    int refl_mark_ = 0, nav_mark_ = 0;
    double refl_edit_ms_ = 0.0;
    bool refl_pending_ = false, nav_current_unsaved_ = false;
    // (a builder's save request waiting for the host's answer; ours, not sent again too soon)
    int save_request_from_ = -1;
    bool save_request_told_ = false;
    double last_save_request_ms_ = -1e9;
    void MaybeRebuildNavMesh(SceneGraph* sg);
    void FlushEditTallies(bool all);
    double rebaseline_at_ms_ = -1.0;  // (after build mode switches: what's there then is the baseline)
    bool build_on_load_ = false;      // (Build a level: the hosted level opens in build mode)
    std::set<int> auto_builders_;     // (players made builders by everyone_builds, once each)
    // The build session (0.41.19, realtime): building is allowed in it; the game runs on while
    // anyone edits, every builder opens their own editor (F1) when they like, and the host may
    // freeze time for everyone (time_frozen_)
    bool build_session_ = false;        // host: building allowed in this session; guest: the host says so
    bool build_state_seen_ = false;     // guest: the first build state (the welcome line)
    double last_hint_ms_ = -1e9;        // a refused F1's line, not more than one a second and a half
    void Hint(const std::string& text);
    // (a level file on its way from the host; the ones that arrived: the host's name -> ours)
    std::string incoming_level_;
    std::vector<std::string> incoming_parts_;
    std::map<std::string, std::string> received_levels_;
    // (the host: who got the level's file with the level change; their handshake that follows doesn't
    // send it again)
    std::string level_file_broadcast_;
    std::set<HSteamNetConnection> level_file_broadcast_to_;
    void SendAvatarPaletteChange(const OGPalette& palette, ObjectID object_id);
    bool IsAvatarPossessed(ObjectID avatar_id);
    void PossessAvatar(PlayerID player_id, ObjectID object_id);
    ObjectID CreateCharacter();
    void SetNoDataInterpStepOverRide(float override);
    float GetNoDataInterpStepOverRide() const;
    void LateUpdate(SceneGraph* scenegraph);

    void SyncHostSessionFlags();
    void GenerateEnvObjectSyncPackages(NetConnectionID conn, SceneGraph* graph);
    void GenerateMovementSyncPackages(NetConnectionID conn, SceneGraph* graph);
    void GenerateStateForNewConnection(NetConnectionID conn);

    bool CheckLoadedMapAndCampaignState(const string& campagin, const string& level_name);
    void SetLevelLoaded();
    uint32_t IncomingMessageCount() const;
    uint32_t OutgoingMessageCount() const;
    bool GetIfPendingAngelScriptUpdates();
    bool GetIfPendingAngelScriptStates();
    AngelScriptUpdate GetAngelScriptUpdate();
    void MoveAngelScriptQueueForward();
    void MoveAngelStateQueueForward();
    AngelScriptUpdate GetAngelScriptStates();
    uint32_t AddSyncState(uint32_t state, const vector<char>& data);
    uint32_t RegisterMPState(const string& state);
    void SetTickperiod(const uint32_t& tickperiod);
    void GetConnectionStatus(const Peer& peer, ConnectionStatus* status) const;

    void RegisterHostObjectID(CommonObjectID hostid, ObjectID clientid);
    void DeRegisterHostClientIDTranslation(ObjectID clientid);
    ObjectID GetObjectID(CommonObjectID hostid) const;
    CommonObjectID GetOriginalID(ObjectID clientid) const;
    void ClearIDTranslations();

    bool SendingRemovePackages() const;
    void RemoveObject(Object* o, ObjectID my_id);
    bool NetworkRemoveableType(Object* o) const;
    void ChangeLevel(const string& id);
    void PerformLevelChangeCleanUp();

    const vector<Peer> GetPeers();
    vector<Peer>::iterator GetPeerIt(NetConnectionID conn_id);
    bool IsLocalAvatar(const ObjectID avatar) const;
    PlayerID GetAvatarOwner(const ObjectID avatar) const;  // whose machine simulates it
    bool IsPlayerCharacter(ObjectID id) const;  // someone's avatar (ours too)
    // The host about a guest's message naming this object: only its own character
    // (or what that holds); the host's own word and a client's view always pass
    bool PeerMayChange(PeerID from, ObjectID id) const;
    bool HostOnlyMessage(OnlineMessageType type) const;
    void StreamCharacters(SceneGraph* scenegraph);
    void ReceiveSnapshots(PeerID from, const uint8_t* data, size_t size);

    // ---- The script API for mods (Online_* in asfuncs.cpp; Docs/Markdown/Overgrowth Online
    // Script API.md). Players by the API's ids: the host 0, guests 1.. (inside, the host's
    // PlayerID is 255 and a guest's peer is its player)
    static const int kScriptApiVersion = 1;
    static const size_t kScriptChannelMax = 64;
    static const size_t kScriptTextMax = 16384;
    int ApiPlayerID(PlayerID id) const { return id == (PlayerID)-1 ? 0 : (int)id; }
    int OwnApiPlayerID() const;
    std::vector<int> ApiPlayerIDs() const;
    std::string ApiPlayerName(int player) const;
    ObjectID ApiPlayerCharacter(int player) const;
    int CharacterApiPlayer(ObjectID character) const;
    // to: a player, or ScriptMessage::kEveryone (every other player); false: refused (no session,
    // a reserved or oversized channel or text). Sent with the next frame's update
    bool SendScriptMessage(int to, const std::string& channel, const std::string& text);
    bool AcceptScriptMessage(PeerID from, const std::string& channel, const std::string& text);  // host: a guest's
    void QueueScriptDelivery(int from, const std::string& channel, const std::string& text);
    void RelayScriptMessage(const OnlineMessageRef& ref, PeerID from);  // host, peers locked
    void UpdateScriptApi();  // LateUpdate: sends, then deliveries and player events to the scripts

    // ---- Building a level together (edit_sync.h, docs_mp_editing_plan.md). The host owns the
    // level. In a build session builders use the editor while the game runs on (realtime); the
    // host may freeze time for everyone
    EditSync edit_sync;
    bool BuildSession() const { return build_session_; }
    bool TimeFrozen() const { return time_frozen_; }
    bool IsBuilder(int player) const;  // (the host always)
    bool MayUseEditor() const;          // offline and the host: yes; a guest: a builder in a build session
    std::vector<int> Builders() const { return builders_; }
    void SetBuildSession(bool on);        // host: building allowed in this session
    void SetTimeFrozen(bool on);          // host: paused for everyone
    void SetBuilder(int player, bool on);  // host
    void ReceiveBuildState(bool building, const std::vector<int>& builders, int saved_ago, int autosaved_ago, int unsaved, bool frozen);  // guest
    // Host: the level from its start for everyone (the AI paths rebuilt first when edits changed them)
    void RestartLevel(SceneGraph* sg);
    // Host: the AI's paths rebuilt for the edited level now (a few seconds on a big level: the host
    // stops meanwhile); NavMeshOutOfDate: edits since the last build
    void RebuildNavMesh(SceneGraph* sg) { MaybeRebuildNavMesh(sg); }
    bool NavMeshOutOfDate(SceneGraph* sg) const;
    // The line at the top of every screen in a build session: what the keys do for this player and
    // whether time runs ("" outside one)
    std::string SessionBanner() const;
    // origin: whose edit (the API's player ids); to: -1 everyone (the host) or a player; a guest's goes to the host
    void SendEditBatch(int origin, const std::string& blob, int to);
    // A guest's translation for an object the edit sync made or removed, set or cleared both ways
    // (a stale pair from an object gone earlier is dropped: it once moved the wrong object)
    void LinkEditedObject(CommonObjectID host, ObjectID local);
    void UnlinkEditedObject(ObjectID local);
    // Builders' presence (phase 2): where each is and what it selected; claims: an object is the
    // first selector's until it lets go, and the host refuses others' changes to it
    void ReceivePresence(int player, const vec3& camera, bool editing, const std::vector<int>& selection);
    bool ClaimedByOther(int shared_id, int player) const;  // host
    struct Presence {
        vec3 camera;
        bool editing = false;
        std::vector<int> selection;  // shared ids
        double ms = -1e9;
    };
    std::map<int, Presence> presence_;
    std::map<int, int> claims_;  // host: shared id -> player
    void UpdateEditing(SceneGraph* sg);  // LateUpdate: batches, scans, the build state, joiners' catch-up
    void AutosaveSoon() { last_autosave_ms_ = -1e9; autosave_mark_ = -1; }  // (the test bridge's)
    // A level loaded (the engine, with edit_sync.Reset): nothing saved, tallied or pending
    void NoteLevelLoaded(SceneGraph* sg);
    // The host saved the level (MapEditor::SaveLevel): everyone told, the navmesh saved beside it
    // when it's current
    void NoteHostSaved(SceneGraph* sg);
    // The level's save state in words ("Saved 3 min ago, 12 unsaved changes, autosaved 1 min ago")
    std::string SaveStateText() const;
    int SavedAgo() const;       // seconds; -1: not since the level loaded
    int AutosavedAgo() const;   // the same
    int UnsavedCount() const;
    // Edit lines for the chat (host): "Bob placed 3 objects, changed 1 object"
    void TallyEdits(int player, const std::vector<EditSync::Entry>& entries);
    bool edit_chat = true;
    // A line in one player's chat (the host's: sent to that player; our own: shown here)
    void TellPlayer(int player, const std::string& text);
    int ClaimHolder(int shared_id) const;  // host: -1 none
    bool EditRebaselinePending() const { return rebaseline_at_ms_ >= 0.0; }
    // Saving while building: a builder asks (Ctrl+S); the host saves at once when builders may
    // save and no dialog is needed, else it gets a prompt (AnswerSaveRequest)
    void RequestSave();                               // guest
    void ReceiveBuildRequest(int player, int what);   // host, inside message handling
    int SaveRequestFrom() const { return save_request_from_; }
    void AnswerSaveRequest(bool save);                // host
    bool builders_may_save = false;
    // Build a level (the Multiplayer menu): the next level hosted opens in build mode, the host in
    // the editor, and everyone who joins may build (the host can untick each)
    void BuildOnLoad() { build_on_load_ = true; }
    // F1 (Engine::PressEditorKey): open, whether it asked to open the editor; done, whether the
    // editor did. A guest's refused F1 says why
    void EditorKeyPressed(bool open, bool done);
    bool everyone_builds = false;
    // A level the guests can't have (from the host's write folder: its own, saved) goes to them as a
    // file before they're told to load it: to one guest joining (conn) or to all (conn 0). A guest
    // keeps it in its write folder (Data/Levels/_online/) and loads that
    void SendLevelFile(HSteamNetConnection conn, const std::string& level);
    void ReceiveLevelChunk(const std::string& name, int part, int parts, const std::string& bytes);  // guest
    std::string ReceivedLevel(const std::string& name) const;  // guest: ours for the host's name, or ""
    int level_files_sent = 0, level_files_received = 0;  // (the test bridge's)
    // The levels saved in the write folder (Data/Levels/..., the autosaves too), for the menu
    static std::vector<std::string> SavedLevels();
    int nav_builds = 0, refl_refreshes = 0, refl_last_count = 0;  // (the test bridge's)
    double nav_build_ms = 0.0;
    std::vector<std::string> chat_log;  // (the test bridge's: the last chat lines here)

    // Combat across machines (Phase 4)
    void SendToOwnerOf(ObjectID id, const OnlineMessageRef& ref, bool peers_locked);
    void RelayFromPeer(const OnlineMessageRef& ref, PeerID from);  // host, inside message handling: to every other client
    void SendHitRequest(ObjectID target, const string& type, const string& attack_path, vec3 dir, vec3 pos,
                        ObjectID attacker, float damage_mult, float knockback_mult, float max_range);
    void ReceiveHitRequest(const OnlineMessageRef& ref, OnlineMessages::HitRequest* m, PeerID from);
    void ReceiveHitResult(const OnlineMessageRef& ref, OnlineMessages::HitResult* m, PeerID from);
    int NetworkWasHit(MovementObject* target, const string& type, const string& attack_path, vec3 dir, vec3 pos,
                      ObjectID attacker, float damage_mult, float knockback_mult);
    bool TakeOver(ObjectID id, float max_seconds);
    void ReleaseTakeOver(ObjectID id);
    void ReceiveOwnerOverride(CommonObjectID cid, PlayerID owner, bool active, float max_seconds, PeerID from);
    void UpdateTakeOvers(SceneGraph* scenegraph);

    // Thrown weapons: the host flies them; hits on characters run elsewhere go to their owners
    vector<ObjectID> pending_item_launches;  // client, main thread: sent at the end of the step
    std::atomic<uint64_t> item_launches{0};
    std::atomic<uint64_t> item_hits{0};
    void QueueItemLaunch(ObjectID item);
    void SendPendingItemLaunches(SceneGraph* scenegraph);
    void ReceiveItemLaunch(OnlineMessages::ItemLaunch* m, PeerID from);
    void SendItemHit(ObjectID character, ObjectID item, const vec3& point, const string& material, int type, const vec3& velocity,
                     const vec3& stick_start, const vec3& stick_end, const mat4& stick_transform);
    void ReceiveItemHit(OnlineMessages::ItemHit* m);
    void SendItemDrop(ObjectID character, ObjectID item);  // queued: called inside message handling
    void SendItemUnstick(ObjectID character, ObjectID item);  // queued too: a weapon came out of a character here
    void UnstickHere(SceneGraph* sg, ItemObject* item);     // quietly: its unstick is on the way
    vector<std::pair<ObjectID, ObjectID> > pending_drop_orders;
    vector<std::pair<CommonObjectID, CommonObjectID>> pending_unsticks;  // (character, item)
    bool applying_unstick = false;  // ours came from the network: don't send it back

    // Co-op rules (story campaigns)
    bool delivering_network_level_message = false;  // Level::Message is passing on the host's message
    bool AllowScriptReset();                        // a level script here asked for "reset"
    bool CoopCampaign();                            // the level belongs to a linear (story) campaign
    vector<ObjectID> PlayerAvatarIDs();             // every player's character here
    void CoopUpdate(SceneGraph* sg);                // host: everyone down for 5 s -> reset
    // host: a checkpoint was reached (by that character, if one did). The
    // downed get up beside them at once, before the next goal's steps run
    // (a dialogue there would stand them up where they lie)
    void CoopCheckpoint(ObjectID by = -1);
    // Every machine, its own player's character: killed where no teammate can
    // reach the body (a fall, lava, spikes, deep water, out of the world), it
    // gets up beside one who is up after a moment
    void CoopZoneRespawn(SceneGraph* sg);
    double coop_lost_since_ms = -1.0;
    ObjectID HostAvatarID() const;                  // the host's player's character here, or -1
    int GuestIndex(ObjectID id) const;              // 1, 2, ... for the guests' characters (player order), else 0
    // Cutscenes: the guests' characters are hidden on every machine while a
    // dialogue plays; when it ends each guest stands up beside the host
    void CoopCutsceneUpdate(SceneGraph* sg);
    bool CoopCutscene() const { return coop_cutscene; }
    bool coop_cutscene = false;
    vector<ObjectID> coop_hidden;
    // Spectating: our own character is down and another player's isn't, so the
    // camera follows theirs (the nearest; attack picks the next, level.as)
    void CoopSpectateUpdate(SceneGraph* sg);
    void SpectateNext() { spectate_next = true; }
    string SpectateName() const { return spectate_name; }  // whose we watch, "" none
    ObjectID spectate_target = -1;
    string spectate_name;
    bool spectate_next = false;
    vec3 spectate_center;
    float spectate_distance = 3.0f;
    // Where each player's character last stood safely (the fallback of the spot search)
    void TrackSafeGround(SceneGraph* sg);
    std::unordered_map<ObjectID, vec3> last_safe_ground;
    double last_safe_ground_ms = -1e9;
    // A client's connection to the host as it is being made (cancel closes it)
    NetConnectionID pending_connection = 0;
    bool has_pending_connection = false;
    // Server browser (public games: SteamworksMatchmaking)
    void RequestPublicGames();
    string PublicGames();
    bool JoinPublicGame(const string& lobby);
    string last_safe_level;
    void CoopReviveRequest(const string& msg);      // "ohg revive <common id> x y z [fx fz]" from the host
    void ReviveAvatar(MovementObject* mo, const vec3& pos, const vec3& facing = vec3(0.0f));
    void PlaceAvatar(ObjectID id, const vec3& pos, const vec3& facing);  // host: up at pos, wherever it runs
    double coop_all_down_ms = -1.0;
    struct PendingRevive {
        ObjectID id;
        vec3 pos;
        vec3 facing;
        double queued_ms;  // waits (3 s at most) while another machine still has it (a teammate's drag)
    };
    bool OwnPlayerCharacter(ObjectID id) const;  // our player's, whoever runs it now
    vector<PendingRevive> pending_revives;
    string coop_campaign_for;
    bool coop_campaign = false;

    // Sounds: scripts of characters this machine runs are playing (main thread)
    int owned_script_depth = 0;
    std::atomic<uint64_t> sounds_from_clients{0};  // host: client characters' sounds received
    // Sounds of characters this machine runs, from their scripts (level, hotspot and dialogue
    // scripts run on every machine: the host's copies doubled the guests' own)
    bool ShouldShareSound() const { return IsActive() && owned_script_depth > 0; }
    void ReceiveItemDrop(CommonObjectID character, CommonObjectID item);
    void ReceiveItemUnstick(const OnlineMessageRef& ref, CommonObjectID character, CommonObjectID item, PeerID from);

    // Your online character (co-op menu: mp_character, mp_fur, mp_cloth)
    // A spot for player number `index` near `base` (facing `facing`; 0: `base`
    // itself if it is safe): solid ground at about the same height, room to
    // stand, no drop beside it, not in deep water or a kill or fall-death volume.
    // FindSafeSpot says whether it found one; SafeSpotNear falls back to `base`
    bool FindSafeSpot(const vec3& base, const vec3& facing, int index, vec3* spot);
    vec3 SafeSpotNear(const vec3& base, const vec3& facing, int index);
    bool GroundHazard(const vec3& ground) const;  // a point on the ground in water, a kill or a fall-death volume
    // Hits held until the defender shows its attacker landing them
    struct HeldHit {
        OnlineMessageRef ref;
        PeerID from;
        double resolve_at_ms;
    };
    vector<HeldHit> held_hits;
    double last_link_log_ms = 0.0;  // connection stats in the log every 10 s
    void LogLinks();
    void ResolveHitRequest(OnlineMessages::HitRequest* m, bool peers_locked);
    void ResolveHeldHits();
    void SendAvatarCharacter(ObjectID character, const string& path);
    void ReceiveAvatarCharacter(CommonObjectID character, const string& path, PeerID from);  // queued to LateUpdate
    void ApplyOwnCharacter(SceneGraph* sg);  // once per level, on our own character
    vector<std::pair<CommonObjectID, string> > pending_avatar_characters;
    string own_character_applied;  // "<level>|<character id>"
    void RestoreOwnCharacter() { own_character_applied.clear(); }  // applied again (the arena randomizer ended)
    // Host: what a script creates now goes to the clients (CreateEntity, kept
    // for late joiners): once the level has loaded, alone or not. (While it
    // loads the scripts of every machine make their own: Online_AnnounceObject.)
    bool SharesCreatedObjects() const { return IsHosting() && (host_started_level || !loading); }
    void BroadcastOwnerOverride(CommonObjectID cid, PlayerID owner, bool active, float max_seconds, PeerID except, bool peers_locked);
    vector<ObjectID> GetLocalAvatarIDs() const;
    PlayerState GenerateHostPlayerState() const;

    void OnConnectionChange(NetFrameworkConnectionStatusChanged* data);

    void BroadcastChatMessage(const string& chat_message);
    void SendRawChatMessage(const string& raw_chat_entry);
    void AddLocalChatMessage(string chat_message);
    void RemoveOldChatMessages(float threshold);
    const vector<ChatMessage>& GetChatMessages() const;

    void AddPeer(NetFrameworkConnectionStatusChanged* data);
    Peer* GetPeerFromConnection(NetConnectionID conn_id);
    Peer* GetPeerFromID(PeerID peer_id);

    bool HasFreeAvatars() const;
    void UpdateObjects(SceneGraph* scenegraph_);
    uint32_t GetNumberOfFreeAvatars() const;
    uint8_t GetPlayerLimit();
    bool IsLobbyFull();
    void AddFreeAvatarsIds();
    void RemoveAllAvatarIds();
    void RemoveFreeAvatarId(uint32_t id);
    ObjectID GetFreeAvatarID();
    void AddFreeAvatarId(uint32_t id);
    void CreateListenSocketIP(const string& localAddress);
    void ConnectByIPAddress(const string& address);
    void ConnectP2P(uint64_t steam_id);  // Steam: to the host of the lobby we joined

    bool NetFrameworkHasFriendInviteOverlay() const;
    void ActivateGameOverlayInviteDialog();
    bool OpenToFriends(bool overlay);
    void UpdateFriendPresence(SceneGraph* scenegraph);
    bool SteamFriendsAvailable() const;
    std::string FriendGames();
    bool JoinFriend(const std::string& steam_id);

    void CheckPendingMessages();
    void ApplyPlayerInput();

    PlayerState GetOwnPlayerState();

    // Host and Client use, used to convert a PlayerInput binding name to a shared integer representation.
    uint8_t GetBindID(string bind_name);

   private:
    // Explicitally disable copy constructors and assignment to prevent misuse.
    Online(const Online&) = delete;
    Online& operator=(const Online&) = delete;
    Online(Online&&) = delete;
    Online& operator=(Online&&) = delete;

    // Host only function, clients get their bindings from the host.
    void AssignBindID(string bind_name);

    // Client side cache, not used on the server.
    void SendPlayerInputState();
    void SendPlayerCameraState();

    void CloseConnectionImmediate(NetConnectionID conn, ConnectionClosedReason reason);

    // Functions run inside a separate socket communication thread.
    void NetworkDataThread();
    void SendMessages();
    void SendMessageObjects();
    void HandleConnectionsToBeClosed();

    void SendMessageToConnection(NetConnectionID conn_id, char* buffer, uint32_t bytes, bool reliable, bool flush);

    struct DelayedSend {
        NetConnectionID conn_id;
        std::vector<char> data;
        bool reliable;
        double release_ms;
    };
    std::deque<DelayedSend> net_sim_queue;  // network thread only
    void FlushDelayedSends();

   public:
    // Transport-level round trip to a peer, ms (-1 if unknown)
    int GetPeerTransportPing(NetConnectionID conn_id);

   private:
    void SendStateMessages();

    int CheckNewMessages();  // messages received

    size_t CompressData(vector<char>& target_buffer, const void* source_buffer, uint32_t source_size);
    size_t DecompressData(vector<char>& target_buffer, const void* source_buffer, size_t source_size);
};

template <typename Message, typename... Args>
void Online::Send(Args... args) {
    Send(message_handler.Create<Message>(args...));
}

template <typename Message, typename... Args>
void Online::SendTo(NetConnectionID target, Args... args) {
    SendTo(target, message_handler.Create<Message>(args...));
}

template <typename Message, typename... Args>
OnlineMessageRef& Online::CreateMessage(Args... args) {
    return message_handler.Create<Message>(args...);
}
