//-----------------------------------------------------------------------------
//           Name: online.cpp
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
#include <thread>
#include <chrono>
#include "online.h"

#include <Game/level.h>
#include <Game/connection_closed_reason.h>

#include <Math/vec3math.h>
#include <Math/vec4math.h>

#include <Steam/steamworks.h>
#include <Steam/steamworks_util.h>

#include <Objects/object.h>
#include <Objects/envobject.h>
#include <Objects/itemobject.h>
#include <Objects/movementobject.h>
#include <Objects/envobjectattach.h>
#include <Objects/riggedobject.h>
#include <Objects/cameraobject.h>
#include <Objects/hotspot.h>

#include <Editors/map_editor.h>
#include <Editors/actors_editor.h>

#include <Online/online_file_transfer_handler.h>
#include <Online/online_utility.h>
#include <Online/package_header.h>

#include <Compat/compat.h>
#include <Compat/time.h>

#include <Graphics/pxdebugdraw.h>
#include <Main/scenegraph.h>
#include <Internal/config.h>
#include <Internal/modloading.h>
#include <Game/scriptablecampaign.h>
#include <Physics/bulletworld.h>

std::string g_launch_lobby;  // (online.h)
#include <Logging/logdata.h>

#if ENABLE_STEAMWORKS
#include <isteamnetworkingutils.h>
#include <isteamfriends.h>
#endif

#include <zstd.h>
#include <zstd-1.5.0/lib/common/zstd_common.c>

#include <limits>

extern Timer game_timer;

OnlineMessageHandler message_handler;

OnlineMessageRef::OnlineMessageRef() : message_id(std::numeric_limits<OnlineMessageID>::max()) {
}

OnlineMessageRef::OnlineMessageRef(const OnlineMessageID message_id) : message_id(message_id) {
    message_handler.Acquire(message_id);
}

OnlineMessageRef::OnlineMessageRef(const OnlineMessageRef& rhs) {
    // Debug the fact that this RHS is corrupt sometimes when loading in debug.
    message_id = rhs.message_id;
    message_handler.Acquire(message_id);
}

OnlineMessageRef::~OnlineMessageRef() {
    if (IsValid()) {
        message_handler.Release(message_id);
    }
}

OnlineMessageRef& OnlineMessageRef::operator=(const OnlineMessageRef& rhs) {
    if (IsValid()) {
        message_handler.Release(message_id);
    }

    message_id = rhs.message_id;

    message_handler.Acquire(message_id);
    return *this;
}

OnlineMessageID OnlineMessageRef::GetID() const {
    return this->message_id;
}

void* OnlineMessageRef::GetData() const {
    return message_handler.GetMessageData(*this);
}

bool OnlineMessageRef::IsValid() const {
    return message_id != std::numeric_limits<OnlineMessageID>::max();
}

void Online::GetConnectionStatus(const Peer& peer, ConnectionStatus* status) const {
    return net.GetConnectionStatus(peer.conn_id, status);
}

void Online::RegisterHostObjectID(CommonObjectID hostid, ObjectID clientid) {
    to_object_id[hostid] = clientid;
    to_common_id[clientid] = hostid;
}

void Online::DeRegisterHostClientIDTranslation(ObjectID clientid) {
    // check if we need to remove a translation
    auto translation = GetOriginalID(clientid);

    if (translation) {
        to_object_id.erase(translation);
        to_common_id.erase(clientid);
    }
}

ObjectID Online::GetObjectID(CommonObjectID hostid) const {
    auto it = to_object_id.find(hostid);
    if (it != to_object_id.end()) {
        return it->second;
    } else {
        return hostid;
    }
}

CommonObjectID Online::GetOriginalID(ObjectID clientid) const {
    auto it = to_common_id.find(clientid);
    if (it != to_common_id.end()) {
        return it->second;
    } else {
        return clientid;
    }
}

void Online::ClearIDTranslations() {
    to_object_id.clear();
    to_common_id.clear();
}

bool Online::SendingRemovePackages() const {
    return !loading;
}

void Online::RemoveObject(Object* o, ObjectID my_id) {
    if (NetworkRemoveableType(o) && SendingRemovePackages()) {
        Send<OnlineMessages::RemoveObject>(my_id, o->GetType());
    }
}

bool Online::NetworkRemoveableType(Object* o) const {
    EntityType type = o->GetType();
    return type == _env_object || type == _movement_object || type == _item_object;
}

void Online::ChangeLevel(const string& id) {
    campaign_id = ModLoading::Instance().WhichCampaignLevelBelongsTo(id);
    host_started_level = false;
    level_name = id;

    RemoveAllAvatarIds();
    loading = true;
    held_hits.clear();
    coop_cutscene = false;  // (the new level's objects)
    coop_hidden.clear();
    last_safe_ground.clear();

    online_session->outgoing_messages_lock.lock();
    // Clear out Level persistent messages
    for (int i = online_session->persistent_outgoing_messages.size() - 1; i >= 0; i--) {
        OnlineMessageBase* message_base = (OnlineMessageBase*)online_session->persistent_outgoing_messages[i].GetData();
        if (message_base->cat == OnlineMessageCategory::LEVEL_PERSISTENT) {
            online_session->persistent_outgoing_messages.erase(online_session->persistent_outgoing_messages.begin() + i);
        }
    }
    online_session->outgoing_messages_lock.unlock();

    // Reset every peer to a pending connection state.
    online_session->client_connection_manager.ResetConnections();

    // Reset every player's possession
    online_session->connected_peers_lock.lock();
    for (auto& it : online_session->player_states) {
        it.second.object_id = -1;
    }
    online_session->connected_peers_lock.unlock();

    // Overgrowth Online: the clients load it now, together with us (the
    // network thread sends it while we load); the handshake that follows
    // used to tell them only once we had finished
    Send<OnlineMessages::PCSFileTransferMetadataMessage>(level_name, campaign_id, true);
}

Online::Online() : mode(MultiplayerMode::NoMultiplayer),
                   initial_ts(0),
                   next_available_state_id(1),
                   next_free_peer_id(1),
                   attach_avatar_camera(true),
                   loading(false),
                   default_hot_join_characte_path("Data/Objects/IGF_Characters/IGF_TurnerActor.xml"),
                   compression_level(10),
                   net(this),
                   host_started_level(false) {
    zstdCContext = ZSTD_createCCtx();
    zstdDContext = ZSTD_createDCtx();
}

void Online::Dispose() {
    StopMultiplayer();
}

bool Online::IsClient() const {
    return mode == MultiplayerMode::Client;
}

void Online::SendAvatarPaletteChange(const OGPalette& palette, ObjectID object_id) {
    for (const auto& it : palette) {
        Send<OnlineMessages::SetAvatarPalette>(object_id, it.color, it.channel);
    }
}

bool Online::IsAvatarPossessed(ObjectID object_id) {
    Object* object = Engine::Instance()->GetSceneGraph()->GetObjectFromID(object_id);
    if (object != nullptr && object->GetType() == EntityType::_movement_object) {
        MovementObject* mov = (MovementObject*)object;
        return mov->controlled;
    }

    LOGW << "IsAvatarPossessed was called with invalid id: " << object_id << endl;
    return false;
}

void Online::PossessAvatar(PlayerID player_id, ObjectID object_id) {
    // LOGI << std::to_string(player_id) << " possessed " << std::to_string(object_id) << std::endl;
    online_session->player_states[player_id].object_id = object_id;
    Send<OnlineMessages::SetPlayerState>(player_id, &online_session->player_states[player_id]);
}

ObjectID Online::CreateCharacter() {
    SceneGraph* scenegraph = Engine::Instance()->GetSceneGraph();

    ObjectID id = scenegraph->map_editor->CreateObject(default_hot_join_characte_path);
    std::string team = "";
    vec3 position = vec3(0.0f);

    // Attempt to move character close to an existing one.
    vector<ObjectID> local_avatar_ids = GetLocalAvatarIDs();
    if (local_avatar_ids.size() > 0) {
        MovementObject* mymo = static_cast<MovementObject*>(scenegraph->GetObjectFromID(local_avatar_ids[0]));
        team = mymo->GetScriptParams()->GetStringVal("Teams");
        // Overgrowth Online: near the host's character on solid ground (not in the water)
        position = SafeSpotNear(mymo->position, mymo->GetFacing(), (int)online_session->player_states.size());
    } else {
        LOGW << "Unable to find locally controlled character while creating character for new connection, using fallback parameters" << std::endl;
    }

    MovementObject* mo = static_cast<MovementObject*>(scenegraph->GetObjectFromID(id));
    mo->SetTranslation(position);
    mo->is_player = true;
    mo->created_on_the_fly = true;

    // Overgrowth Online: create it everywhere first, or the clients drop its Teams update
    Send<OnlineMessages::CreateEntity>(default_hot_join_characte_path, position, id);

    ScriptParams* sp = mo->GetScriptParams();
    sp->ASSetString("Teams", team);

    // Set player color for new character
    OGPalette* palettes = mo->GetPalette();
    for (auto& palette : *palettes) {
        if (palette.label == "Cloth") {
            palette.color = vec3(1.0f, 0.0f, 0.0f);
        }
    }
    mo->ApplyPalette(*palettes);
    return id;
}

void Online::SetNoDataInterpStepOverRide(float override) {
    no_data_interpstep_override = override;
}

float Online::GetNoDataInterpStepOverRide() const {
    return no_data_interpstep_override;
}

// Client side sending camera info to host
void Online::SendPlayerCameraState() {
    Camera* camera = ActiveCameras::Instance()->GetCamera(0);
    Send<OnlineMessages::CameraTransformMessage>(camera->GetPos(), camera->GetFlatFacing(), camera->GetFacing(), camera->GetUpVector(), camera->GetXRotation(), camera->GetYRotation());
}

void Online::SendPlayerInputState() {
    // Assume player one control. For split screen multiplayer we have to start dealing with multiplayer controllers here.
    PlayerInput* player_input = Input::Instance()->GetController(0);

    for (const string& binding : Input::Instance()->GetAllAvailableBindings()) {
        // TODO Can we make it so these bindings don't even show up in available bindings?
        if (binding == "look_left" || binding == "look_right" || binding == "look_up" || binding == "look_down") {
            continue;
        }

        KeyState local_keystate = player_input->key_down[binding];
        KeyState remote_keystate = client_key_down_map_state[binding];

        // Ownership: our character runs here, so the host only needs what is
        // held (its scripts may read it), not every frame a key stays down
        bool changed = std::fabs(local_keystate.depth - remote_keystate.depth) > 0.05f ||
                       (local_keystate.depth == 0.0f) != (remote_keystate.depth == 0.0f) ||
                       (local_keystate.count > 0) != (remote_keystate.count > 0) ||
                       (local_keystate.depth_count > 0) != (remote_keystate.depth_count > 0);
        if (changed) {
            Send<OnlineMessages::PlayerInputMessage>(local_keystate.depth, local_keystate.depth_count, local_keystate.count, online_session->binding_id_map_[binding]);
            client_key_down_map_state[binding] = local_keystate;
        }
    }
}

void Online::LateUpdate(SceneGraph* scenegraph) {
    // Overgrowth Online: the session is over: the level scripts go back to
    // offline, and we to our own game speed
    if (sent_role && !IsActive()) {
        sent_role = false;
        last_role_message_ms = -1e9;
        Engine::Instance()->FollowGameSpeed(config["global_time_scale_mult"].toNumber<float>());
        if (scenegraph != nullptr && scenegraph->level != nullptr) {
            scenegraph->level->Message("mp_role none");
        }
    }

    if (IsActive()) {
        // Ping twice a second: round trip and clock offset per peer (clock_sync.h)
        if (online_session != nullptr && game_timer.wall_time > online_session->last_ping_time + 0.5f) {
            online_session->last_ping_id = online_session->ping_counter++;
            online_session->last_ping_time = game_timer.wall_time;
            Send<OnlineMessages::Ping>(online_session->last_ping_id, OnlineClockMs());
        }

        online_session->client_connection_manager.Update();

        if (OnlineClockMs() - last_link_log_ms > 10000.0) {
            last_link_log_ms = OnlineClockMs();
            LogLinks();
        }

        UpdateTakeOvers(scenegraph);

        // Overgrowth Online: level scripts learn whether they run on the host or a client
        if (scenegraph != nullptr && scenegraph->level != nullptr && host_started_level &&
            OnlineClockMs() - last_role_message_ms > 2000.0) {
            last_role_message_ms = OnlineClockMs();
            sent_role = true;
            int mine = GetOwnPlayerState().object_id;
            string role = string("mp_role ") + (IsHosting() ? "host" : "client") + " #" + to_string(mine) + " " +
                          to_string(GetPlayerCount());
            for (const PlayerState& player : GetPlayerStates()) {
                string name = player.playername.empty() ? "Player" : player.playername;
                for (char& c : name) {
                    if (c == ' ' || c == '\t' || c == '#') {
                        c = '_';
                    }
                }
                role += " #" + to_string((int)player.object_id) + " " + name;
            }
            scenegraph->level->Message(role);

            if (IsHosting()) {
                Send<OnlineMessages::SendLevelMessage>("ohg speed " + to_string(Engine::Instance()->GameSpeed()));
                OpenToFriends(false);
                UpdateFriendPresence(scenegraph);
            }
        }

        CoopUpdate(scenegraph);
        CoopSpectateUpdate(scenegraph);
        ResolveHeldHits();

        // Characters: a client's switch goes on to everyone (kept for late
        // joiners), then our own choice
        for (const auto& c : pending_avatar_characters) {
            Send<OnlineMessages::AvatarCharacter>(c.first, c.second);
        }
        pending_avatar_characters.clear();
        ApplyOwnCharacter(scenegraph);

        for (const auto& order : pending_drop_orders) {
            SendToOwnerOf(order.first, message_handler.Create<OnlineMessages::ItemDrop>(GetOriginalID(order.first), GetOriginalID(order.second)), false);
        }
        pending_drop_orders.clear();

        if (IsHosting()) {
            HandleConnectionsToBeClosed();  // on the main thread: it deletes characters
            SyncHostSessionFlags();
        }
    }
}

void Online::SyncHostSessionFlags() {
    lock_guard<mutex> guard(online_session->connected_peers_lock);
    for (auto& conn : online_session->connected_peers) {
        for (auto& flag : online_session->host_session_flags) {
            if (conn.host_session_flags.find(flag.first) == conn.host_session_flags.end() || conn.host_session_flags.find(flag.first)->second != flag.second) {
                SendTo<OnlineMessages::HostSessionFlag>(conn.conn_id, flag.first, flag.second);
                conn.host_session_flags[flag.first] = flag.second;
            }
        }
    }
}

void Online::GenerateStateForNewConnection(NetConnectionID conn) {
    SceneGraph* graph = Engine::Instance()->GetSceneGraph();
    if (graph != nullptr) {
        GenerateMovementSyncPackages(conn, graph);
        GenerateEnvObjectSyncPackages(conn, graph);
    }
}

void Online::GenerateEnvObjectSyncPackages(NetConnectionID conn, SceneGraph* graph) {
    std::vector<Object*> envobjects = graph->GetObjectsOfType(EntityType::_env_object);

    for (const auto& it : envobjects) {
        EnvObject* eo = static_cast<EnvObject*>(it);
        SendTo<OnlineMessages::EnvObjectUpdate>(conn, eo);
    }
}

void Online::GenerateMovementSyncPackages(NetConnectionID conn, SceneGraph* graph) {
    std::vector<Object*> movobjects = graph->GetObjectsOfType(EntityType::_movement_object);

    for (const auto& it : movobjects) {
        MovementObject* mov = static_cast<MovementObject*>(it);
        ObjectID avatarid = mov->GetID();

        // riggedbody blood?
        std::vector<MorphTargetStateStorage> network_morphs = mov->rigged_object()->network_morphs;

        for (const auto& it : network_morphs) {
            SendTo<OnlineMessages::MorphTargetUpdate>(conn, avatarid, it);
        }
    }
}

bool Online::CheckLoadedMapAndCampaignState(const string& campagin, const string& level_name) {
    Engine* engine = Engine::Instance();
    SceneGraph* graph = engine->GetSceneGraph();

    ScriptableCampaign* loaded_campagin = engine->GetCurrentCampaign();
    if (loaded_campagin == nullptr || graph == nullptr) {
        return false;
    }

    string current_campaign = loaded_campagin->GetCampaignID();
    string campaignId = engine->GetCurrentCampaign()->GetCampaignID();
    string current_level = graph->level_path_.GetOriginalPathStr();
    return (current_campaign == campagin) && (level_name == current_level);
}

