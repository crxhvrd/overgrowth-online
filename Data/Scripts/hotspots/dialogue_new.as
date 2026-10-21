//-----------------------------------------------------------------------------
//           Name: dialogue_new.as
//      Developer: Wolfire Games LLC
//    Script Type: Hotspot
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

bool played;

void Reset() {
    played = false;
}

void Init() {
    Reset();
}

void SetParameters() {
	params.AddIntCheckbox("Play Once", true);
	params.AddIntCheckbox("Play only If dead", false);
	params.AddIntCheckbox("Play for npcs", false);
    params.AddString("Dialogue", "Default text");
}

void HandleEvent(string event, MovementObject @mo){
    if(event == "enter"){
        OnEnter(mo);
    } else if(event == "exit"){
        OnExit(mo);
    }
}

// Another player already in the hotspot (online: it played when they came in)
bool OtherPlayerInside(MovementObject @mo) {
    array<int> ids;
    level.GetCollidingObjects(hotspot.GetID(), ids);
    for(uint i = 0; i < ids.size(); ++i){
        if(ids[i] != mo.GetID() && ObjectExists(ids[i]) && ReadObjectFromID(ids[i]).GetType() == _movement_object &&
           ReadCharacterID(ids[i]).controlled){
            return true;
        }
    }
    return false;
}

void OnEnter(MovementObject @mo) {
    if(Online_IsClient()){
        return;  // (online the host starts dialogues)
    }
    if(Online_IsActive() && params.GetInt("Play Once") == 0 && mo.controlled && OtherPlayerInside(mo)){
        return;
    }
    if((mo.GetIntVar("knocked_out") > 0 || params.GetInt("Play only If dead") == 0)
		&& (!played || params.GetInt("Play Once") == 0)
		&& (mo.controlled || params.GetInt("Play for npcs") == 1)){
		
        level.SendMessage("start_dialogue \""+params.GetString("Dialogue")+"\"");
        played = true;
    }
}

void OnExit(MovementObject @mo) {
}