//-----------------------------------------------------------------------------
//           Name: threatcheck.as
//      Developer: Wolfire Games LLC
//    Script Type:
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

// Our own character: online every player's is "controlled", ours is not "remote"
int GetPlayerCharacterID() {
    int num = GetNumCharacters();
    for(int i=0; i<num; ++i){
        MovementObject@ char = ReadCharacter(i);
        if(char.controlled && !char.remote){
            return i;
        }
    }
    for(int i=0; i<num; ++i){
        MovementObject@ char = ReadCharacter(i);
        if(char.controlled){
            return i;
        }
    }
    return -1;
}

// Offline, or the only player online. With teammates the host starts the level
// over once every player is down (Online::CoopUpdate), not a script's timer
bool CoopAlone() {
    return !Online_IsActive() || GetControlledCharacterIDs().length() < 2;
}

// Every player's character is down (co-op: the level stops only then)
bool AllPlayersDown() {
    int num = GetNumCharacters();
    bool any = false;
    for(int i=0; i<num; ++i){
        MovementObject@ char = ReadCharacter(i);
        if(char.controlled){
            any = true;
            if(char.GetIntVar("knocked_out") == _awake){
                return false;
            }
        }
    }
    return any;
}

array<int> GetControlledCharacterIDs() {
    array<int> ids;
    int num = GetNumCharacters();
    for(int i=0; i<num; ++i){
        MovementObject@ char = ReadCharacter(i);
        if(char.controlled){
            ids.insertLast(i);
        }
    }
    return ids;
}

int NumActivelyHostileThreats() {
    int player_id = GetPlayerCharacterID();
    if(player_id == -1){
        return -1;
    }
    MovementObject@ player_char = ReadCharacter(player_id);

    int num = GetNumCharacters();
    int num_threats = 0;
    for(int i=0; i<num; ++i){
        MovementObject@ char = ReadCharacter(i);
        if(char.GetIntVar("knocked_out") == _awake && 
           !player_char.OnSameTeam(char) && 
           char.QueryIntFunction("int IsUnaware()") != 1)
        {
            ++num_threats;
        }
    }
    return num_threats;   
}

int ThreatsRemaining() {
    int player_id = GetPlayerCharacterID();
    if(player_id == -1){
        return -1;
    }
    MovementObject@ player_char = ReadCharacter(player_id);

    int num = GetNumCharacters();
    int num_threats = 0;
    for(int i=0; i<num; ++i){
        MovementObject@ char = ReadCharacter(i);
        if(char.GetIntVar("knocked_out") == _awake && !player_char.OnSameTeam(char))
        {
            ++num_threats;
        }
    }
    return num_threats;
}
   
int ThreatsPossible() {
    int player_id = GetPlayerCharacterID();
    if(player_id == -1){
        return -1;
    }
    MovementObject@ player_char = ReadCharacter(player_id);
   
    int num = GetNumCharacters();
    int num_threats = 0;
    for(int i=0; i<num; ++i){
        MovementObject@ char = ReadCharacter(i);
        vec3 target_pos = char.position;
        if(!player_char.OnSameTeam(char))
        {
            ++num_threats;
        }
    }
    return num_threats;
}