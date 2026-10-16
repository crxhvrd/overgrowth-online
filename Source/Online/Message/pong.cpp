//-----------------------------------------------------------------------------
//           Name: pong.cpp
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
#include "pong.h"

#include <Online/online.h>
#include <Online/clock_sync.h>
#include <Internal/timer.h>

extern Timer game_timer;

namespace OnlineMessages {
Pong::Pong(uint32_t ping_id, double ping_send_ms, double reply_ms) : OnlineMessageBase(OnlineMessageCategory::TRANSIENT),
                                                                    ping_id(ping_id),
                                                                    ping_send_ms(ping_send_ms),
                                                                    reply_ms(reply_ms) {
}

binn* Pong::Serialize(void* object) {
    Pong* p = static_cast<Pong*>(object);
    binn* l = binn_object();

    binn_object_set_uint32(l, "id", p->ping_id);
    binn_object_set_double(l, "t0", p->ping_send_ms);
    binn_object_set_double(l, "t1", p->reply_ms);

    return l;
}

void Pong::Deserialize(void* object, binn* l) {
    Pong* p = static_cast<Pong*>(object);

    binn_object_get_uint32(l, "id", &p->ping_id);
    binn_object_get_double(l, "t0", &p->ping_send_ms);
    binn_object_get_double(l, "t1", &p->reply_ms);
}

void Pong::Execute(const OnlineMessageRef& ref, void* object, PeerID from) {
    Pong* p = static_cast<Pong*>(object);
    Online* online = Online::Instance();

    // Runs under connected_peers_lock (CheckPendingMessages)
    double t2 = OnlineClockMs();
    Peer* peer = online->GetPeerFromID(from);
    if (peer != nullptr && p->ping_send_ms > 0.0) {
        peer->clock.AddSample(p->ping_send_ms, p->reply_ms, t2);
        peer->current_ping_delta = (float)(peer->clock.Rtt() / 1000.0);
    }
}

void* Pong::Construct(void* mem) {
    return new (mem) Pong(std::numeric_limits<uint32_t>::max());
}

void Pong::Destroy(void* object) {
    Pong* p = static_cast<Pong*>(object);
    p->~Pong();
}
}  // namespace OnlineMessages