void Online::SetLevelLoaded() {
    if (IsClient()) {
        // We notify the host about having loaded, the host can only finish loading if we have.
        // From the client's perspective! The host might already be ingame, client doesn't know.
        Send<OnlineMessages::PCSLoadingCompletedMessage>();
        preload_loaded = !preloaded_level.empty() && preloaded_level == level_name;
    }

    // Host finished loading
    if (IsHosting()) {
        loading = false;
        RemoveAllAvatarIds();
        AddFreeAvatarsIds();
        ObjectID avatar = GetFreeAvatarID();

        campaign_id = ModLoading::Instance().WhichCampaignLevelBelongsTo(level_name);
        LOG_ASSERT(level_name == Engine::Instance()->GetSceneGraph()->level_path_.GetOriginalPathStr());
    }
}

uint32_t Online::IncomingMessageCount() const {
    if (IsActive()) {
        return online_session->incoming_messages.size();
    }
    return 0;
}

uint32_t Online::OutgoingMessageCount() const {
    if (IsActive()) {
        return online_session->outgoing_messages.size();
    }
    return 0;
}

bool Online::GetIfPendingAngelScriptUpdates() {
    return online_session->peer_queued_level_updates.empty() == false;
}

bool Online::GetIfPendingAngelScriptStates() {
    return online_session->peer_queued_sync_updates.empty() == false;
}

AngelScriptUpdate Online::GetAngelScriptUpdate() {
    if (online_session->peer_queued_level_updates.empty() == false) {
        return online_session->peer_queued_level_updates.front();
    }
    return AngelScriptUpdate();
}

void Online::MoveAngelScriptQueueForward() {
    online_session->peer_queued_level_updates.pop();
}

void Online::MoveAngelStateQueueForward() {
    online_session->peer_queued_sync_updates.pop();
}

AngelScriptUpdate Online::GetAngelScriptStates() {
    if (online_session->peer_queued_sync_updates.empty() == false) {
        return online_session->peer_queued_sync_updates.front();
    }
    return AngelScriptUpdate();
}

uint32_t Online::AddSyncState(uint32_t state, const vector<char>& data) {
    AngelScriptUpdate temp;

    temp.state = state;
    temp.data = data;

    lock_guard<mutex> guard(online_session->state_packages_lock);
    online_session->state_packages[state] = temp;

    return state;
}

uint32_t Online::RegisterMPState(const string& state) {
    if (states.find(state) != states.end()) {
        return states[state];
    }

    states[state] = next_available_state_id++;
    return states[state];
}

void Online::SetTickperiod(const uint32_t& tickperiod) {
    tick_period = tickperiod;
}

bool Online::IsActive() const {
    return (IsHosting() || IsClient()) && online_session != nullptr;
}

bool Online::IsHosting() const {
    return mode == MultiplayerMode::Host;
}

bool Online::IsAwaitingShutdown() const {
    return mode == MultiplayerMode::AwaitingShutdown;
}

void Online::StartMultiplayer(MultiplayerMode multiplayerMode) {
    // for(string binding : Input::Instance()->GetAllAvailableBindings()) {
    //     LOGI << binding << " " << (int)Input::Instance()->GetBindID(binding) << endl;
    // }

    // for(string binding_cat : Input::Instance()->GetAvailableBindingCategories()) {
    //     LOGI << binding_cat << endl;
    // }

    if (online_session != nullptr) {
        LOGE << "Attempting to start multiplayer while the previous session is still valid!" << endl;
        // TODO can we close the application here with a big error? This should be a fatal issue!
    }

    if (OnlineUtility::HasActiveIncompatibleMods()) {
        LOGE << "Online multiplayer was enabled despite using unsupported mods, this might cause issues!" << std::endl;
        LOGE << "Incompatible mods: " << OnlineUtility::GetActiveIncompatibleModsString() << std::endl;
    }

    online_session = new OnlineSession();

    host_started_level = false;
    online_session->host_session_flags[OnlineFlags::ALLOWSEDITOR] = false;

    mode = multiplayerMode;

    Engine* engine = Engine::Instance();
    SceneGraph* graph = engine->GetSceneGraph();

    string mpcachedir = string(write_path) + string("multiplayercache/");

    CreateParentDirs(mpcachedir);

    AddPath(mpcachedir.c_str(), PathFlags::kDataPaths);

    if (mode == MultiplayerMode::Host) {
        // assert(scenegraph);
        // TODO this might never be true due to how we handle starting multiplayer now
        if (engine->GetSceneGraph() != nullptr) {
            RemoveAllAvatarIds();
            AddFreeAvatarsIds();
            host_started_level = true;

            // index 0 is probably not correct
            ScriptableCampaign* sc = engine->GetCurrentCampaign();
            if (sc != nullptr) {
                campaign_id = sc->GetCampaignID();
            } else {
                campaign_id = "";
            }

            // you can check with scenegraph
            level_name = graph->level_path_.GetOriginalPathStr();
        }

        // Assign mappings for input bindings.
        for (const string& binding : Input::Instance()->GetAllAvailableBindings()) {
            AssignBindID(binding);
        }

        online_session->local_player_id = -1;
        online_session->player_states[online_session->local_player_id] = GenerateHostPlayerState();
    }

    net.Initialize();

    engine->menu_paused = false;
    engine->CommitPause();

    tick_period = 30;
    socket_thread = (thread(&Online::NetworkDataThread, this));
}

void Online::StartHostingMultiplayer() {
    if (!IsActive()) {
        StartMultiplayer(MultiplayerMode::Host);
        CreateListenSocketIP("");
    }
}

void Online::PerformLevelChangeCleanUp() {
    lock_guard<mutex> guard(online_session->state_packages_lock);
    online_session->state_packages.clear();
}

const vector<Peer> Online::GetPeers() {
    lock_guard<mutex> guard(online_session->connected_peers_lock);
    return online_session->connected_peers;
}

Peer* Online::GetPeerFromConnection(NetConnectionID conn_id) {
    for (auto& connected_peer : online_session->connected_peers) {
        if (connected_peer.conn_id == conn_id) {
            return &connected_peer;
        }
    }
    return nullptr;
}

Peer* Online::GetPeerFromID(PeerID peer_id) {
    for (auto& connected_peer : online_session->connected_peers) {
        if (connected_peer.peer_id == peer_id) {
            return &connected_peer;
        }
    }
    return nullptr;
}

vector<Peer>::iterator Online::GetPeerIt(NetConnectionID conn_id) {
    for (int i = 0; i < online_session->connected_peers.size(); i++) {
        if (online_session->connected_peers[i].conn_id == conn_id) {
            return online_session->connected_peers.begin() + i;
        }
    }
    return online_session->connected_peers.end();
}

void Online::QueueStopMultiplayer() {
    if (IsActive()) {
        mode = MultiplayerMode::AwaitingShutdown;
    }
}

void Online::StopMultiplayer() {
    if (IsActive() || mode == MultiplayerMode::AwaitingShutdown) {
        mode = MultiplayerMode::CleanUp;

        {  // Immediately disconnect all clients
            lock_guard<mutex> guard(online_session->connected_peers_lock);
            for (auto& it : online_session->connected_peers) {
                // We kill the connection on the socket directly, we no longer want to keep track of any connection changes
                // We can't call CloseConnectionImmediate here, as it modifies the array we are iterating over and uses the same lock
                net.CloseConnection(it.conn_id, ConnectionClosedReason::HOST_STOPPED_HOSTING);
            }
            online_session->connected_peers.clear();  // We shouldn't have to clear this, since we plan on getting rid of online_session shortyly
            StopListening();

            // Overgrowth Online: a connection still being made (the player
            // cancelled, or it never answered) is closed too; it went on forever
            if (has_pending_connection) {
                net.CloseConnection(pending_connection, ConnectionClosedReason::DISCONNECTED);
                has_pending_connection = false;
            }
        }

        // Clear up on the fly
        SceneGraph* graph = Engine::Instance()->GetSceneGraph();
        if (graph != nullptr) {
            vector<Object*> objects = graph->GetObjectsOfType(EntityType::_movement_object);

            for (auto it : objects) {
                if (it->created_on_the_fly) {
                    graph->map_editor->RemoveObject(it, graph, true);
                    DeRegisterHostClientIDTranslation(it->GetID());
                }
            }
        }

        next_available_state_id = 1;
        next_free_peer_id = 1;

        host_started_level = false;
        loading = false;
        p2p_sockets_active = false;
        held_hits.clear();

#if ENABLE_STEAMWORKS
        if (Steamworks::Instance()->GetMatchmaking() != nullptr) {
            Steamworks::Instance()->GetMatchmaking()->LeaveAll();  // the lobby and the "Join Game" presence
        }
#endif

        if (socket_thread.joinable()) {
            socket_thread.join();
        }

        // Make sure we get booted into the main menu, if we aren't there already.
        if (Engine::Instance()->current_engine_state_.type != EngineStateType::kEngineScriptableUIState) {
            Engine::Instance()->ScriptableUICallback("back_to_main_menu");
        }

        mode = MultiplayerMode::NoMultiplayer;

        delete online_session;
        online_session = nullptr;
    }
}

bool Online::IsLocalAvatar(const ObjectID avatar_id) const {
    if (online_session == nullptr) {
        // Everything is local when playing offline
        return true;
    }

    // Handed to a machine for a throw, choke or drag (Overgrowth Online)
    auto ov = owner_overrides.find(GetOriginalID(avatar_id));
    if (ov != owner_overrides.end()) {
        return ov->second.owner == online_session->local_player_id;
    }

    // Is this avatar controlled by a playerstate? If yes, is it us?
    for (const auto& it : online_session->player_states) {
        if (it.second.object_id == avatar_id) {
            return it.first == online_session->local_player_id;
        }
    }

    // As a host, everything not convered in a playerstate is local
    // As a client, everything not controlled by us playerstate is remote
    return IsHosting();
}

bool Online::IsPlayerCharacter(ObjectID id) const {
    if (online_session == nullptr) {
        return false;
    }
    for (const auto& it : online_session->player_states) {
        if (it.second.object_id == id) {
            return true;
        }
    }
    return false;
}

PlayerID Online::GetAvatarOwner(const ObjectID avatar_id) const {
    auto ov = owner_overrides.find(GetOriginalID(avatar_id));
    if (ov != owner_overrides.end()) {
        return ov->second.owner;
    }

    if (online_session != nullptr) {
        for (const auto& it : online_session->player_states) {
            if (it.second.object_id == avatar_id) {
                return it.first;
            }
        }
    }

    // Not a player's character: the host's (NPCs, free avatars)
    return IsHosting() && online_session != nullptr ? online_session->local_player_id : 0;
}

// Overgrowth Online: ownership streaming. Every machine captures the characters
// it simulates (30 Hz) and sends them: a client to the host; the host to every
// client, along with the players' characters it relays. The rate per receiver
// falls with the distance to that receiver's character; a record that did not
// change (a still character, a settled ragdoll) goes out once a second.
namespace {
const double kStreamCaptureMs = 1000.0 / 30.0;
const double kStreamHeartbeatMs = 1000.0;
const double kStreamSlackMs = 8.0;  // captures land on frames, not exactly on the grid
const size_t kStreamBundleBytes = 1000;  // about one packet: a loss costs a few characters
const int kStreamMorphRefresh = 30;  // captures between full morph sets (a lost change heals within a second)

// The host's snapshots, all receivers together: 1.2 Mbit/s (stress_8: 8
// players and 30 NPCs within 1.5 Mbit/s with everything else)
const double kSnapshotBudget = 150000.0;  // bytes a second

// How much a receiver wants a character, 0..1 (times 30 Hz)
double StreamPriority(float dist, bool in_view, bool engaged) {
    double w = dist < 5.0f ? 1.0 : dist < 12.0f ? 0.6 : dist < 25.0f ? 0.35 : dist < 50.0f ? 0.2 : 0.08;
    if (!in_view) {
        w *= 0.5;  // at most half: a rate jump on turning the camera re-times the playback (a snap)
    }
    if (engaged) {
        w = 1.0;  // fighting each other: full rate
    }
    return w;
}

int MorphStep(float v) {
    return (int)std::round(v * 50.0f);  // changes under 2 % wait for the next refresh
}
}  // namespace

