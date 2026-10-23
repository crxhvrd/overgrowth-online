//-----------------------------------------------------------------------------
//           Name: dialogue_weapon.as
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
string dialogue_key = "q";

void Reset() {
    played = false;
}

void Init() {
    Reset();
}

void SetParameters() {
	params.AddIntCheckbox("Check every Frame", false);
	params.AddIntCheckbox("Play Once", true);
	params.AddIntCheckbox("Play only If dead", false);
	params.AddIntCheckbox("Play for npc's", false);
	params.AddIntCheckbox("Play for player", true);
	params.AddIntCheckbox("Play on button", true);
    params.AddString("Dialogue", "Default text");
}

void Update() {
    Object@ obj = ReadObjectFromID(hotspot.GetID());
    vec3 pos = obj.GetTranslation();
    vec3 scale = obj.GetScale();
	if(params.GetInt("Check every Frame") == 1){
		int num_chars = GetNumCharacters();
		for(int i=0; i<num_chars; ++i){
			MovementObject @mo = ReadCharacter(i);
			
			vec3 mopos = mo.position;
			bool isinside =	   mopos.x > pos.x-scale.x*2.0f
							&& mopos.x < pos.x+scale.x*2.0f
							&& mopos.y > pos.y-scale.y*2.0f
							&& mopos.y < pos.y+scale.y*2.0f
							&& mopos.z > pos.z-scale.z*2.0f
							&& mopos.z < pos.z+scale.z*2.0f;
						
			if(isinside){
				OnEnter(mo);
			}
		}
	}
}

void ReceiveMessage(string msg){
    if(msg == "coop_played"){  // online: the host played it
        played = true;
    }
    if(msg == "coop_request" && (!played || params.GetInt("Play Once") == 0)){  // online, the host: a guest's key
        PlayIt();
    }
}

void PlayIt() {
    level.SendMessage("start_dialogue \""+params.GetString("Dialogue")+"\"");
    played = true;
    if(Online_IsHosting()){
        level.SendMessage("ohl coop_talked "+hotspot.GetID());
    }
}

void HandleEvent(string event, MovementObject @mo){
    if(event == "enter"){
        OnEnter(mo);
    } else if(event == "exit"){
        OnExit(mo);
    }
}

void OnEnter(MovementObject @mo) {
    if(mo.controlled && mo.remote){
        return;  // (online: a teammate's copy; its owner presses the key there)
    }
    if((mo.GetIntVar("knocked_out") > 0 || params.GetInt("Play only If dead") == 0)
		&& (!played || params.GetInt("Play Once") == 0)
		&&( (!mo.controlled && params.GetInt("Play for npc's") == 1)
		|| (mo.controlled && params.GetInt("Play for player") == 1) && GetInputPressed(mo.controller_id, dialogue_key) && params.GetInt("Play on button") == 1)){
		
        if(Online_IsClient()){
            if(mo.controlled){
                level.SendMessage("ohr coop_talk "+hotspot.GetID());  // (the host plays it for everyone)
            }
            return;
        }
        PlayIt();
    }
}

void OnExit(MovementObject @mo) {
}