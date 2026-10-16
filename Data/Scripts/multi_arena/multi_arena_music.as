//-----------------------------------------------------------------------------
//           Name: multi_arena_music.as
//      Developer: Wolfire Games LLC
//    Script Type: Level (include)
//    Description: The arena music (moved out of multi_arena.as unchanged, for
//                 online_arena.as too). Include music_load.as before it.
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

MusicLoad ml("Data/Music/arena.xml");

enum MusicState
{
    PlayerDead,
    Tempo1,
    Tempo2,
    Tempo3,
    Tempo4,
    Tempo5
}

class CountdownTimer
{
    CountdownTimer( float _duration )
    {
        duration = _duration;
        Reset();
    }

    float start_time;
    float duration;
    bool checked = false;

    bool Check()
    {
        if( checked == false )
        {
            if( the_time - start_time > duration )
            {
                checked = true;
                return true;
            }
        } 
        return false;
    }

    void Reset()
    {
        start_time = the_time;
        checked = false;
    }
};


class MusicSegmentHandler
{
    MusicState current_state;
    MusicState last_used_state; 
    
    string current_segment;
    
    CountdownTimer player_last_hurt_countdown(10.0f);
    CountdownTimer aggression_reduction_counter(3.0f);

    int aggression_count;

    MusicSegmentHandler()
    {
        aggression_count = 0;
        current_segment = "";
        current_state = PlayerDead;
        last_used_state = Tempo1;
    }

    void UpdatePlayedSegment()
    {
        if( last_used_state != current_state )
        { 
            bool do_queue = false;
            string segment = "";
            switch( current_state )
            {
                case PlayerDead:
                    //Let segment finish when ending =)
                    do_queue = true;
                    segment = "fight1-ver1";
                    break; 
                case Tempo1:
                    segment = "fight2-ver1";
                    break;
                case Tempo2:
                    segment = "fight3-ver1";//"fight3-ver2"] );
                    break;
                case Tempo3:
                    segment = "fight4-ver1"; //"fight4-ver2"] );
                    break;
                case Tempo4:
                    segment = "fight5-ver1"; //"fight5-ver2"] ); 
                    break;
                case Tempo5:
                    segment = "fight6-ver1";
                    break;
            }

            if( current_segment != segment )
            {
                Log(info, "Setting Segment to " + segment );
                if( do_queue )
                    QueueSegment( segment );
                else
                    PlaySegment( segment );
                current_segment = segment;
            }
            last_used_state = current_state;
        }
    }

    void CheckPlayerHitReduce()
    {
        if( player_last_hurt_countdown.Check() )
        {
            switch( current_state )
            {
                case Tempo2:
                    current_state = Tempo1;
                    break;
                case Tempo3:
                    current_state = Tempo2;
                    break;
                default:
                    break;
            }
        }
    }
    
    void CheckAnyAggressionReduce()
    {
        if( aggression_reduction_counter.Check() )
        {
            if( aggression_count > 0 )
                aggression_count--;
            aggression_reduction_counter.Reset();
        }

        switch( current_state )
        {
            case Tempo2:
                if( aggression_count < 3 )
                {
                    current_state = Tempo1;
                }
                break;
            case Tempo3:
                if( aggression_count < 5 )
                {
                    current_state = Tempo2;
                }
                break;
            case Tempo4:
                if( aggression_count < 7 )
                {
                    current_state = Tempo3;
                }
                break;
            case Tempo5:
                if( aggression_count < 12 )
                {
                    current_state = Tempo4;
                }
                break;
        }
    }

    void Update()
    {
        CheckPlayerHitReduce();
        CheckAnyAggressionReduce();

        UpdatePlayedSegment();
    }

    
    void PlayerDied()
    {
        /*
        switch( current_state )
        {
            case PlayerDead:
                //Nothing;
                break;
            default:
                current_state = PlayerDead;
                break;
        }
        */
    }   

    void PlayerRevived()
    {
        /*
        switch( current_state )
        {
            case PlayerDead:
                current_state = Tempo1;
                break;
            default:
                break;
        }
        */
    }

    void PlayerKilledCharacter()
    {
        Log( info, "PlayerKilledCharacter" );
        switch( current_state )
        {
            case Tempo1:
                current_state = Tempo3;
                if( aggression_count < 6 )
                    aggression_count = 6;
                break;
            case Tempo2:
                current_state = Tempo4;
                if( aggression_count < 8 )
                    aggression_count = 8;
                break;
            case Tempo3:
                current_state = Tempo5;
                if( aggression_count < 13 )
                    aggression_count = 13;
                break;
            case Tempo4:
                current_state = Tempo5;
                if( aggression_count < 13 )
                    aggression_count = 13;
                break;
        }
    }

    void AggressionShown()
    {
        aggression_count++;
        Log( info, "Aggro:" + aggression_count );
        aggression_reduction_counter.Reset();

        switch( current_state )
        {
            case Tempo1:
                if( aggression_count >= 3 )
                {
                    current_state = Tempo2;
                }
                break;
            case Tempo2:
                if( aggression_count >= 5 )
                {
                    current_state = Tempo3;
                }
                break;
            case Tempo3:
                if( aggression_count >= 7 )
                {
                    current_state = Tempo4;
                }
                break;
        }
    }

    void StartFight()
    {
        switch( current_state )
        {
            case PlayerDead:
                current_state = Tempo1;
                break;
            default:
        }
    }

    void MatchEnded()
    {
        aggression_count /= 2;
        switch( current_state )
        {
            case PlayerDead:
                //Nothing;
                break;
            default:
                current_state = PlayerDead;
                break;
        }
    }
};