void Online::StreamCharacters(SceneGraph* sg) {
    if (!host_started_level || online_session == nullptr) {
        stream_latest.clear();
        stream_sent.clear();
        stream_owned.clear();
        owner_overrides.clear();
        handed_back_ms.clear();
        return;
    }

    double now = OnlineClockMs();
    PlayerID me = online_session->local_player_id;

    // 1. Capture the characters we simulate
    if (now >= stream_next_capture_ms) {
        stream_next_capture_ms = std::max(stream_next_capture_ms + kStreamCaptureMs, now - kStreamCaptureMs);

        for (Object* o : sg->movement_objects_) {
            MovementObject* mo = static_cast<MovementObject*>(o);
            if (mo->network_proxy) {
                auto was = stream_owned.find(GetOriginalID(mo->GetID()));
                if (was != stream_owned.end()) {
                    was->second.has_last = false;  // someone else streams it now: ours starts fresh
                }
                continue;
            }

            CommonObjectID cid = GetOriginalID(mo->GetID());
            StreamOwned& owned = stream_owned[cid];
            bool all_morphs = owned.captures % kStreamMorphRefresh == 0;
            ++owned.captures;

            CharSnapshot::Frame frame;
            mo->CaptureNetworkPose(frame, true);
            frame.src_ms = (uint32_t)now;

            // Morphs: only the ones that moved since we last sent them
            owned.sent_morphs.resize(frame.morphs.size() * 2, std::numeric_limits<int>::min());
            bool morphs_moved = all_morphs;
            for (size_t i = 0; i < frame.morphs.size() && !morphs_moved; ++i) {
                morphs_moved = MorphStep(frame.morphs[i].disp_weight) != owned.sent_morphs[i * 2] ||
                               MorphStep(frame.morphs[i].mod_weight) != owned.sent_morphs[i * 2 + 1];
            }

            // Only breathing since the last one sent: keep that one (the
            // morph refresh above still goes out about once a second)
            if (!morphs_moved && owned.has_last && CharSnapshot::Similar(frame, owned.last_sent)) {
                ++snapshot_idle_skips;
                owned.idle = true;
                continue;
            }
            frame.resumed = owned.idle;  // it stood still until now: receivers hold, not glide
            owned.idle = false;
            owned.last_sent = frame;
            owned.last_sent.morphs.clear();
            owned.has_last = true;

            size_t kept = 0;
            for (size_t i = 0; i < frame.morphs.size(); ++i) {
                int d = MorphStep(frame.morphs[i].disp_weight);
                int w = MorphStep(frame.morphs[i].mod_weight);
                if (all_morphs || d != owned.sent_morphs[i * 2] || w != owned.sent_morphs[i * 2 + 1]) {
                    owned.sent_morphs[i * 2] = d;
                    owned.sent_morphs[i * 2 + 1] = w;
                    frame.morphs[kept++] = frame.morphs[i];
                }
            }
            frame.morphs.resize(kept);
            frame.has_morphs = kept > 0;

            if (!CharSnapshot::SameVitals(frame.vitals, owned.last_vitals)) {
                owned.last_vitals = frame.vitals;
                ++owned.vitals_version;
            }

            StreamRecord& r = stream_latest[cid];
            r.bytes.clear();
            int positions = 0;
            CharSnapshot::Write(r.bytes, frame, &mo->rigged_object()->NetworkRig(), &positions);
            r.lite.clear();
            if (IsHosting() && frame.vitals.valid) {
                CharSnapshot::Frame lite = frame;
                lite.vitals.valid = false;
                CharSnapshot::Write(r.lite, lite, &mo->rigged_object()->NetworkRig());
            }
            r.vitals_version = owned.vitals_version;
            r.target = frame.vitals.valid ? frame.vitals.target_id : -1;
            r.attacker = frame.vitals.valid ? frame.vitals.attacked_by : -1;
            snapshot_positions_sent += positions;
            snapshot_bones_captured += frame.bones.size();
            r.src_ms = frame.src_ms;
            r.hash = CharSnapshot::Hash(r.bytes.data() + CharSnapshot::kContentOffset, r.bytes.size() - CharSnapshot::kContentOffset);
            r.position = mo->position;
            r.owner = me;
            r.player = IsPlayerCharacter(mo->GetID());
        }
    }

    // 2. Send what each receiver is due: a client sends to the host only
    struct Receiver {
        PeerID peer;
        NetConnectionID conn;
        bool has_pos;
        vec3 pos;
        bool has_cam;
        vec3 cam_pos;
        vec3 cam_dir;
        CommonObjectID avatar;
    };
    vector<Receiver> receivers;
    {
        lock_guard<mutex> guard(online_session->connected_peers_lock);
        for (const Peer& peer : online_session->connected_peers) {
            receivers.push_back({peer.peer_id, peer.conn_id, false, vec3(0.0f), false, vec3(0.0f), vec3(0.0f), -1});
        }
    }
    double budget = kSnapshotBudget / std::max<size_t>(1, receivers.size());  // bytes a second each
    vector<CommonObjectID> players_in_stream;  // (filled once, for the first receiver)

    for (Receiver& rc : receivers) {
        auto ps = online_session->player_states.find(rc.peer);
        if (IsHosting() && ps != online_session->player_states.end()) {
            Object* o = sg->GetObjectFromID(ps->second.object_id);
            if (o != nullptr && o->GetType() == _movement_object) {
                rc.has_pos = true;
                rc.pos = static_cast<MovementObject*>(o)->position;
                rc.avatar = GetOriginalID(ps->second.object_id);
            }

            // Their view (the camera they send us, 5 Hz)
            Camera* cam = ps->second.camera_id >= 0 ? ActiveCameras::GetCamera(ps->second.camera_id) : nullptr;
            if (cam != nullptr) {
                rc.has_cam = true;
                rc.cam_pos = cam->GetPos();
                rc.cam_dir = cam->GetFacing();
            }
        }

        std::vector<uint8_t> bundle;
        int records = 0;

        // What they'd like (30 Hz times the priority), and the scale that fits their share
        struct Want {
            CommonObjectID cid;
            double rate;
        };
        vector<Want> wants;
        double want_bytes = 0.0;
        if (players_in_stream.empty()) {
            for (auto& it : stream_latest) {
                if (it.second.player) {
                    players_in_stream.push_back(it.first);
                }
            }
        }
        for (auto& it : stream_latest) {
            const StreamRecord& r = it.second;
            if (r.owner == rc.peer || r.bytes.empty()) {
                continue;  // their own character
            }
            float dist = rc.has_pos ? distance(rc.pos, r.position) : 10.0f;
            bool in_view = true;
            if (rc.has_cam) {
                vec3 to = r.position - rc.cam_pos;
                in_view = length(to) < 12.0f || dot(normalize(to), rc.cam_dir) > 0.0f;  // (wide: the camera turns fast)
            }
            bool engaged = rc.avatar >= 0 && (r.target == rc.avatar || r.attacker == rc.avatar);
            double priority = StreamPriority(dist, in_view, engaged);
            // Players, and whoever fights one (a friend's fight is watched):
            // a few of them; full rate within 25 m, smooth however far (the
            // budget below still rules)
            bool fights_player = std::find(players_in_stream.begin(), players_in_stream.end(), r.target) != players_in_stream.end() ||
                                 std::find(players_in_stream.begin(), players_in_stream.end(), r.attacker) != players_in_stream.end();
            if (r.player || fights_player) {
                priority = std::max(priority, dist < 25.0f ? 1.0 : 0.5);
            }
            if (!IsHosting()) {
                priority = 1.0;  // to the host: its AI, its hit checks and every relay run on this stream
            }
            double rate = 30.0 * priority;
            wants.push_back({it.first, rate});
            want_bytes += rate * (r.lite.empty() ? r.bytes.size() : r.lite.size());
        }
        double scale = want_bytes > budget ? budget / want_bytes : 1.0;

        for (const Want& want : wants) {
            const StreamRecord& r = stream_latest[want.cid];
            StreamSent& sent = stream_sent[std::make_pair(rc.peer, want.cid)];
            if (r.src_ms == sent.src_ms) {
                continue;
            }

            double interval = 1000.0 / std::max(1.0, std::min(30.0, want.rate * scale));
            if (now - sent.sent_ms < interval - kStreamSlackMs) {
                continue;
            }
            if (r.hash == sent.hash && now - sent.sent_ms < kStreamHeartbeatMs) {
                continue;
            }

            // Vitals when they changed for them (and twice more), and once a second
            bool vitals = r.lite.empty();
            if (!vitals) {
                if (sent.vitals_version != r.vitals_version) {
                    sent.vitals_version = r.vitals_version;
                    sent.vitals_repeats = 3;
                }
                if (sent.vitals_repeats > 0 || now - sent.vitals_ms > 1000.0) {
                    vitals = true;
                    sent.vitals_repeats = std::max(0, sent.vitals_repeats - 1);
                    sent.vitals_ms = now;
                }
            }
            const std::vector<uint8_t>& bytes = vitals ? r.bytes : r.lite;

            if (!bundle.empty() && bundle.size() + bytes.size() > kStreamBundleBytes) {
                snapshot_bytes_out += bundle.size();
                snapshot_records_out += records;
                SendTo<OnlineMessages::CharacterSnapshots>(rc.conn, bundle);
                bundle.clear();
                records = 0;
            }

            bundle.insert(bundle.end(), bytes.begin(), bytes.end());
            ++records;
            sent.src_ms = r.src_ms;
            sent.hash = r.hash;
            sent.sent_ms = now;
        }

        if (!bundle.empty()) {
            snapshot_bytes_out += bundle.size();
            snapshot_records_out += records;
            SendTo<OnlineMessages::CharacterSnapshots>(rc.conn, bundle);
        }
    }
}

void Online::ReceiveSnapshots(PeerID from, const uint8_t* data, size_t size) {
    SceneGraph* sg = Engine::Instance()->GetSceneGraph();
    if (sg == nullptr || !host_started_level) {
        return;
    }

    double now = OnlineClockMs();
    size_t offset = 0;
    CharSnapshot::Frame frame;

    while (offset < size) {
        size_t begin = offset;
        if (!CharSnapshot::Read(data, size, offset, frame)) {
            LOGW << "Malformed character snapshot from peer " << (int)from << std::endl;
            return;
        }

        ObjectID id = GetObjectID(frame.id);
        Object* o = sg->GetObjectFromID(id);
        if (o == nullptr || o->GetType() != _movement_object || IsLocalAvatar(id)) {
            continue;  // gone, or ours: we simulate it
        }
        if (IsHosting() && GetAvatarOwner(id) != from) {
            continue;  // only a character's owner streams it
        }

        MovementObject* mo = static_cast<MovementObject*>(o);
        if (!CharSnapshot::Resolve(frame, mo->rigged_object()->NetworkRig())) {
            continue;  // a different skeleton here: nothing sensible to show
        }
        mo->net_buffer.Push(frame, now);

        // Who holds what, as soon as it arrives (a throw or drop can't wait for the playback delay)
        if (frame.vitals.valid && frame.src_ms >= mo->net_slots_src_ms && mo->network_proxy && mo->NetworkVitalsSupported()) {
            mo->net_slots_src_ms = frame.src_ms;
            mo->ReconcileNetworkItems(frame.vitals);
        }
        ++snapshot_records_in;

        if (frame.has_morphs) {
            RiggedObject* rig = mo->rigged_object();
            for (const CharSnapshot::Morph& morph : frame.morphs) {
                if (morph.index < rig->morph_targets.size()) {
                    const std::string& name = rig->morph_targets[morph.index].name;
                    MorphTargetStateStorage& m = rig->incoming_network_morphs[name];
                    m.name = name;
                    m.disp_weight = morph.disp_weight;
                    m.mod_weight = morph.mod_weight;
                    m.dirty = false;
                }
            }
        }

        if (IsHosting()) {  // relay to the other players
            StreamRecord& r = stream_latest[frame.id];
            r.bytes.assign(data + begin, data + offset);
            r.src_ms = frame.src_ms;
            r.hash = CharSnapshot::Hash(r.bytes.data() + CharSnapshot::kContentOffset, r.bytes.size() - CharSnapshot::kContentOffset);
            r.position = frame.position;
            r.owner = from;
            r.player = IsPlayerCharacter(id);
            r.lite.clear();
            if (frame.vitals.valid) {
                r.target = frame.vitals.target_id;
                r.attacker = frame.vitals.attacked_by;
            }
        }
    }
}

// Overgrowth Online, Phase 4: a message about a character goes to the machine
// that runs it. A client sends to the host; the host forwards what it doesn't
// run to that player's connection. (Inside message handling the peers lock
// is already held.)
void Online::SendToOwnerOf(ObjectID id, const OnlineMessageRef& ref, bool peers_locked) {
    if (!IsActive() || online_session == nullptr) {
        return;
    }

    if (IsClient()) {
        Send(ref);
        return;
    }

    PlayerID owner = GetAvatarOwner(id);
    if (owner == online_session->local_player_id) {
        return;  // ours: the caller handles it here
    }

    NetConnectionID conn = 0;
    bool found = false;
    {
        std::unique_lock<mutex> guard(online_session->connected_peers_lock, std::defer_lock);
        if (!peers_locked) {
            guard.lock();
        }
        for (const Peer& peer : online_session->connected_peers) {
            if (peer.peer_id == owner) {
                conn = peer.conn_id;
                found = true;
            }
        }
    }

    if (found) {
        SendTo(conn, ref);
    }
}

// Host, inside message handling (peers lock held): pass a client's message on
// to every other client
void Online::RelayFromPeer(const OnlineMessageRef& ref, PeerID from) {
    if (!IsHosting() || online_session == nullptr) {
        return;
    }

    for (const Peer& peer : online_session->connected_peers) {
        if (peer.peer_id != from) {
            SendTo(peer.conn_id, ref);
        }
    }
}

void Online::SendHitRequest(ObjectID target, const string& type, const string& attack_path, vec3 dir, vec3 pos,
                            ObjectID attacker, float damage_mult, float knockback_mult, float max_range) {
    if (!IsActive() || IsLocalAvatar(target)) {
        return;
    }

    OnlineMessageRef ref = message_handler.Create<OnlineMessages::HitRequest>(
        GetOriginalID(attacker), GetOriginalID(target), type, attack_path, dir, pos, damage_mult, knockback_mult, max_range);
    static_cast<OnlineMessages::HitRequest*>(ref.GetData())->src_ms = (uint32_t)OnlineClockMs();
    SendToOwnerOf(target, ref, false);
    ++hits_requested;
}

namespace {
bool KnownHitType(const string& type) {
    return type == "blockprepare" || type == "attackimpact" || type == "attackblocked" || type == "grabbed";
}

MovementObject* CharacterByID(ObjectID id) {
    SceneGraph* sg = Engine::Instance()->GetSceneGraph();
    Object* o = sg != nullptr ? sg->GetObjectFromID(id) : nullptr;
    return (o != nullptr && o->GetType() == _movement_object) ? static_cast<MovementObject*>(o) : nullptr;
}

const float kHitRangeSlack = 0.75f;  // both machines see the other a little late
}  // namespace

// A script of ours hit a character another machine runs: ask its owner, and
// give the attack the usual guess meanwhile (aschar.as values: _miss 0,
// _hit 2, _block_impact 3). The reach the defender checks against is ours.
int Online::NetworkWasHit(MovementObject* target, const string& type, const string& attack_path, vec3 dir, vec3 pos,
                          ObjectID attacker, float damage_mult, float knockback_mult) {
    SceneGraph* sg = Engine::Instance()->GetSceneGraph();
    Object* a = sg != nullptr ? sg->GetObjectFromID(attacker) : nullptr;
    float reach = a != nullptr ? distance(a->GetTranslation(), target->position) : 0.0f;

    SendHitRequest(target->GetID(), type, attack_path, dir, pos, attacker, damage_mult, knockback_mult, reach);

    if (type == "attackimpact") {
        return target->NetworkVitalsSupported() && target->ASGetIntVar("knocked_out") != 0 ? 0 : 2;
    } else if (type == "attackblocked") {
        return 3;  // we switched to the blocked attack because it said it would block
    }
    return 0;  // blockprepare: no block until it says so
}

// We run the defender: decide the hit on its own state, as its player saw it
void Online::ReceiveHitRequest(const OnlineMessageRef& ref, OnlineMessages::HitRequest* m, PeerID from) {
    if (!host_started_level || !KnownHitType(m->type)) {
        return;
    }

    ObjectID target_id = GetObjectID(m->target);
    ObjectID attacker_id = GetObjectID(m->attacker);
    MovementObject* target = CharacterByID(target_id);
    MovementObject* attacker = CharacterByID(attacker_id);

    if (target == nullptr || attacker == nullptr) {
        return;
    }

    if (!IsLocalAvatar(target_id)) {
        if (IsHosting()) {
            SendToOwnerOf(target_id, ref, true);  // a player's character: its machine decides
        }
        return;
    }

    if (IsHosting() && GetAvatarOwner(attacker_id) != from) {
        return;  // only the attacker's owner asks about its attacks
    }

    // Shown in the past here (a proxy): hold it until we see the attacker land it
    const double kMaxHoldMs = 250.0;
    if (attacker->network_proxy && m->src_ms != 0) {
        double wait = (double)m->src_ms - attacker->net_buffer.RenderSrcMs();
        if (wait > 1.0) {
            held_hits.push_back({ref, from, OnlineClockMs() + std::min(wait, kMaxHoldMs)});
            return;
        }
    }

    ResolveHitRequest(m, true);
}

// Every 10 s: how each connection does (a drop shows what led to it)
void Online::LogLinks() {
    lock_guard<mutex> guard(online_session->connected_peers_lock);
    for (const Peer& peer : online_session->connected_peers) {
        ConnectionStatus s;
        GetConnectionStatus(peer, &s);
        LOGI << "Link peer " << (int)peer.peer_id << ": ping " << s.ms_ping << " ms, quality " << s.connection_quality_local << "/"
             << s.connection_quality_remote << ", out " << (int)s.out_bytes_per_sec << " B/s, in " << (int)s.in_bytes_per_sec
             << " B/s, rate " << s.send_rate_bytes_per_second << ", pending " << s.pending_reliable << "+" << s.pending_unreliable
             << " (unacked " << s.sent_unacked_reliable << "), queue " << s.usec_queue_time / 1000 << " ms" << std::endl;
    }
}

// Held hits whose attacker we now show landing them (main thread, LateUpdate)
void Online::ResolveHeldHits() {
    double now = OnlineClockMs();
    size_t done = 0;
    while (done < held_hits.size() && held_hits[done].resolve_at_ms <= now) {
        ResolveHitRequest(static_cast<OnlineMessages::HitRequest*>(held_hits[done].ref.GetData()), false);
        ++done;
    }
    held_hits.erase(held_hits.begin(), held_hits.begin() + done);
}

void Online::ResolveHitRequest(OnlineMessages::HitRequest* m, bool peers_locked) {
    ObjectID target_id = GetObjectID(m->target);
    ObjectID attacker_id = GetObjectID(m->attacker);
    MovementObject* target = CharacterByID(target_id);
    MovementObject* attacker = CharacterByID(attacker_id);
    if (target == nullptr || attacker == nullptr || !IsLocalAvatar(target_id)) {
        return;  // gone, or not ours any more
    }

    int result = 0;  // _miss
    bool reaches = m->max_range <= 0.0f || distance(attacker->position, target->position) <= m->max_range + kHitRangeSlack;

    if (!reaches) {
        ++hits_out_of_range;
    }
    if (reaches || m->type == "attackblocked") {
        ++owned_script_depth;  // the defender's sounds are everyone's
        result = target->ResolveRemoteHit(m->type, m->attack_path, m->dir, m->pos, attacker_id, m->damage_mult, m->knockback_mult);
        --owned_script_depth;
    }

    ++hits_resolved;
    SendToOwnerOf(attacker_id, message_handler.Create<OnlineMessages::HitResult>(m->attacker, m->target, m->type, result), peers_locked);
}

// We run the attacker: act on the defender's answer (a block or dodge staggers us)
void Online::ReceiveHitResult(const OnlineMessageRef& ref, OnlineMessages::HitResult* m, PeerID from) {
    if (!host_started_level || !KnownHitType(m->type)) {
        return;
    }

    ObjectID attacker_id = GetObjectID(m->attacker);
    ObjectID target_id = GetObjectID(m->target);
    MovementObject* attacker = CharacterByID(attacker_id);

    if (attacker == nullptr) {
        return;
    }

    if (!IsLocalAvatar(attacker_id)) {
        if (IsHosting()) {
            SendToOwnerOf(attacker_id, ref, true);
        }
        return;
    }

    ++hit_results;

    string t = to_string(target_id), r = to_string(m->result);

    if (attacker->HasFunction("void OnHitResult(int target, const string &in type, int result)")) {
        attacker->Execute("OnHitResult(" + t + ", \"" + m->type + "\", " + r + ");");
    } else if (m->type == "blockprepare") {
        // aschar.as (1.4 and later): a block switches the attack to its
        // blocked animation while the impact is still ahead; a dodge staggers
        attacker->Execute(
            "if(state == _attack_state && target_id == " + t + ") {"
            " if(" + r + " == _going_to_block && attack_getter.GetAsLayer() != 1 && knife_layer_id == -1 &&"
            " this_mo.rigged_object().anim_client().GetTimeUntilEvent(\"attackimpact\") > 0.0f) {"
            " HandleAIEvent(_attack_failed); SwitchToBlockedAnim(); }"
            " else if(" + r + " == _going_to_dodge) { HandleAIEvent(_attack_failed); block_stunned = 0.5f; block_stunned_by_id = " + t + "; } }");
    } else {
        attacker->Execute(
            "if(" + r + " == _block_impact) { block_stunned = 0.5f; block_stunned_by_id = " + t + "; }"
            " else if(" + r + " == _going_to_dodge) { HandleAIEvent(_attack_failed); block_stunned = 0.5f; block_stunned_by_id = " + t + "; }");
    }
}

// ---- temporary ownership (throws, chokes, drags) ----------------------------------

void Online::BroadcastOwnerOverride(CommonObjectID cid, PlayerID owner, bool active, float max_seconds, PeerID except, bool peers_locked) {
    OnlineMessageRef ref = message_handler.Create<OnlineMessages::OwnerOverride>(cid, (int32_t)owner, active, max_seconds);

    if (IsClient()) {
        Send(ref);
        return;
    }

    vector<NetConnectionID> conns;
    {
        std::unique_lock<mutex> guard(online_session->connected_peers_lock, std::defer_lock);
        if (!peers_locked) {
            guard.lock();
        }
        for (const Peer& peer : online_session->connected_peers) {
            if (peer.peer_id != except) {
                conns.push_back(peer.conn_id);
            }
        }
    }

    for (NetConnectionID conn : conns) {
        SendTo(conn, ref);
    }
}

// Take a character another machine runs, for a throw, choke or drag that
// starts now on ours. True if we run it now (or always did).
bool Online::TakeOver(ObjectID id, float max_seconds) {
    if (!IsActive() || online_session == nullptr) {
        return true;
    }

    if (IsLocalAvatar(id)) {
        return true;
    }

    SceneGraph* sg = Engine::Instance()->GetSceneGraph();
    Object* o = sg != nullptr ? sg->GetObjectFromID(id) : nullptr;
    if (o == nullptr || o->GetType() != _movement_object) {
        return false;
    }

    // Just handed back (a revived teammate's body goes home to get up): a hand
    // still on the grab key doesn't take it again for a moment
    CommonObjectID cid = GetOriginalID(id);
    auto back = handed_back_ms.find(cid);
    if (back != handed_back_ms.end() && OnlineClockMs() - back->second < 2000.0) {
        return false;
    }

    OwnerOverrideEntry& e = owner_overrides[cid];
    e.owner = online_session->local_player_id;
    e.since_ms = OnlineClockMs();
    e.max_ms = max_seconds * 1000.0;
    static_cast<MovementObject*>(o)->SetNetworkProxy(false);  // now, not next update: the caller acts on it at once
    BroadcastOwnerOverride(cid, e.owner, true, max_seconds, online_session->local_player_id, false);
    ++takeovers;
    LOGI << "Took over character " << id << " for up to " << max_seconds << " s" << std::endl;
    return true;
}

void Online::ReleaseTakeOver(ObjectID id) {
    CommonObjectID cid = GetOriginalID(id);
    auto it = owner_overrides.find(cid);

    if (it == owner_overrides.end() || online_session == nullptr || it->second.owner != online_session->local_player_id) {
        return;
    }

    owner_overrides.erase(it);
    handed_back_ms[cid] = OnlineClockMs();
    BroadcastOwnerOverride(cid, online_session->local_player_id, false, 0.0f, online_session->local_player_id, false);
    LOGI << "Handed back character " << id << std::endl;
}

void Online::ReceiveOwnerOverride(CommonObjectID cid, PlayerID owner, bool active, float max_seconds, PeerID from) {
    if (!host_started_level || online_session == nullptr) {
        return;
    }

    auto it = owner_overrides.find(cid);

    if (IsHosting()) {
        // The host keeps the table: the first taker holds it until it lets go
        if (active && it != owner_overrides.end() && it->second.owner != from) {
            BroadcastOwnerOverride(cid, it->second.owner, true, (float)(it->second.max_ms / 1000.0), 0, true);  // tells the latecomer
            return;
        }
        if (!active && (it == owner_overrides.end() || it->second.owner != from)) {
            return;
        }
        owner = from;
    }

    if (active) {
        OwnerOverrideEntry& e = owner_overrides[cid];
        e.owner = owner;
        e.since_ms = OnlineClockMs();
        e.max_ms = max_seconds * 1000.0;
    } else if (it != owner_overrides.end()) {
        owner_overrides.erase(it);
    }

    if (IsHosting()) {
        BroadcastOwnerOverride(cid, owner, active, max_seconds, from, true);
    }
}

// Ours to hand back: when the grabbed one is free and settled, or the time is up
void Online::UpdateTakeOvers(SceneGraph* sg) {
    if (online_session == nullptr || sg == nullptr) {
        return;
    }

    const int kMovementState = 0, kRagdollState = 4, kAwake = 0, kTetheredFree = 0;  // aschar.as
    double now = OnlineClockMs();
    vector<ObjectID> release;

    for (auto& it : owner_overrides) {
        if (it.second.owner != online_session->local_player_id) {
            continue;
        }

        ObjectID id = GetObjectID(it.first);
        Object* o = sg->GetObjectFromID(id);
        double held = now - it.second.since_ms;

        if (o == nullptr || o->GetType() != _movement_object || held > it.second.max_ms) {
            release.push_back(id);
            continue;
        }

        MovementObject* mo = static_cast<MovementObject*>(o);
        if (held < 600.0 || !mo->NetworkVitalsSupported() || mo->ASGetIntVar("tethered") != kTetheredFree) {
            it.second.settled_ms = -1.0;
            continue;
        }

        int state = mo->ASGetIntVar("state");
        bool settled = state == kMovementState || mo->ASGetIntVar("knocked_out") != kAwake ||
                       (state == kRagdollState && length(mo->velocity) < 1.5f);

        // Settled for a second: its last state (a teammate got it up: awake)
        // has reached its machine before it takes the character back
        if (!settled) {
            it.second.settled_ms = -1.0;
        } else if (it.second.settled_ms < 0.0) {
            it.second.settled_ms = now;
        } else if (now - it.second.settled_ms > 1000.0) {
            release.push_back(id);
        }
    }

    for (ObjectID id : release) {
        ReleaseTakeOver(id);
    }
}

// ---- thrown weapons ---------------------------------------------------------------

void Online::QueueItemLaunch(ObjectID item) {
    if (IsClient() && std::find(pending_item_launches.begin(), pending_item_launches.end(), item) == pending_item_launches.end()) {
        pending_item_launches.push_back(item);
    }
}

// Client, end of the step: the throw's velocity and spin are set by now
void Online::SendPendingItemLaunches(SceneGraph* sg) {
    for (ObjectID id : pending_item_launches) {
        Object* o = sg->GetObjectFromID(id);
        if (o == nullptr || o->GetType() != _item_object) {
            continue;
        }

        ItemObject* item = static_cast<ItemObject*>(o);
        if (item->IsHeld()) {
            continue;
        }

        OnlineMessageRef ref = message_handler.Create<OnlineMessages::ItemLaunch>();
        OnlineMessages::ItemLaunch* m = static_cast<OnlineMessages::ItemLaunch*>(ref.GetData());
        mat4 t = item->GetPhysicsTransform();
        m->item = GetOriginalID(id);
        m->thrower = item->last_held_char_id_ == -1 ? -1 : GetOriginalID(item->last_held_char_id_);
        m->position = t.GetTranslationPart();
        m->rotation_x = t.GetColumnVec3(0);
        m->rotation_y = t.GetColumnVec3(1);
        m->linear_velocity = item->GetLinearVelocity();
        m->angular_velocity = item->GetAngularVelocity();
        m->straight = item->IsThrownStraight();
        Send(ref);
        ++item_launches;
    }

    pending_item_launches.clear();
}

void Online::ReceiveItemLaunch(OnlineMessages::ItemLaunch* m, PeerID from) {
    if (!IsHosting() || !host_started_level) {
        return;
    }

    SceneGraph* sg = Engine::Instance()->GetSceneGraph();
    Object* o = sg != nullptr ? sg->GetObjectFromID(GetObjectID(m->item)) : nullptr;
    if (o == nullptr || o->GetType() != _item_object) {
        return;
    }

    ItemObject* item = static_cast<ItemObject*>(o);
    ObjectID thrower = m->thrower == -1 ? -1 : GetObjectID(m->thrower);

    if (item->IsHeld()) {
        Object* holder = sg->GetObjectFromID(item->HeldByWhom());
        if (holder == nullptr || item->HeldByWhom() != thrower || holder->GetType() != _movement_object) {
            return;  // someone else holds it here
        }
        static_cast<MovementObject*>(holder)->NetworkDetachItem(item->GetID());  // its record saying so is on the way
    }
    item->net_released_ms = OnlineClockMs();

    vec3 z = normalize(cross(m->rotation_x, m->rotation_y));
    mat4 t;
    t.SetColumn(0, m->rotation_x);
    t.SetColumn(1, m->rotation_y);
    t.SetColumn(2, z);
    t.SetTranslationPart(m->position);
    item->SetPhysicsTransform(t);  // (puts it out of the physics world, as for a held item)
    item->ActivatePhysics();       // back in: it flies from here
    item->SetLinearVelocity(m->linear_velocity);
    item->SetAngularVelocity(m->angular_velocity);
    item->WakeUpPhysics();
    if (m->thrower != -1) {
        item->SetHolderID(GetObjectID(m->thrower));
    }
    if (m->straight) {
        item->SetThrownStraight();
    } else {
        item->SetThrown();
    }
    ++item_launches;
}

// Host: our thrown item hit a character. Everyone sticks it in; its owner takes the hit.
void Online::SendItemHit(ObjectID character, ObjectID item, const vec3& point, const string& material, int type, const vec3& velocity,
                         const vec3& stick_start, const vec3& stick_end, const mat4& stick_transform) {
    if (!IsHosting()) {
        return;
    }

    OnlineMessageRef ref = message_handler.Create<OnlineMessages::ItemHit>();
    OnlineMessages::ItemHit* m = static_cast<OnlineMessages::ItemHit*>(ref.GetData());
    m->character = GetOriginalID(character);
    m->item = GetOriginalID(item);
    m->point = point;
    m->material = material;
    m->type = type;
    m->velocity = velocity;
    m->stick_start = stick_start;
    m->stick_end = stick_end;
    m->stick_pos = stick_transform.GetTranslationPart();
    m->stick_x = stick_transform.GetColumnVec3(0);
    m->stick_y = stick_transform.GetColumnVec3(1);
    Send(ref);
    ++item_hits;
}

void Online::ReceiveItemHit(OnlineMessages::ItemHit* m) {
    SceneGraph* sg = Engine::Instance()->GetSceneGraph();
    if (sg == nullptr || !host_started_level) {
        return;
    }

    ObjectID char_id = GetObjectID(m->character);
    ObjectID item_id = GetObjectID(m->item);
    Object* co = sg->GetObjectFromID(char_id);
    Object* io = sg->GetObjectFromID(item_id);
    if (co == nullptr || io == nullptr || co->GetType() != _movement_object || io->GetType() != _item_object) {
        return;
    }

    MovementObject* mo = static_cast<MovementObject*>(co);
    ItemObject* item = static_cast<ItemObject*>(io);

    if (m->type == 2 && !item->IsHeld() && item->StuckInWhom() == -1) {
        vec3 z = normalize(cross(m->stick_x, m->stick_y));
        mat4 t;
        t.SetColumn(0, m->stick_x);
        t.SetColumn(1, m->stick_y);
        t.SetColumn(2, z);
        t.SetTranslationPart(m->stick_pos);
        mo->rigged_object()->StickItem(item_id, m->stick_start, m->stick_end, t);
    }

    if (IsLocalAvatar(char_id)) {
        item->SetLinearVelocity(m->velocity);  // its script weighs the blow by the item's speed
        ++owned_script_depth;
        mo->HitByItem(item_id, m->point, m->material, m->type);
        --owned_script_depth;
        ++item_hits;
    }
}

// Host: the character's machine lets go of an item someone else got first
void Online::SendItemDrop(ObjectID character, ObjectID item) {
    if (IsHosting()) {
        pending_drop_orders.push_back(std::make_pair(character, item));  // sent from LateUpdate (the peers lock is held here)
    }
}

void Online::ReceiveItemDrop(CommonObjectID character, CommonObjectID item) {
    SceneGraph* sg = Engine::Instance()->GetSceneGraph();
    ObjectID char_id = GetObjectID(character);
    ObjectID item_id = GetObjectID(item);
    Object* co = sg != nullptr ? sg->GetObjectFromID(char_id) : nullptr;
    Object* io = sg != nullptr ? sg->GetObjectFromID(item_id) : nullptr;

    if (co == nullptr || io == nullptr || co->GetType() != _movement_object || io->GetType() != _item_object || !IsLocalAvatar(char_id)) {
        return;
    }

    MovementObject* mo = static_cast<MovementObject*>(co);
    if (static_cast<ItemObject*>(io)->HeldByWhom() == char_id) {
        mo->NetworkDetachItem(item_id);
        mo->Execute("NotifyItemDetach(" + to_string(item_id) + ");");
        LOGI << "Dropped item " << item_id << ": someone else got it first" << std::endl;
    }
}

vector<ObjectID> Online::GetLocalAvatarIDs() const {
    vector<ObjectID> local_avatar_ids;
    SceneGraph* sg = Engine::Instance()->GetSceneGraph();
    if (sg != nullptr) {
        for (const auto& it : sg->GetControllableMovementObjects()) {
            if (IsLocalAvatar(it->GetID())) {
                local_avatar_ids.push_back(it->GetID());
            }
        }
    }
    return local_avatar_ids;
}

PlayerState Online::GenerateHostPlayerState() const {
    if (!IsHosting()) {
        LOGW << "Called GenerateHostPlayerState(), but wasn't hosting. Only the host should generate playerstates, especially his own!" << std::endl;
        PlayerState player_state;
        return player_state;
    }

    PlayerState host_player_state;
    host_player_state.ping = 0;
    host_player_state.playername = OnlineUtility::GetPlayerName();
    host_player_state.object_id = -1;
    return host_player_state;
}

void Online::AddLocalChatMessage(string chat_message) {
    lock_guard<mutex> guard(online_session->chat_messages_lock);
    online_session->chat_messages.push_back({game_timer.game_time, chat_message});
}

void Online::RemoveOldChatMessages(float threshold) {
    lock_guard<mutex> guard(online_session->chat_messages_lock);
    online_session->chat_messages.erase(remove_if(online_session->chat_messages.begin(), online_session->chat_messages.end(),
                                                  [threshold](ChatMessage i) { return game_timer.game_time - i.time > threshold; }),
                                        online_session->chat_messages.end());
}

const vector<ChatMessage>& Online::GetChatMessages() const {
    return online_session->chat_messages;
}

void Online::AddPeer(NetFrameworkConnectionStatusChanged* data) {
    if (IsHosting()) {
        if (IsLobbyFull()) {
            CloseConnection(data->conn_id, ConnectionClosedReason::LOBBY_FULL);
            AddLocalChatMessage(string("Somebody tried connecting but we don't have any slots free"));
        } else {
            Peer peer;
            peer.conn_id = data->conn_id;
            peer.peer_id = next_free_peer_id++;
            peer.m_info = data->conn_info;
            {
                lock_guard<mutex> guard(online_session->connected_peers_lock);
                online_session->connected_peers.push_back(peer);
                online_session->client_connection_manager.AddConnection(peer.peer_id, data->conn_id);
            }
        }
    } else {
        Peer peer;
        peer.conn_id = data->conn_id;
        peer.peer_id = 0;
        peer.m_info = data->conn_info;
        {
            lock_guard<mutex> guard(online_session->connected_peers_lock);
            online_session->connected_peers.push_back(peer);
        }
    }
}

/// Queue termination of a connection
void Online::CloseConnection(NetConnectionID conn_id, ConnectionClosedReason reason) {
    if (online_session != nullptr) {
        ConnectionToBeClosed temp;
        temp.conn_id = conn_id;
        temp.reason = reason;

        lock_guard<mutex> guard(close_queue_lock);
        online_session->connections_waiting_to_be_closed.push(temp);
    } else {
        LOGW << "Attempted to close a connection without a valid online session" << endl;
        assert(online_session != nullptr);  // Kill the application for now, this should not be happening!
    }
}

/// Close a connection right away, possibly unsafe to call from anywhere
void Online::CloseConnectionImmediate(NetConnectionID conn_id, ConnectionClosedReason reason) {
    lock_guard<mutex> guard(online_session->connected_peers_lock);

    net.CloseConnection(conn_id, reason);

    vector<Peer>::iterator peer = GetPeerIt(conn_id);
    if (peer != online_session->connected_peers.end()) {
        PlayerID player_id = peer->peer_id;  // TODO We assume peer_id == player_id
        if (online_session->player_states.find(player_id) != online_session->player_states.end()) {
            auto& player_state = online_session->player_states[player_id];

            SendRawChatMessage(player_state.playername + " disconnected!");

            // Clean up objects assigned to disconnected player
            SceneGraph* graph = Engine::Instance()->GetSceneGraph();
            if (graph != nullptr) {
                Object* o = graph->GetObjectFromID(player_state.object_id);
                if (o != nullptr) {
                    if (o->created_on_the_fly) {
                        graph->map_editor->RemoveObject(o, graph);
                    } else {
                        online_session->free_avatars.push_back(player_state.object_id);
                    }
                    ((MovementObject*)o)->remote = false;
                }
            }

            // Hand back what they held for a throw, choke or drag
            for (auto ov = owner_overrides.begin(); ov != owner_overrides.end();) {
                if (ov->second.owner == player_id) {
                    BroadcastOwnerOverride(ov->first, 0, false, 0.0f, player_id, true);
                    ov = owner_overrides.erase(ov);
                } else {
                    ++ov;
                }
            }

            // Remove playerstate
            online_session->player_states.erase(player_id);
            Send<OnlineMessages::RemovePlayerState>(player_id);

            // TODO do we need to clean the camera and controller of the player?
        }

        online_session->client_connection_manager.RemoveConnection(peer->peer_id);
        online_session->connected_peers.erase(peer);
    } else {
        LOGW << "Tried closing a connection, but there is no peer assigned to NetConnectionID: " << conn_id << std::endl;
    }
}

void Online::CheckPendingMessages() {
    // Locking online_session->connected_peers_lock here because it's used in some arrays here, and if done more specifically
    // theres a risk for overlapping locks with CheckNewMessages(), causing a full lockup.
    // TODO: Reduce number of locks in multiplayer.
    lock_guard<mutex> guard(online_session->connected_peers_lock);

    {
        lock_guard<mutex> guard(online_session->incoming_messages_lock);

        for (auto& it : online_session->incoming_messages) {
            message_handler.Execute(it.message, it.from);

            // Overgrowth Online: a client's character sounds go to the other clients too
            if (IsHosting()) {
                OnlineMessageType type = message_handler.GetMessageType(it.message);
                if (type == message_handler.GetMessageType<OnlineMessages::AudioPlaySoundLocationMessage>() ||
                    type == message_handler.GetMessageType<OnlineMessages::AudioPlaySoundGroupMessage>() ||
                    type == message_handler.GetMessageType<OnlineMessages::AudioPlayGroupPriorityMessage>() ||
                    type == message_handler.GetMessageType<OnlineMessages::AudioPlaySoundGroupGainMessage>() ||
                    type == message_handler.GetMessageType<OnlineMessages::AudioPlaySoundGroupVoiceMessage>() ||
                    type == message_handler.GetMessageType<OnlineMessages::MaterialSoundEvent>()) {
                    RelayFromPeer(it.message, it.from);
                    ++sounds_from_clients;
                } else if (type == message_handler.GetMessageType<OnlineMessages::SetAvatarPalette>()) {
                    Send(it.message);  // to everyone, and kept for late joiners (its sender ignores its own)
                }
            }
        }

        online_session->incoming_messages.clear();
    }

    if (host_started_level) {
        ApplyPlayerInput();
    }
}

void Online::ApplyPlayerInput() {
    for (auto& it : online_session->player_inputs) {
        // We don't want to apply two inputs of the same key on the same frame
        // Releasing and pressing down the key should not be processed on the same frame
        unordered_map<uint8_t, int> depth;

        for (list<OnlineMessageRef>::iterator i = it.second.begin(); i != it.second.end();) {
            PlayerInputMessage* piks = static_cast<PlayerInputMessage*>(it.second.front().GetData());

            // Get player involved in the input, discard input if missing
            if (online_session->player_states.find(it.first) == online_session->player_states.end()) {
                break;
            }

            int controller_id = online_session->player_states[it.first].controller_id;
            PlayerInput* player_input = Input::Instance()->GetController(controller_id);
            if (player_input == nullptr) {
                break;
            }

            if (depth.find(piks->id) != depth.end()) {
                int velocity = piks->count - depth[piks->id];

                // a negative velocity mean't that we pressed and release
                // process this later.
                if (velocity < 0) {
                    i++;
                    continue;
                }

                // this is a first frame down, process all other inputs on the next frame
                if (depth[piks->id] == 1) {
                    i++;
                    continue;
                }
            }

            // process the input
            KeyState& key_state = player_input->key_down[online_session->binding_name_map_[piks->id]];
            depth[piks->id] = piks->count;
            key_state.count = piks->count;
            key_state.depth_count = piks->depth_count;
            key_state.depth = piks->depth;

            // we are done with this input
            it.second.erase(i++);
        }
    }
}

PlayerState Online::GetOwnPlayerState() {
    std::lock_guard<std::mutex> guard(online_session->connected_peers_lock);

    auto it = online_session->player_states.find(online_session->local_player_id);
    if (it != online_session->player_states.end()) {
        return it->second;
    }

    LOGW << "Tried to get own players state, but there is no state registered for player id: " << std::to_string(online_session->local_player_id) << std::endl;
    PlayerState fallback;
    return fallback;
}

bool Online::TryGetPlayerState(PlayerState& player_state, ObjectID object_id) const {
    if (IsActive()) {
        for (auto& it : online_session->player_states) {
            if (it.second.object_id == object_id) {
                player_state = it.second;
                return true;
            }
        }
    }

    return false;
}

void Online::AssignNewControllerForPlayer(PlayerID player_id) {
    online_session->player_states[player_id].controller_id = Input::Instance()->AllocateRemotePlayerInput();
    online_session->player_states[player_id].camera_id = ActiveCameras::Instance()->CreateVirtualCameraInstance();
}

size_t Online::CompressData(vector<char>& target_buffer, const void* source_buffer, uint32_t source_size) {
    size_t maxSize = ZSTD_compressBound(source_size);

    if (target_buffer.size() < maxSize) {
        target_buffer.resize(maxSize);
    }

    size_t compressedSize = ZSTD_compressCCtx(zstdCContext,
                                              target_buffer.data(),
                                              target_buffer.size(),
                                              source_buffer,
                                              source_size,
                                              compression_level);

    if (compressedSize > maxSize) {
        LOGE << "Failed to compress data, assumed size was smaller than the real size" << endl;
    }

    target_buffer.resize(compressedSize);

    return compressedSize;
}

size_t Online::DecompressData(vector<char>& target_buffer, const void* source_buffer, size_t source_size) {
    size_t decompressed_size = ZSTD_getFrameContentSize(source_buffer, source_size);

    if (decompressed_size == ZSTD_CONTENTSIZE_UNKNOWN) {
        LOGW << "Unable to determine uncompressed package size" << std::endl;
    } else if (decompressed_size == ZSTD_CONTENTSIZE_ERROR) {
        LOGE << "Error while trying to determine content size" << std::endl;
    } else if (target_buffer.size() < decompressed_size) {
        target_buffer.resize(decompressed_size);
    }

    size_t final_decompressed_size = 0;
    while (true) {
        size_t final_decompressed_size = ZSTD_decompressDCtx(zstdDContext, target_buffer.data(), target_buffer.size(), source_buffer, source_size);
        if (ZSTD_isError(final_decompressed_size)) {
            LOGE << ZSTD_getErrorString(ZSTD_getErrorCode(final_decompressed_size)) << endl;
        } else if (final_decompressed_size > target_buffer.size()) {
            // If the final decompressed size is larger than the target buffer, re-do the decompression after resizing.
            target_buffer.resize(final_decompressed_size);
            continue;
        }
        break;
    }

    return final_decompressed_size;
}

uint32_t Online::GetPlayerCount() {
    lock_guard<mutex> guard(online_session->connected_peers_lock);
    return online_session->player_states.size();
}

/// Hosts will add their name here, clients will have their name added by the host later
void Online::BroadcastChatMessage(const string& chat_message) {
    if (IsActive()) {
        if (IsHosting()) {
            if (OnlineUtility::IsCommand(chat_message)) {
                OnlineUtility::HandleCommand(online_session->local_player_id, chat_message);
                AddLocalChatMessage(OnlineUtility::GetPlayerName() + ": " + chat_message);
            } else {
                SendRawChatMessage(OnlineUtility::GetPlayerName() + ": " + chat_message);
            }
        } else {
            SendRawChatMessage(chat_message);
        }
    }
}

void Online::SendRawChatMessage(const string& raw_chat_entry) {
    if (IsActive()) {
        Send<OnlineMessages::ChatEntryMessage>(raw_chat_entry);
        if (IsHosting()) {
            AddLocalChatMessage(raw_chat_entry);
        }
    }
}

vector<PlayerState> Online::GetPlayerStates() {
    if (IsActive()) {
        vector<PlayerState> player_states;
        for (const auto& it : online_session->player_states) {
            player_states.push_back(it.second);
        }
        return player_states;
    }

    vector<PlayerState> empty;
    return empty;
}

void Online::Send(const OnlineMessageRef& message_ref) {
    if (IsActive()) {
        lock_guard<mutex> guard(online_session->outgoing_messages_lock);
        online_session->outgoing_messages.push_back({true, 0, message_ref});
    }
}

void Online::SendTo(NetConnectionID target, const OnlineMessageRef& message_ref) {
    if (IsActive()) {
        lock_guard<mutex> guard(online_session->outgoing_messages_lock);
        online_session->outgoing_messages.push_back({false, target, message_ref});
    }
}

void Online::SendIntFloatScriptParam(uint32_t id, const string& key, const ScriptParam& param) {
    if (param.IsFloat()) {
        Send<OnlineMessages::SPUnionMessage>(id, key, param.GetFloat(), ScriptParam::ScriptParamType::FLOAT, param.editor().type(), param.editor().GetDetails());
    } else {
        Send<OnlineMessages::SPUnionMessage>(id, key, param.GetInt(), ScriptParam::ScriptParamType::INT, param.editor().type(), param.editor().GetDetails());
    }
}

void Online::SendStringScriptParam(uint32_t id, const string& key, const ScriptParam& param) {
    Send<OnlineMessages::SPStringMessage>(id, key, param.GetStringForSocket(), param.editor().type(), param.editor().GetDetails());
}

void Online::SendScriptParam(uint32_t id, const string& key, const ScriptParam& param) {
    if (param.IsString()) {
        SendStringScriptParam(id, key, param);
    } else {
        SendIntFloatScriptParam(id, key, param);
    }
}

void Online::SendScriptParamMap(uint32_t id, const ScriptParamMap& param) {
    for (auto& it : param) {
        SendScriptParam(id, it.first, it.second);
    }
}

bool Online::GetHostAllowsClientEditor() const {
    if (online_session == nullptr)
        return false;
    return online_session->host_session_flags[OnlineFlags::ALLOWSEDITOR];
}

void Online::SetIfHostAllowsClientEditor(bool mode) {
    if (online_session != nullptr) {
        online_session->host_session_flags[OnlineFlags::ALLOWSEDITOR] = mode;
    }
}

void Online::SetDefaultHotJoinCharacter(const string& path) {
    default_hot_join_characte_path = path;
}

string Online::GetDefaultHotJoinCharacter() const {
    return default_hot_join_characte_path;
}

void Online::SessionStarted(bool host_started_the_level) {
    host_started_level = host_started_the_level;
}

bool Online::ForceMapStartOnLoad() const {
    return host_started_level;
}

bool Online::AllClientsReady() {
    lock_guard<mutex> guard(online_session->connected_peers_lock);
    return online_session->client_connection_manager.IsEveryClientLoaded();
}

ScriptParams* Online::GetScriptParamsFromID(ObjectID id) {
    if (Engine::Instance()->GetSceneGraph() != nullptr) {
        if (id == numeric_limits<uint32_t>::max())
            return &Engine::Instance()->GetSceneGraph()->level->script_params();

        Object* o = Engine::Instance()->GetSceneGraph()->GetObjectFromID(id);
        if (o != nullptr)
            return o->GetScriptParams();
    }
    return nullptr;
}

void Online::UpdateMovementObjectFromID(uint32_t id) {
    if (id != numeric_limits<uint32_t>::max()) {
        Object* o = Engine::Instance()->GetSceneGraph()->GetObjectFromID(id);
        if (o != nullptr && o->GetType() == EntityType::_movement_object) {
            ((MovementObject*)o)->UpdateScriptParams();
        }
    }
}

bool Online::SetAvatarCameraAttachedMode(bool mode) {
    return attach_avatar_camera = mode;
}

bool Online::IsAvatarCameraAttached() const {
    return attach_avatar_camera && Engine::Instance()->GetSceneGraph()->map_editor->state_ == MapEditor::kInGame;
}

void Online::SendLevelMessage(const string& msg) {
    // Overgrowth Online: level scripts' own channel to the clients ("ohl ...")
    if (msg.compare(0, 4, "ohl ") == 0) {
        Send<OnlineMessages::SendLevelMessage>(msg);  // the host's ids are the common ones
        return;
    }

    // we currently only want start_dialogue
    if (msg.find("reset") == string::npos)
        return;  // tutorial is very spammy

    Send<OnlineMessages::SendLevelMessage>(msg);
}

// "#<id>" tokens: our ids to the host's (to_common) or the host's to ours
string Online::TranslateIDs(const string& msg, bool to_common) const {
    string out;
    size_t i = 0;

    while (i < msg.size()) {
        if (msg[i] == '#' && i + 1 < msg.size() && (isdigit((unsigned char)msg[i + 1]) || msg[i + 1] == '-')) {
            size_t j = i + 1;
            if (msg[j] == '-') {
                ++j;
            }
            while (j < msg.size() && isdigit((unsigned char)msg[j])) {
                ++j;
            }
            int id = atoi(msg.substr(i + 1, j - i - 1).c_str());
            if (id >= 0) {
                id = to_common ? GetOriginalID(id) : GetObjectID(id);
            }
            out += "#" + to_string(id);
            i = j;
        } else {
            out += msg[i++];
        }
    }

    return out;
}

// A standing character's position over its ground (aschar.as: legs and char sphere)
static const float kStandHeight = 0.75f;

// A point in a hotspot's box (its local -2..2 on each axis; Hotspot::UpdateCollisionShape)
static vec3 HotspotLocal(Hotspot* h, const vec3& p) {
    return invert(h->GetTransform()) * p;
}

static bool InBox(const vec3& l) {
    return std::fabs(l.x()) <= 2.0f && std::fabs(l.y()) <= 2.0f && std::fabs(l.z()) <= 2.0f;
}

bool Online::GroundHazard(const vec3& ground) const {
    SceneGraph* sg = Engine::Instance()->GetSceneGraph();
    if (sg == nullptr) {
        return false;
    }
    for (Object* o : sg->hotspots_) {
        if (o == nullptr || o->GetType() != _hotspot_object || !o->enabled_) {
            continue;
        }
        Hotspot* h = static_cast<Hotspot*>(o);
        const string& file = h->GetScriptFile();
        bool water = file.find("wet_cube") != string::npos || file.find("wet_cylinder") != string::npos;
        bool lethal = file.find("kill") != string::npos || file.find("spike") != string::npos ||
                      (file.find("checkpoint") != string::npos && h->GetScriptParams()->HasParam("fall_death"));
        if (water) {
            // Deeper than a wade (the surface is the top of the box), or lethal water
            vec3 l = HotspotLocal(h, ground);
            bool inside = file.find("wet_cylinder") != string::npos ? l.x() * l.x() + l.z() * l.z() <= 4.0f
                                                                      : std::fabs(l.x()) <= 2.0f && std::fabs(l.z()) <= 2.0f;
            if (inside && l.y() <= 2.0f && l.y() >= -2.5f) {
                float surface = (h->GetTransform() * vec3(l.x(), 2.0f, l.z())).y();
                if (surface - ground.y() > 0.5f || h->GetScriptParams()->HasParam("Lethal")) {
                    return true;
                }
            }
        } else if (lethal) {
            // The feet or the body in it
            if (InBox(HotspotLocal(h, ground)) || InBox(HotspotLocal(h, ground + vec3(0.0f, 1.0f, 0.0f)))) {
                return true;
            }
        }
    }
    return false;
}

bool Online::FindSafeSpot(const vec3& base, const vec3& facing, int index, vec3* spot) {
    SceneGraph* sg = Engine::Instance()->GetSceneGraph();
    if (sg == nullptr || sg->bullet_world_ == nullptr) {
        return false;
    }
    BulletWorld* world = sg->bullet_world_;
    const vec3 up(0.0f, 1.0f, 0.0f);

    // The ground under the base point (a jump or a step off a ledge: further down)
    vec3 hit;
    if (world->CheckRayCollision(base + up * 0.5f, base - up * 6.0f, &hit) == nullptr) {
        return false;
    }
    const float ground = hit.y();
    float above = base.y() - ground;  // how high a standing character's position sits over its ground
    if (above < 0.3f || above > 1.3f) {
        above = kStandHeight;  // (the base wasn't a standing character)
    }

    vec3 f = facing;
    f.y() = 0.0f;
    f = length_squared(f) > 1e-6f ? normalize(f) : vec3(0.0f, 0.0f, 1.0f);
    vec3 right(-f.z(), 0.0f, f.x());

    // Index 0: the base itself first. Then beside, behind, in front, in three
    // rings; each player starts at its own place in the first
    const float kSide[] = {1.0f, -1.0f, 0.0f, 1.0f, -1.0f, 0.0f, 1.0f, -1.0f};
    const float kBack[] = {0.0f, 0.0f, -1.0f, -1.0f, -1.0f, 1.0f, 1.0f, 1.0f};
    const float kRing[] = {1.2f, 2.4f, 3.6f};
    vector<vec3> candidates;
    if (index <= 0) {
        candidates.push_back(vec3(base.x(), ground, base.z()));
    }
    for (float r : kRing) {
        for (int t = 0; t < 8; ++t) {
            int k = (std::max(0, index - 1) + t) % 8;
            vec3 d = right * kSide[k] + f * kBack[k];
            candidates.push_back(vec3(base.x(), ground, base.z()) + normalize(d) * r);
        }
    }

    const vec3 from(base.x(), ground, base.z());
    for (const vec3& c : candidates) {
        bool is_base = length_squared(c - from) < 1e-4f;

        // Nothing in between (a wall, a rock)
        if (!is_base && (world->CheckRayCollision(from + up * 0.6f, c + up * 0.6f) != nullptr ||
                         world->CheckRayCollision(from + up * 1.3f, c + up * 1.3f) != nullptr)) {
            continue;
        }
        // Solid ground at about the same height (no drop, no sea floor)
        vec3 g;
        if (world->CheckRayCollision(c + up * 1.5f, c - up * 2.5f, &g) == nullptr) {
            continue;
        }
        if (!is_base && std::fabs(g.y() - ground) > 0.6f) {
            continue;
        }
        // Room to stand
        if (world->CheckRayCollision(g + up * 0.1f, g + up * 1.8f) != nullptr) {
            continue;
        }
        // Ground all around it (not on the very edge of a drop)
        bool footing = true;
        for (int s = 0; s < 4 && footing; ++s) {
            vec3 o = (s < 2 ? right : f) * (s % 2 == 0 ? 0.45f : -0.45f);
            vec3 g2;
            footing = world->CheckRayCollision(g + o + up * 0.6f, g + o - up * 0.9f, &g2) != nullptr;
        }
        if (!footing || GroundHazard(g)) {
            continue;
        }
        *spot = vec3(g.x(), g.y() + above, g.z());
        return true;
    }
    return false;
}

vec3 Online::SafeSpotNear(const vec3& base, const vec3& facing, int index) {
    vec3 spot;
    return FindSafeSpot(base, facing, index, &spot) ? spot : base;
}

// Every quarter second: where each player's character stands safely
void Online::TrackSafeGround(SceneGraph* sg) {
    if (sg == nullptr || sg->bullet_world_ == nullptr || OnlineClockMs() - last_safe_ground_ms < 250.0) {
        return;
    }
    last_safe_ground_ms = OnlineClockMs();
    if (last_safe_level != level_name) {  // (a client may not see ChangeLevel)
        last_safe_level = level_name;
        last_safe_ground.clear();
    }
    for (ObjectID id : PlayerAvatarIDs()) {
        Object* o = sg->GetObjectFromID(id);
        if (o == nullptr || o->GetType() != _movement_object) {
            continue;
        }
        MovementObject* mo = static_cast<MovementObject*>(o);
        if (mo->ASGetIntVar("knocked_out") != 0 || !mo->ASGetBoolVar("on_ground") || mo->ASGetBoolVar("dialogue_control")) {
            continue;
        }
        vec3 g;
        if (sg->bullet_world_->CheckRayCollision(mo->position, mo->position - vec3(0.0f, 1.5f, 0.0f), &g) != nullptr && !GroundHazard(g)) {
            last_safe_ground[id] = mo->position;
        }
    }
}

// A spot for player number `index` beside the character `anchor_id` (where it
// is, else where it last stood safely), and the way it faces
static bool SpotBeside(Online* online, SceneGraph* sg, ObjectID anchor_id, int index, vec3* spot, vec3* facing) {
    Object* o = sg != nullptr ? sg->GetObjectFromID(anchor_id) : nullptr;
    if (o == nullptr || o->GetType() != _movement_object) {
        return false;
    }
    MovementObject* anchor = static_cast<MovementObject*>(o);
    *facing = anchor->GetFacing();
    if (online->FindSafeSpot(anchor->position, *facing, index, spot)) {
        return true;
    }
    auto it = online->last_safe_ground.find(anchor_id);
    return it != online->last_safe_ground.end() && online->FindSafeSpot(it->second, *facing, index, spot);
}

ObjectID Online::HostAvatarID() const {
    if (online_session == nullptr) {
        return -1;
    }
    // The host's own player state (its local_player_id, -1 as a PlayerID) on every machine
    auto it = online_session->player_states.find((PlayerID)-1);
    if (it != online_session->player_states.end() && it->second.object_id >= 0) {
        return it->second.object_id;
    }
    if (IsHosting()) {
        vector<ObjectID> mine = GetLocalAvatarIDs();
        return mine.empty() ? -1 : mine[0];
    }
    return -1;
}

int Online::GuestIndex(ObjectID id) const {
    if (online_session == nullptr) {
        return 0;
    }
    ObjectID host = HostAvatarID();
    vector<PlayerID> guests;
    for (const auto& it : online_session->player_states) {
        if (it.second.object_id >= 0 && it.second.object_id != host) {
            guests.push_back(it.first);
        }
    }
    std::sort(guests.begin(), guests.end());
    for (size_t k = 0; k < guests.size(); ++k) {
        if (online_session->player_states.at(guests[k]).object_id == id) {
            return (int)k + 1;
        }
    }
    return 0;
}

// ---- Your online character -----------------------------------------------------

void Online::SendAvatarCharacter(ObjectID character, const string& path) {
    if (IsActive() && host_started_level) {
        Send<OnlineMessages::AvatarCharacter>(GetOriginalID(character), path);
    }
}

void Online::ReceiveAvatarCharacter(CommonObjectID character, const string& path) {
    // At once, before the colours sent after it (they are the new character's channels)
    SceneGraph* sg = Engine::Instance()->GetSceneGraph();
    ObjectID id = GetObjectID(character);
    Object* o = sg != nullptr ? sg->GetObjectFromID(id) : nullptr;
    if (o != nullptr && o->GetType() == _movement_object && !IsLocalAvatar(id)) {
        static_cast<MovementObject*>(o)->ApplyNetworkCharacter(path);
    }
    if (IsHosting()) {
        pending_avatar_characters.push_back(std::make_pair(character, path));  // sent on from LateUpdate
    }
}

static bool ParseColor(const string& s, vec3* c) {
    float r = 0.0f, g = 0.0f, b = 0.0f;
    if (sscanf(s.c_str(), "%f %f %f", &r, &g, &b) != 3) {
        return false;
    }
    *c = vec3(std::max(0.0f, std::min(1.0f, r)), std::max(0.0f, std::min(1.0f, g)), std::max(0.0f, std::min(1.0f, b)));
    return true;
}

void Online::ApplyOwnCharacter(SceneGraph* sg) {
    // Ours: on a client its player state's (a joined character isn't in
    // GetLocalAvatarIDs there), on the host the first local one
    vector<ObjectID> mine = IsHosting() ? GetLocalAvatarIDs() : vector<ObjectID>(1, GetOwnPlayerState().object_id);
    if (sg == nullptr || !host_started_level || mine.empty() || mine[0] < 0) {
        return;
    }
    Object* o = sg->GetObjectFromID(mine[0]);
    if (o == nullptr || o->GetType() != _movement_object) {
        return;
    }
    MovementObject* mo = static_cast<MovementObject*>(o);
    string key = level_name + "|" + to_string((int)mine[0]);
    if (mo->network_proxy || key == own_character_applied) {
        return;
    }
    own_character_applied = key;

    // The character (stock aschar.as SwitchCharacter: drops weapons, rebuilds, random colours)
    string path = config["mp_character"].str();
    if (!path.empty() && path != mo->character_path && FileExists(path, kAnyPath) &&
        mo->HasFunction("void SwitchCharacter(string)")) {
        LOGI << "Overgrowth Online: playing as " << path << std::endl;
        mo->Execute("SwitchCharacter(\"" + path + "\");");
    }

    // The colours, by what each palette channel is (fur, cloth)
    vec3 fur, cloth;
    bool has_fur = ParseColor(config["mp_fur"].str(), &fur);
    bool has_cloth = ParseColor(config["mp_cloth"].str(), &cloth);
    if ((has_fur || has_cloth) && mo->HasFunction("void RandomizeColors()")) {
        string code = "Object@ obj = ReadObjectFromID(this_mo.GetID()); for(int i = 0; i < 4; ++i) { string ch = character_getter.GetChannel(i);";
        if (has_fur) {
            code += " if(ch == \"fur\") { obj.SetPaletteColor(i, vec3(" + to_string(fur.x()) + ", " + to_string(fur.y()) + ", " + to_string(fur.z()) + ")); }";
        }
        if (has_cloth) {
            code += " if(ch == \"cloth\") { obj.SetPaletteColor(i, vec3(" + to_string(cloth.x()) + ", " + to_string(cloth.y()) + ", " + to_string(cloth.z()) + ")); }";
        }
        mo->Execute(code + " }");
    }
}

// ---- Co-op rules (story campaigns) -------------------------------------------

vector<ObjectID> Online::PlayerAvatarIDs() {
    vector<ObjectID> ids;
    for (const PlayerState& player : GetPlayerStates()) {
        if (player.object_id >= 0) {
            ids.push_back(player.object_id);
        }
    }
    return ids;
}

static bool AvatarUp(SceneGraph* sg, ObjectID id) {
    Object* o = sg != nullptr ? sg->GetObjectFromID(id) : nullptr;
    return o != nullptr && o->GetType() == _movement_object &&
           static_cast<MovementObject*>(o)->ASGetIntVar("knocked_out") == 0;  // aschar.as _awake
}

bool Online::AllowScriptReset() {
    if (!IsActive() || !host_started_level) {
        return true;
    }
    if (!IsHosting()) {
        LOGI << "Overgrowth Online: a client does not reset the level, the host does" << std::endl;
        return false;
    }

    // The host's death prompt: not while someone else is still fighting
    SceneGraph* sg = Engine::Instance()->GetSceneGraph();
    vector<ObjectID> mine = GetLocalAvatarIDs();
    if (!mine.empty() && AvatarUp(sg, mine[0])) {
        return true;  // the host stands: Retry or a script, on purpose
    }
    for (ObjectID id : PlayerAvatarIDs()) {
        if (!IsLocalAvatar(id) && AvatarUp(sg, id)) {
            LOGI << "Overgrowth Online: reset held, player character " << id << " is still up" << std::endl;
            return false;
        }
    }
    return true;
}

bool Online::CoopCampaign() {
    if (coop_campaign_for != level_name + "|" + campaign_id) {
        coop_campaign_for = level_name + "|" + campaign_id;
        coop_campaign = false;
        for (ModInstance* mod : ModLoading::Instance().GetAllMods()) {
            if (!mod->IsActive()) {
                continue;
            }
            for (const ModInstance::Campaign& c : mod->campaigns) {
                if (!campaign_id.empty() && campaign_id == c.id.str()) {
                    coop_campaign = string(c.main_script.str()).find("linear_campaign") != string::npos;
                }
            }
        }
    }
    return coop_campaign;
}

void Online::CoopUpdate(SceneGraph* sg) {
    TrackSafeGround(sg);
    CoopCutsceneUpdate(sg);

    // Revives: a client's own character, as the host asked
    // (one a teammate still drags comes back first: wait for it, a while)
    double now_ms = OnlineClockMs();
    vector<PendingRevive> waiting;
    for (const auto& r : pending_revives) {
        Object* o = sg != nullptr ? sg->GetObjectFromID(r.id) : nullptr;
        if (o == nullptr || o->GetType() != _movement_object) {
            continue;
        }
        if (coop_cutscene && OwnPlayerCharacter(r.id)) {
            waiting.push_back(r);  // after the cutscene (held still in it; its end places us)
        } else if (IsLocalAvatar(r.id)) {
            MovementObject* mo = static_cast<MovementObject*>(o);
            mo->SetNetworkProxy(false);  // (now: its update would only do it later)
            ReviveAvatar(mo, r.pos, r.facing);
        } else if (OwnPlayerCharacter(r.id) && now_ms - r.queued_ms < 3000.0) {
            waiting.push_back(r);
        }
    }
    pending_revives.swap(waiting);

    if (!IsHosting() || !host_started_level || sg == nullptr || sg->level == nullptr) {
        coop_all_down_ms = -1.0;
        return;
    }

    vector<ObjectID> players = PlayerAvatarIDs();

    // Everyone down for 5 s in a story level: the level starts over for all
    if (players.size() < 2 || !CoopCampaign()) {
        coop_all_down_ms = -1.0;
        return;
    }
    for (ObjectID id : players) {
        if (AvatarUp(sg, id)) {
            coop_all_down_ms = -1.0;
            return;
        }
    }
    if (coop_all_down_ms < 0.0) {
        coop_all_down_ms = OnlineClockMs();
    } else if (OnlineClockMs() - coop_all_down_ms > 5000.0) {
        coop_all_down_ms = -1.0;
        LOGI << "Overgrowth Online: every player is down, the level starts over" << std::endl;
        sg->level->Message("reset");
        sg->level->Message("skip_dialogue");
    }
}

// Host, from the level's "coop_checkpoint" message (Level::Message): everyone
// who is down gets up beside the one who reached the checkpoint (else the
// host, else anyone up), on safe ground
void Online::CoopCheckpoint(ObjectID by) {
    SceneGraph* sg = Engine::Instance()->GetSceneGraph();
    if (!IsHosting() || !host_started_level || sg == nullptr) {
        return;
    }
    vector<ObjectID> players = PlayerAvatarIDs();
    vector<ObjectID> anchors;
    if (by >= 0 && AvatarUp(sg, by)) {
        anchors.push_back(by);
    }
    vector<ObjectID> mine = GetLocalAvatarIDs();
    if (!mine.empty() && AvatarUp(sg, mine[0])) {
        anchors.push_back(mine[0]);
    }
    for (ObjectID id : players) {
        if (AvatarUp(sg, id)) {
            anchors.push_back(id);
        }
    }
    int k = 0;
    for (ObjectID id : players) {
        Object* o = sg->GetObjectFromID(id);
        if (anchors.empty() || o == nullptr || o->GetType() != _movement_object || AvatarUp(sg, id)) {
            continue;
        }
        ++k;
        vec3 pos, facing;
        ObjectID beside = -1;
        for (ObjectID a : anchors) {
            if (SpotBeside(this, sg, a, k, &pos, &facing)) {
                beside = a;
                break;
            }
        }
        if (beside < 0) {
            LOGW << "Overgrowth Online: checkpoint, no safe ground for player character " << id << " (stays down)" << std::endl;
            continue;
        }
        LOGI << "Overgrowth Online: checkpoint, player character " << id << " back up beside " << beside << std::endl;
        if (IsLocalAvatar(id)) {
            ReviveAvatar(static_cast<MovementObject*>(o), pos, facing);
        } else {
            Send<OnlineMessages::SendLevelMessage>("ohg revive " + to_string((int)GetOriginalID(id)) + " " +
                                                   to_string(pos.x()) + " " + to_string(pos.y()) + " " + to_string(pos.z()) + " " +
                                                   to_string(facing.x()) + " " + to_string(facing.z()));
        }
    }
}

// Every machine. A dialogue is the host's character's scene: the guests'
// characters aren't in it (hidden here; level.as holds a client's own one
// still). When it ends, each guest stands up beside the host
void Online::CoopCutsceneUpdate(SceneGraph* sg) {
    bool cutscene = host_started_level && sg != nullptr && sg->level != nullptr && sg->level->DialogueCameraControl();
    ObjectID host = HostAvatarID();

    if (cutscene && host >= 0) {
        for (ObjectID id : PlayerAvatarIDs()) {
            Object* o = id != host ? sg->GetObjectFromID(id) : nullptr;
            if (o == nullptr || o->GetType() != _movement_object) {
                continue;
            }
            static_cast<MovementObject*>(o)->visible = false;  // (every frame: aschar.as Reset shows it again)
            if (std::find(coop_hidden.begin(), coop_hidden.end(), id) == coop_hidden.end()) {
                coop_hidden.push_back(id);
                LOGI << "Overgrowth Online: cutscene, player character " << id << " hidden" << std::endl;
            }
        }
    }
    if (cutscene == coop_cutscene) {
        return;
    }
    coop_cutscene = cutscene;
    if (cutscene) {
        return;
    }

    for (ObjectID id : coop_hidden) {
        Object* o = sg != nullptr ? sg->GetObjectFromID(id) : nullptr;
        if (o != nullptr && o->GetType() == _movement_object) {
            static_cast<MovementObject*>(o)->visible = true;
        }
    }
    coop_hidden.clear();

    // Our own character, a guest's: beside the host, facing its way (a revive
    // that came during the cutscene is this now)
    ObjectID me = IsHosting() ? -1 : GetOwnPlayerState().object_id;
    pending_revives.erase(std::remove_if(pending_revives.begin(), pending_revives.end(),
                                         [&](const PendingRevive& r) { return r.id == me; }),
                          pending_revives.end());
    Object* o = me >= 0 && me != host && IsLocalAvatar(me) ? sg->GetObjectFromID(me) : nullptr;
    vec3 pos, facing;
    if (o != nullptr && o->GetType() == _movement_object && SpotBeside(this, sg, host, std::max(1, GuestIndex(me)), &pos, &facing)) {
        LOGI << "Overgrowth Online: cutscene over, player character " << me << " beside the host's " << host << std::endl;
        ReviveAvatar(static_cast<MovementObject*>(o), pos, facing);
    }
}

// Every machine, once a frame after the game updates: while our own character
// is down and another player's is up, the camera looks at theirs. The
// character script still turns it (mouse look); only its centre moves, set
// twice so the smoothing goes from our last centre to this one
void Online::CoopSpectateUpdate(SceneGraph* sg) {
    ObjectID me = IsHosting() ? HostAvatarID() : GetOwnPlayerState().object_id;
    Object* mine = sg != nullptr && me >= 0 ? sg->GetObjectFromID(me) : nullptr;
    bool down = mine != nullptr && mine->GetType() == _movement_object &&
                static_cast<MovementObject*>(mine)->ASGetIntVar("knocked_out") != 0;

    vector<ObjectID> up;
    if (down && host_started_level && !coop_cutscene) {
        for (ObjectID id : PlayerAvatarIDs()) {
            if (id != me && AvatarUp(sg, id)) {
                up.push_back(id);
            }
        }
    }
    if (up.empty()) {
        if (spectate_target != -1) {
            LOGI << "Overgrowth Online: spectating stopped" << std::endl;
        }
        spectate_target = -1;
        spectate_name.clear();
        spectate_next = false;
        return;
    }

    // Whose: the same one while they stay up; attack: the next; else the nearest
    auto at = std::find(up.begin(), up.end(), spectate_target);
    ObjectID target = spectate_target;
    if (at == up.end()) {
        float best = 1e30f;
        for (ObjectID id : up) {
            float d = distance_squared(static_cast<MovementObject*>(sg->GetObjectFromID(id))->position,
                                       static_cast<MovementObject*>(mine)->position);
            if (d < best) {
                best = d;
                target = id;
            }
        }
    } else if (spectate_next) {
        target = *(++at == up.end() ? up.begin() : at);
    }
    spectate_next = false;

    MovementObject* mo = static_cast<MovementObject*>(sg->GetObjectFromID(target));
    Camera* cam = ActiveCameras::GetCamera(0);  // (ours: a host's other cameras are its players' virtual ones)
    if (cam == nullptr || mo == nullptr) {
        return;
    }
    vec3 center = mo->position + vec3(0.0f, 0.6f, 0.0f);  // (aschar.as: the camera centre over a standing character)
    vec3 facing = cam->GetFacing();
    float dist = std::max(1.5f, std::min(6.0f, cam->GetChaseDistance()));
    vec3 hit;
    if (sg->bullet_world_ != nullptr && sg->bullet_world_->CheckRayCollision(center, center - facing * dist, &hit) != nullptr) {
        dist = std::max(0.4f, distance(center, hit) - 0.25f);  // (a wall behind them: closer)
    }

    if (target != spectate_target) {
        spectate_target = target;
        spectate_name.clear();
        for (const auto& it : online_session->player_states) {
            if (it.second.object_id == target) {
                spectate_name = it.second.playername;
            }
        }
        LOGI << "Overgrowth Online: spectating " << spectate_name << " (character " << target << ")" << std::endl;
        spectate_center = center;  // (a cut, not a glide)
        spectate_distance = dist;
    }
    cam->SetPos(spectate_center);
    cam->SetPos(center);
    cam->SetDistance(spectate_distance);
    cam->SetDistance(dist);
    spectate_center = center;
    spectate_distance = dist;
}

// Client: "ohg revive <common id> x y z"; runs in LateUpdate
void Online::CoopReviveRequest(const string& msg) {
    int cid = 0;
    float x = 0.0f, y = 0.0f, z = 0.0f, fx = 0.0f, fz = 0.0f;
    int n = sscanf(msg.c_str(), "ohg revive %d %f %f %f %f %f", &cid, &x, &y, &z, &fx, &fz);
    if (n == 4 || n == 6) {
        pending_revives.push_back({GetObjectID(cid), vec3(x, y, z), vec3(fx, 0.0f, fz), OnlineClockMs()});
    }
}

// Our player's character (by the player states: a teammate dragging it runs it for now)
bool Online::OwnPlayerCharacter(ObjectID id) const {
    if (online_session == nullptr) {
        return true;
    }
    for (const auto& it : online_session->player_states) {
        if (it.second.object_id == id) {
            return it.first == online_session->local_player_id;
        }
    }
    return false;
}

static bool OwnPlayerCharacterOfAnyone(ObjectID id) {
    Online* online = Online::Instance();
    for (const PlayerState& p : online->GetPlayerStates()) {
        if (p.object_id == id) {
            return true;
        }
    }
    return false;
}

// Host: a player's character up at `pos` (facing `facing`, if any) on the
// machine whose player it is (online arenas: round starts; co-op revives)
void Online::PlaceAvatar(ObjectID id, const vec3& pos, const vec3& facing) {
    if (!IsHosting()) {
        return;
    }
    if (OwnPlayerCharacter(id) || (IsLocalAvatar(id) && !OwnPlayerCharacterOfAnyone(id))) {
        pending_revives.push_back({id, pos, facing, OnlineClockMs()});  // (CoopUpdate, outside the calling script)
    } else {
        Send<OnlineMessages::SendLevelMessage>("ohg revive " + to_string((int)GetOriginalID(id)) + " " + to_string(pos.x()) + " " +
                                               to_string(pos.y()) + " " + to_string(pos.z()) + " " + to_string(facing.x()) + " " +
                                               to_string(facing.z()));
    }
}

void Online::ReviveAvatar(MovementObject* mo, const vec3& pos, const vec3& facing) {
    // Stock calls: full health (tutorial.as revive_all), dialogue control on
    // and off stands the body up, then the spot
    mo->Execute("Recover();");
    mo->ReceiveScriptMessage("set_dialogue_control true");
    mo->ReceiveScriptMessage("set_dialogue_control false");
    mo->Execute("this_mo.position = vec3(" + to_string(pos.x()) + ", " + to_string(pos.y()) + ", " + to_string(pos.z()) +
                "); this_mo.velocity = vec3(0.0f);");
    if (length_squared(facing) > 0.01f) {  // turned that way, the camera behind
        vec3 f = normalize(facing);
        mo->Execute("this_mo.SetRotationFromFacing(vec3(" + to_string(f.x()) + ", 0.0f, " + to_string(f.z()) +
                    ")); SetCameraFromFacing(); SetOnGround(true); FixDiscontinuity();");
    }
}

// Client: a level script's request to the host ("ohr ...")
bool Online::SendLevelRequest(const string& msg) {
    if (!IsClient() || msg.compare(0, 4, "ohr ") != 0) {
        return false;
    }
    Send<OnlineMessages::SendLevelMessage>(TranslateIDs(msg, true));
    return true;
}

void Online::UpdateObjects(SceneGraph* scenegraph_) {
    uint32_t wall_ticks = game_timer.GetWallTicks();
    uint32_t tick_delta = wall_ticks - last_update_wall_ticks;
    uint32_t quarter_tick_delta = wall_ticks - last_quarter_update_wall_ticks;

    bool is_quarter_tick = quarter_tick_delta >= tick_period / 4;
    bool is_tick = tick_delta >= tick_period;

    StreamCharacters(scenegraph_);

    // Test bridge tracking (only while a test asks for it)
    if (!track_samples.empty()) {
        double now = OnlineClockMs();

        for (auto& it : track_samples) {
            Object* o = scenegraph_->GetObjectFromID(it.first);
            if (o == nullptr || o->GetType() != _movement_object || it.second.size() >= 20000) {
                continue;
            }

            MovementObject* mo = static_cast<MovementObject*>(o);
            TrackSample s;
            s.clock_ms = now;
            s.render_src_ms = mo->network_proxy ? mo->net_buffer.RenderSrcMs() : now;
            s.position = mo->position;
            s.bones = vec3(0.0f);
            int n = 0;

            for (const PhysicsBone& bone : mo->rigged_object()->skeleton_.physics_bones) {
                if (bone.bullet_object != nullptr) {
                    s.bones += bone.bullet_object->transform.GetTranslationPart();
                    ++n;
                }
            }

            if (n > 0) {
                s.bones /= (float)n;
            }
            it.second.push_back(s);
        }
    }

    if (IsHosting()) {
        if (is_tick) {
            for (Object* o : scenegraph_->objects_) {
                switch (o->GetType()) {
                    case _env_object:
                        // (gear on a character follows its bones everywhere)
                        if (o->online_transform_dirty && (o->parent == nullptr || o->parent->GetType() != _movement_object)) {
                            Send<OnlineMessages::EnvObjectUpdate>(static_cast<EnvObject*>(o));
                        }
                        break;

                    case _item_object: {
                        ItemObject* item = static_cast<ItemObject*>(o);
                        if (!item->IsHeld() && item->StuckInWhom() == -1) {  // those follow their character everywhere
                            Send<OnlineMessages::ItemUpdate>(item);
                        }
                        break;
                    }
                }
                o->online_transform_dirty = false;
            }
        }
    } else if (IsClient()) {
        SendPendingItemLaunches(scenegraph_);

        // Ownership: the host does not steer our character by our camera
        // any more; its scripts get a coarse view of it (5 Hz)
        static double last_camera_ms = -1e9;
        if (OnlineClockMs() - last_camera_ms >= 200.0) {
            last_camera_ms = OnlineClockMs();
            SendPlayerCameraState();
        }
        // Send input information every frame, we want to make sure we get the latest.
        SendPlayerInputState();
    }

    if (is_tick) {
        last_update_wall_ticks += tick_delta;
    }

    if (is_quarter_tick) {
        last_quarter_update_wall_ticks += quarter_tick_delta;
    }
}

bool Online::HasFreeAvatars() const {
    return !online_session->free_avatars.empty();
}

uint32_t Online::GetNumberOfFreeAvatars() const {
    return online_session->free_avatars.size();
}

uint8_t Online::GetPlayerLimit() {
    if (Engine::Instance()->GetSceneGraph() != nullptr && Engine::Instance()->GetSceneGraph()->level != nullptr) {
        if (Engine::Instance()->GetSceneGraph()->level->script_params().HasParam("Player Limit")) {
            return Engine::Instance()->GetSceneGraph()->level->script_params().ASGetInt("Player Limit");
        }
        return 8;  // Default to 8 players (arbitrary limit)
    }
    return 1;  // No level, no players
}

bool Online::IsLobbyFull() {
    return GetPlayerLimit() <= GetPlayerCount();
}

void Online::AddFreeAvatarsIds() {
    Engine::Instance()->GetAvatarIds(online_session->free_avatars);
}

void Online::RemoveAllAvatarIds() {
    online_session->free_avatars.clear();
    online_session->taken_avatars.clear();
}

void Online::RemoveFreeAvatarId(uint32_t id) {
    vector<ObjectID>::iterator it = find(online_session->free_avatars.begin(), online_session->free_avatars.end(), id);

    if (it != online_session->free_avatars.end()) {
        online_session->free_avatars.erase(it);
    }
}

void Online::AddFreeAvatarId(uint32_t id) {
    vector<ObjectID>::iterator it = find(online_session->taken_avatars.begin(), online_session->taken_avatars.end(), id);

    if (it == online_session->taken_avatars.end()) {
        online_session->free_avatars.push_back(id);
    }
}

ObjectID Online::GetFreeAvatarID() {
    ObjectID avatarID = -1;
    if (!online_session->free_avatars.empty()) {
        auto front = online_session->free_avatars.begin();
        avatarID = *front;
        online_session->taken_avatars.push_back(avatarID);
        online_session->free_avatars.erase(front);
    }

    return avatarID;
}

void Online::OnConnectionChange(NetFrameworkConnectionStatusChanged* data) {
    switch (data->conn_info.connection_state) {
        case NetFrameworkConnectionState::Connecting: {
            if (IsHosting()) {
                bool accept_connection = true;
                if (accept_connection) {
                    int accept_connection_result;
                    bool is_ok = net.AcceptConnection(data->conn_id, &accept_connection_result);

                    if (is_ok) {
                        AddLocalChatMessage(string("Accepted connection: ") + to_string(data->conn_id));
                    } else {
                        string error_string = to_string(data->conn_id) + " failed to accept connection: (" + net.GetResultString(accept_connection_result) + ")";
                        AddLocalChatMessage(error_string);
                        LOGW << error_string << endl;
                    }
                } else {
                    net.CloseConnection(data->conn_id, ConnectionClosedReason::UNSPECIFIED);
                }
            } else {
                // We come here when we have an outward connection from the client.
            }
            break;
        }

        case NetFrameworkConnectionState::ClosedByPeer: {
            ConnectionClosedReason reason = data->conn_info.end_reason;
            CloseConnection(data->conn_id, reason);
            if (IsClient()) {
                QueueStopMultiplayer();  // the main loop stops it (we may be on the network thread)

                if (ConnectionClosedReasonUtil::IsUnusualReason(reason)) {
                    string error = ConnectionClosedReasonUtil::GetErrorMessage(reason);
                    LOGI << "Connection closed by the host: " << error << std::endl;  // (what the player was told)
                    Engine::Instance()->QueueErrorMessage("Connection Closed", error);
                }
            }
            break;
        }

        case NetFrameworkConnectionState::ProblemDetectedLocally: {
            ConnectionClosedReason reason = data->conn_info.end_reason;
            CloseConnection(data->conn_id, reason);

            if (IsClient()) {
                // Lost the host: leave the session (it used to stay half open)
                QueueStopMultiplayer();

                if (ConnectionClosedReasonUtil::IsUnusualReason(reason)) {
                    string error = ConnectionClosedReasonUtil::GetErrorMessage(reason);
                    Engine::Instance()->QueueErrorMessage("Connection Lost", error);
                }
            }

            break;
        }

        case NetFrameworkConnectionState::FindingRoute: {
            // packages will be dropped in here
            break;
        }

        case NetFrameworkConnectionState::Connected: {
            AddPeer(data);
            break;
        }

        default: {
            LOGE << "Online::OnConnectionChange: Connection has unhandeld state" << endl;
            LOGE << data->conn_info.connection_state << endl;
        }
    }
}

void Online::StopListening() {
    for (auto& socket : listen_sockets) {
        net.CloseListenSocket(socket);
    }

    listen_sockets.clear();
}

bool Online::NetFrameworkHasFriendInviteOverlay() const {
    return net.HasFriendInviteOverlay();
}

void Online::ActivateGameOverlayInviteDialog() {
    if (!IsActive()) {
        LOGW << "Attempted to show Game Overlay Invite dialogue while offline!" << std::endl;
        return;
    }

#if ENABLE_STEAMWORKS
    if (!OpenToFriends(true)) {
        LOGE << "Couldn't open invite overlay page" << endl;
    }
#else
    LOGW << "Tried opening a game invite overlay, but we haven't compiled in any support for one." << endl;
#endif
}

// Overgrowth Online (host, Steam): friends reach us through Steam (P2P), the
// lobby carries the invite and "Join Game"; open from the level's start, so
// friends can join without one (Join a friend)
bool Online::OpenToFriends(bool overlay) {
#if ENABLE_STEAMWORKS
    SteamworksMatchmaking* matchmaking = Steamworks::Instance()->GetMatchmaking();
    if (matchmaking == nullptr || !IsHosting()) {
        return false;
    }
    if (!p2p_sockets_active) {
        NetListenSocketID listen = net.CreateListenSocketP2P();
        if (listen != 0) {
            listen_sockets.insert(listen);
            p2p_sockets_active = true;
        }
    }
    matchmaking->OpenToFriends(overlay);
    return true;
#else
    return false;
#endif
}

// Overgrowth Online (host, Steam): what friends see in Join a friend: the
// campaign and level, the player count, our mods (they must match)
void Online::UpdateFriendPresence(SceneGraph* scenegraph) {
#if ENABLE_STEAMWORKS
    SteamworksMatchmaking* matchmaking = Steamworks::Instance()->GetMatchmaking();
    Engine* engine = Engine::Instance();
    ScriptableCampaign* sc = engine->GetCurrentCampaign();
    string game;

    if (matchmaking == nullptr) {
        return;
    }
    if (sc != nullptr) {
        string id = sc->GetCampaignID();
        string level_id = engine->GetCurrentLevelID();
        ModInstance::Campaign campaign = ModLoading::Instance().GetCampaign(id);

        game = campaign.title.str();
        for (const ModInstance::Level& level : campaign.levels) {
            if (level.id.str() == level_id && !level.title.str().empty()) {
                game += (game.empty() ? "" : ": ") + level.title.str();
            }
        }
    }
    if (game.empty() && scenegraph != nullptr) {
        game = scenegraph->level_name_;
    }
    matchmaking->SetHostPresence(game, GetPlayerCount(), OnlineUtility::GetActiveModsDigest());
#endif
}

// Overgrowth Online: Join a friend works (Steam runs, we are signed in)
bool Online::SteamFriendsAvailable() const {
#if ENABLE_STEAMWORKS
    return Steamworks::Instance()->IsConnected() && Steamworks::Instance()->GetMatchmaking() != nullptr;
#else
    return false;
#endif
}

// Overgrowth Online: Join a friend (see SteamworksMatchmaking::FriendGames)
string Online::FriendGames() {
#if ENABLE_STEAMWORKS
    SteamworksMatchmaking* matchmaking = Steamworks::Instance()->GetMatchmaking();
    if (matchmaking != nullptr) {
        return matchmaking->FriendGames(OnlineUtility::GetActiveModsDigest());
    }
#endif
    return "";
}

// Overgrowth Online: the server browser (public games of Overgrowth Online)
void Online::RequestPublicGames() {
#if ENABLE_STEAMWORKS
    SteamworksMatchmaking* matchmaking = Steamworks::Instance()->GetMatchmaking();
    if (matchmaking != nullptr) {
        matchmaking->RequestPublicGames();
    }
#endif
}

string Online::PublicGames() {
#if ENABLE_STEAMWORKS
    SteamworksMatchmaking* matchmaking = Steamworks::Instance()->GetMatchmaking();
    if (matchmaking != nullptr) {
        return matchmaking->PublicGames(OnlineUtility::GetActiveModsDigest(), OnlineUtility::GetOvergrowthOnlineVersion());
    }
#endif
    return "none";
}

bool Online::JoinPublicGame(const string& lobby) {
#if ENABLE_STEAMWORKS
    SteamworksMatchmaking* matchmaking = Steamworks::Instance()->GetMatchmaking();
    if (matchmaking != nullptr && !IsActive()) {
        matchmaking->JoinLobby(lobby);
        return true;
    }
#endif
    return false;
}

bool Online::JoinFriend(const string& steam_id) {
#if ENABLE_STEAMWORKS
    SteamworksMatchmaking* matchmaking = Steamworks::Instance()->GetMatchmaking();
    if (matchmaking != nullptr && !IsActive()) {
        return matchmaking->JoinFriend(steam_id);
    }
#endif
    return false;
}

void Online::CreateListenSocketIP(const string& local_address) {
    NetListenSocketID listen_socket = net.CreateListenSocketIP(local_address);
    listen_sockets.insert(listen_socket);
}

void Online::ConnectP2P(uint64_t steam_id) {
    if (!IsActive()) {
        StartMultiplayer(MultiplayerMode::Client);

        NetConnectionID conn_id;

        if (net.ConnectP2P(steam_id, &conn_id) == false) {
            StopMultiplayer();
        } else {
            pending_connection = conn_id;  // (closed if the player cancels: StopMultiplayer)
            has_pending_connection = true;
        }
    }
}

void Online::ConnectByIPAddress(const string& address) {
    if (!IsActive()) {
        StartMultiplayer(MultiplayerMode::Client);

        NetConnectionID conn_id;

        if (net.ConnectByIPAddress(address, &conn_id) == false) {
            StopMultiplayer();
        } else {
            pending_connection = conn_id;  // (closed if the player cancels: StopMultiplayer)
            has_pending_connection = true;
        }
    }
}

uint8_t Online::GetBindID(string binding_name) {
    return online_session->binding_id_map_[binding_name];
}

void Online::AssignBindID(string binding_name) {
    if (online_session->binding_id_map_.find(binding_name) == online_session->binding_id_map_.end()) {
        online_session->binding_id_map_[binding_name] = ++online_session->binding_id_map_counter_;
        online_session->binding_name_map_[online_session->binding_id_map_[binding_name]] = binding_name;
    } else {
        LOGW << "Binding \"" << binding_name << "\" has been assigned multiple times!" << std::endl;
    }
}

void Online::NetworkDataThread() {
    while (IsActive()) {
        uint64_t sent_before = net_msgs_out.load();
        net.Update();
        int received = CheckNewMessages();
        SendMessages();
        FlushDelayedSends();

        if (received == 0 && net_msgs_out.load() == sent_before) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        message_handler.FreeUnreferencedMessagesOnePass();
    }
}

void Online::ClearOnlineSession() {
    delete online_session;
    online_session = new OnlineSession();
}

void Online::SendMessages() {
    SendMessageObjects();
    if (IsHosting()) {
        SendStateMessages();
    }
}

void Online::SendMessageObjects() {
    online_session->connected_peers_lock.lock();

    online_session->outgoing_messages_lock.lock();

    // First, send persistent packages, if we see there's a new client who isn't up to date.
    if (host_started_level && IsHosting()) {
        for (auto& peer : online_session->connected_peers) {
            if (online_session->client_connection_manager.IsClientLoaded(peer.peer_id) && online_session->client_connection_manager.HasClientGottenPersistentQueue(peer.peer_id) == false) {
                for (auto& persistent_outgoing_message : online_session->persistent_outgoing_messages) {
                    OnlineMessageBase* omb = static_cast<OnlineMessageBase*>(message_handler.GetMessageData(persistent_outgoing_message));

                    if (omb != nullptr) {
                        PackageHeader package_header;

                        package_header.package_type = PackageHeader::Type::MESSAGE_OBJECT;
                        package_header.message_ref = persistent_outgoing_message;

                        binn* l = package_header.Serialize();

                        size_t compressed_size = CompressData(compressed_serialization_buffer, binn_ptr(l), binn_size(l));

                        binn_free(l);

                        SendMessageToConnection(peer.conn_id, compressed_serialization_buffer.data(), compressed_size, true, false);
                    } else {
                        LOGE << "Package data pointer was null, this is unexpected and odd." << endl;
                    }
                }
                online_session->client_connection_manager.SetClientGottenPersistentQueue(peer.peer_id);
            }
        }
    }

    // Send the standard message queues. Store away persistent messages for future client peers if we are host.
    for (int i = 0; i < online_session->outgoing_messages.size(); i++) {
        OnlineSession::OutgoingMessage& outgoing_message = online_session->outgoing_messages[i];
        OnlineMessageBase* omb = static_cast<OnlineMessageBase*>(message_handler.GetMessageData(outgoing_message.message));

        if (omb != nullptr) {
            PackageHeader package_header;

            package_header.package_type = PackageHeader::Type::MESSAGE_OBJECT;
            package_header.message_ref = outgoing_message.message;

            binn* l = package_header.Serialize();

            size_t compressed_size = CompressData(compressed_serialization_buffer, binn_ptr(l), binn_size(l));

            binn_free(l);

            for (auto& peer : online_session->connected_peers) {
                bool send_message = true;

                if (outgoing_message.broadcast == false && outgoing_message.target != peer.conn_id) {
                    send_message = false;
                    // Don't send packags to client peers who have recently connected, but hasn't loaded the level, or finished getting the persistent queue yet.
                } else if (IsHosting() && omb->cat != OnlineMessageCategory::TRANSIENT && online_session->client_connection_manager.HasClientGottenPersistentQueue(peer.peer_id) == false) {
                    send_message = false;
                }

                if (send_message) {
                    SendMessageToConnection(peer.conn_id, compressed_serialization_buffer.data(), compressed_size, omb->reliable_delivery, i + 1 == online_session->outgoing_messages.size());
                }
            }

            if (IsHosting()) {
                if (omb->cat == OnlineMessageCategory::LEVEL_PERSISTENT) {
                    online_session->persistent_outgoing_messages.push_back(outgoing_message.message);
                }
            }
        } else {
            LOGE << "Package data pointer was null, this is unexpected and odd." << endl;
        }
    }

    online_session->outgoing_messages.clear();

    online_session->outgoing_messages_lock.unlock();

    online_session->connected_peers_lock.unlock();
}

// Main thread only (LateUpdate): CloseConnectionImmediate removes the player's
// character from the scene (scripts, GL resources). It used to run on the
// network thread, which crashed the host when a player dropped.
void Online::HandleConnectionsToBeClosed() {
    vector<ConnectionToBeClosed> todo;
    {
        lock_guard<mutex> guard(close_queue_lock);

        while (online_session->connections_waiting_to_be_closed.empty() == false) {
            todo.push_back(online_session->connections_waiting_to_be_closed.front());
            online_session->connections_waiting_to_be_closed.pop();
        }
    }

    for (const ConnectionToBeClosed& connection : todo) {
        CloseConnectionImmediate(connection.conn_id, connection.reason);
    }
}

static double NetSimNowMs() {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

void Online::SendMessageToConnection(NetConnectionID conn_id, char* buffer, uint32_t bytes, bool reliable, bool flush) {
    int latency = net_sim_latency_ms.load() + net_sim_conn_latency_ms[conn_id].load();
    int jitter = net_sim_jitter_ms.load() + net_sim_conn_jitter_ms[conn_id].load();
    float loss = net_sim_loss.load();

    if (latency <= 0 && jitter <= 0 && loss <= 0.0f && net_sim_queue.empty()) {
        net_bytes_out += bytes;
        ++net_msgs_out;
        net.SendMessageToConnection(conn_id, buffer, bytes, reliable, flush);
        return;
    }

    if (!reliable && loss > 0.0f && (float)rand() / (float)RAND_MAX < loss) {
        ++net_sim_dropped;
        return;
    }

    double release = NetSimNowMs() + latency + (jitter > 0 ? rand() % (jitter + 1) : 0);

    for (auto it = net_sim_queue.rbegin(); it != net_sim_queue.rend(); ++it) {
        if (it->conn_id == conn_id) {
            release = std::max(release, it->release_ms);  // keep order per connection
            break;
        }
    }

    net_sim_queue.push_back({conn_id, std::vector<char>(buffer, buffer + bytes), reliable, release});
}

void Online::FlushDelayedSends() {
    if (net_sim_queue.empty()) {
        return;
    }

    double now = NetSimNowMs();
    lock_guard<mutex> guard(online_session->connected_peers_lock);

    for (auto it = net_sim_queue.begin(); it != net_sim_queue.end();) {
        if (it->release_ms > now) {
            ++it;
            continue;
        }

        bool connected = false;

        for (auto& peer : online_session->connected_peers) {
            connected = connected || peer.conn_id == it->conn_id;
        }

        if (connected) {
            net_bytes_out += it->data.size();
            ++net_msgs_out;
            net.SendMessageToConnection(it->conn_id, it->data.data(), (uint32_t)it->data.size(), it->reliable, true);
        }

        it = net_sim_queue.erase(it);
    }
}

void Online::SendStateMessages() {
    lock_guard<mutex> guard(online_session->connected_peers_lock);
    for (auto& conn : online_session->connected_peers) {
        if (conn.should_send_state_messages) {
            {
                lock_guard<mutex> guard(online_session->state_packages_lock);
                for (auto& package : online_session->state_packages) {
                    SendTo<OnlineMessages::AngelscriptData>(conn.conn_id, package.first, package.second.data, true);
                }
            }

            conn.should_send_state_messages = false;
        }
    }
}

int Online::CheckNewMessages() {
    int received = 0;
    // we are locking over a big segment and I/O, but it's not dangerous
    // calls that could be waiting are not time sensitive.
    lock_guard<mutex> guard(online_session->connected_peers_lock);

    // static SteamNetworkingMessage_t ** msg = (SteamNetworkingMessage_t **)malloc(sizeof(SteamNetworkingMessage_t*) * 1);
    NetworkingMessage msg;

    for (auto& it : online_session->connected_peers) {
      for (int pass = 0; pass < 1024; ++pass) {
        int nrOfMessages = net.ReceiveMessageOnConnection(it.conn_id, &msg);

        if (nrOfMessages == -1) {
            LOGE << "Invalid connection handle: " << it.conn_id << endl;
        }

        if (nrOfMessages <= 0) {
            break;
        }

        received += nrOfMessages;

        for (int i = 0; i < nrOfMessages; i++) {
            net_bytes_in += msg.GetSize();
            ++net_msgs_in;
            PackageHeader package_header;
            static vector<char> data;
            DecompressData(data, msg.GetData(), msg.GetSize());
            package_header.Deserialize(data.data());

            if (package_header.package_type == PackageHeader::Type::MESSAGE_OBJECT) {
                online_session->incoming_messages_lock.lock();
                online_session->incoming_messages.push_back({it.peer_id, package_header.message_ref});
                online_session->incoming_messages_lock.unlock();
            } else {
                LOGE << "Unrecognized package type: " << (int)package_header.package_type << endl;
            }
            msg.Release();
        }
      }
    }

    return received;
}

int Online::GetPeerTransportPing(NetConnectionID conn_id) {
    ConnectionStatus status;
    net.GetConnectionStatus(conn_id, &status);
    return status.ms_ping;
}

binn* PackageHeader::Serialize() {
    binn* l = binn_list();

    binn_list_add_int8(l, (int8_t)package_type);
    if (package_type == Type::MESSAGE_OBJECT) {
        binn_list_add_int8(l, (int8_t)message_handler.GetMessageType(message_ref));
        binn* serialized_object = message_handler.Serialize(message_ref);
        if (serialized_object != nullptr) {
            binn_list_add_object(l, serialized_object);
        } else {
            LOGE << "Serialization of PackageHeader message resulted in a nullptr" << endl;
        }
        binn_free(serialized_object);
    }

    return l;
}

void PackageHeader::Deserialize(void* data) {
    // Do a raw read, don't worry about copying the data first.
    binn* l = (binn*)data;

    int pos = 1;

    int8_t v_package_type;
    if (binn_list_get_int8(l, pos++, &v_package_type) == false) {
        LOGE << "Failed to read package type from binn message" << endl;
    }
    this->package_type = (Type)v_package_type;

    if (package_type == Type::MESSAGE_OBJECT) {
        int8_t message_type;
        binn_list_get_int8(l, pos++, &message_type);

        void* serialized_object;
        binn_list_get_object(l, pos++, &serialized_object);

        if (serialized_object != nullptr) {
            message_ref = message_handler.Deserialize((OnlineMessageType)message_type, (binn*)serialized_object);
        } else {
            LOGE << "Got a nullptr when trying to fetch a MESSAGE_OBJECT binn from a PackageHeader" << endl;
        }
    }
}
