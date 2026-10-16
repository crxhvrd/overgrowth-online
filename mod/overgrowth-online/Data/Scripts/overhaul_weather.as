//-----------------------------------------------------------------------------
//           Name: overhaul_weather.as
//    Description: Weather and time of day, run by the level hook
//                 (overhaul_menu.as). Off by default: the level's own sky.
//                 Time of day moves the sun along an arc through the level's
//                 own noon direction (a moon at night) and sets its colour,
//                 the ambient light and the sky's tint from the level's own
//                 values. Weather dims the sky and adds rain, snow or a storm
//                 around the camera, with the game's own particles and sounds
//                 (as hotspots/rain.as and snow.as do). The Overgrowth Online
//                 build also swaps the sky itself for one of the game's own
//                 (a sunset, the night, storm clouds: "oo_sky <dome>", answered
//                 "oo_sky_set"); the stock game only tints the level's sky.
//                 Never in the editor, so a level is never saved with it.
//                 Online the host's: "ohl weather <time on> <hour> <time runs>
//                 <day minutes> <kind> <amount>" every 2 s.
//                 Config: ow_time_on, ow_hour, ow_time_runs, ow_day_minutes,
//                 ow_kind, ow_amount.
//-----------------------------------------------------------------------------

enum WeatherKind {
    kWeatherOwn,
    kWeatherCloudy,
    kWeatherRain,
    kWeatherStorm,
    kWeatherSnow,
    kWeatherBlizzard
}

const array<string> kWeatherNames = {"The level's own", "Cloudy", "Rain", "Storm", "Snow", "Blizzard"};

// The level's own sky, read once
bool weather_base_read = false;
vec3 weather_base_sun_pos;
vec3 weather_base_sun_col;
vec3 weather_base_tint;
float weather_base_ambient = 1.0f;
bool weather_applied = false;   // the sky is ours (put back when off)
bool weather_sky_swaps = false; // this build swaps the sky dome ("oo_sky_set" came back)
string weather_dome = "";       // the dome we asked for last ("": none yet, "own": the level's)

// The settings in use (ours, or the host's online)
bool weather_time_on = false;
float weather_hour = 12.0f;
float weather_hour_cfg = -1.0f;  // ow_hour as we last read or wrote it
bool weather_time_runs = false;
float weather_day_minutes = 24.0f;
int weather_kind = kWeatherOwn;
float weather_amount = 1.0f;

float weather_read_next = 0.0f;
float weather_save_next = 0.0f;
float weather_net_next = 0.0f;

// Rain, snow, lightning
float weather_rain_debt = 0.0f;
float weather_snow_debt = 0.0f;
float weather_flake_debt = 0.0f;
vec3 weather_cam_prev;
bool weather_cam_known = false;
int weather_rain_sound = -1;
float weather_rain_gain = 0.0f;
float weather_lightning_at = -100.0f;
float weather_lightning_next = 0.0f;
float weather_thunder_at = -1.0f;
float weather_lightning_far = 0.0f;
vec3 weather_flash_dir;

float WSmooth(float a, float b, float x) {
    float t = max(0.0f, min(1.0f, (x - a) / (b - a)));
    return t * t * (3.0f - 2.0f * t);
}

float WLuma(const vec3 &in c) {
    return c.x * 0.3f + c.y * 0.59f + c.z * 0.11f;
}

// "06:30"
string WeatherClock(float hour) {
    int minutes = int(hour * 60.0f + 0.5f) % (24 * 60);
    int h = minutes / 60;
    int m = minutes % 60;
    return (h < 10 ? "0" : "") + h + ":" + (m < 10 ? "0" : "") + m;
}

string WeatherSummary() {
    string res = weather_time_on ? WeatherClock(weather_hour) + (weather_time_runs ? " (time passes)" : "") : "the level's own time";
    return res + ", " + (weather_kind == kWeatherOwn ? "the level's own weather" : kWeatherNames[weather_kind]);
}

// ---- settings ---------------------------------------------------------------------

void WeatherReadSettings() {
    if(MPClient()) {
        return;  // the host's (WeatherReceive)
    }

    weather_time_on = CfgBool("ow_time_on", false);
    float hour = max(0.0f, min(24.0f, CfgFloat("ow_hour", 12.0f)));

    if(abs(hour - weather_hour_cfg) > 0.0001f) {  // set in the window (or a new level)
        weather_hour = hour;
        weather_hour_cfg = hour;
    }

    weather_time_runs = CfgBool("ow_time_runs", false);
    weather_day_minutes = max(2.0f, min(120.0f, CfgFloat("ow_day_minutes", 24.0f)));
    weather_kind = max(0, min(int(kWeatherNames.length()) - 1, CfgInt("ow_kind", kWeatherOwn)));
    weather_amount = max(0.2f, min(2.0f, CfgFloat("ow_amount", 1.0f)));
}

void WeatherNetSync() {
    if(MPHost() && the_time >= weather_net_next) {
        weather_net_next = the_time + 2.0f;
        MPToClients("weather " + (weather_time_on ? 1 : 0) + " " + weather_hour + " " + (weather_time_runs ? 1 : 0) + " " +
            weather_day_minutes + " " + weather_kind + " " + weather_amount);
    }
}

// Client: the host's weather and time
void WeatherReceive(const string &in message) {
    WeatherSkyReceive(message);

    if(!MPClient() || message.length() < 12 || message.substr(0, 12) != "ohl weather ") {
        return;
    }

    array<string> t = MPTokens(message);

    if(t.length() < 8) {
        return;
    }

    float hour = max(0.0f, min(24.0f, atof(t[3])));
    float apart = abs(hour - weather_hour);
    weather_time_on = atoi(t[2]) != 0;
    weather_time_runs = atoi(t[4]) != 0;
    weather_day_minutes = max(2.0f, min(120.0f, atof(t[5])));
    weather_kind = max(0, min(int(kWeatherNames.length()) - 1, atoi(t[6])));
    weather_amount = max(0.2f, min(2.0f, atof(t[7])));

    // Our clock runs too: take the host's only when it drifted
    if(!weather_time_runs || min(apart, 24.0f - apart) > 0.05f) {
        weather_hour = hour;
    }
}

// ---- every update -------------------------------------------------------------------

void WeatherUpdate(int is_paused) {
    if(!weather_base_read) {
        weather_base_read = true;
        weather_base_sun_pos = GetSunPosition();
        weather_base_sun_col = GetSunColor();
        weather_base_ambient = GetSunAmbient();
        weather_base_tint = GetBaseSkyTint();
    }

    if(the_time >= weather_read_next || is_paused != 0) {
        weather_read_next = the_time + 0.25f;
        WeatherReadSettings();
        WeatherNetSync();
    }

    bool active = (weather_time_on || weather_kind != kWeatherOwn) && !EditorModeActive();

    if(!active) {
        if(weather_applied) {
            WeatherRestore();
        }

        if(weather_dome != "" && weather_dome != "own") {
            WeatherSetDome("own");
        }

        WeatherRainSound(0.0f);
        weather_cam_known = false;
        return;
    }

    if(is_paused == 0 && weather_time_on && weather_time_runs) {
        weather_hour += time_step * 24.0f / (weather_day_minutes * 60.0f);

        if(weather_hour >= 24.0f) {
            weather_hour -= 24.0f;
        }

        // The window shows it, and the next level goes on from it
        if(!MPClient() && the_time >= weather_save_next) {
            weather_save_next = the_time + 1.0f;
            SetConfigValueFloat("ow_hour", weather_hour);
            weather_hour_cfg = weather_hour;
        }
    }

    WeatherSky(is_paused);

    if(is_paused == 0) {
        WeatherFall();
    }

    float gain = weather_kind == kWeatherRain ? 0.35f : (weather_kind == kWeatherStorm ? 0.55f : 0.0f);
    WeatherRainSound(gain * weather_amount);
}

void WeatherRestore() {
    SetSunPosition(weather_base_sun_pos);
    SetSunColor(weather_base_sun_col);
    SetSunAmbient(weather_base_ambient);
    SetSkyTint(weather_base_tint);
    weather_applied = false;
}

// The sun (or the moon, 12 hours on) at `hour`: an arc from one side at 6:00
// to the other at 18:00, through the level's own noon direction, 65 degrees
// up at noon
vec3 WeatherArc(float hour) {
    vec3 noon = weather_base_sun_pos;
    noon.y = 0.0f;

    if(length(noon) < 0.01f) {
        noon = vec3(0.0f, 0.0f, 1.0f);
    }

    noon = normalize(noon);
    vec3 side = normalize(cross(vec3(0.0f, 1.0f, 0.0f), noon));
    vec3 high = noon * cos(1.134f) + vec3(0.0f, 1.0f, 0.0f) * sin(1.134f);
    float t = (hour - 6.0f) / 12.0f * 3.14159265f;
    return normalize(side * -cos(t) + high * sin(t));
}

// 0 day, 1 sunrise or sunset, 2 twilight, 3 night (the level's own time: day)
int WeatherPhase() {
    if(!weather_time_on) {
        return 0;
    }

    float y = WeatherArc(weather_hour).y;
    return y > 0.20f ? 0 : (y > -0.08f ? 1 : (y > -0.22f ? 2 : 3));
}

// The game's own sky for the hour and the weather ("own": the level's)
string WeatherDome() {
    const string kSkies = "Data/Textures/skies/";
    int phase = WeatherPhase();

    if(weather_kind == kWeatherOwn) {
        return phase == 0 ? "own" : kSkies + (phase == 1 ? "CloudySunset.tga" : (phase == 2 ? "dusk.tga" : "full_moon.tga"));
    }

    if(phase >= 2) {
        return kSkies + "blue_overcast.tga";  // any weather at night
    }

    if(weather_kind == kWeatherCloudy) {
        return kSkies + (phase == 1 ? "DarkSunset.tga" : "cloudy2.tga");
    } else if(weather_kind == kWeatherRain) {
        return kSkies + "dark_overcast.tga";
    } else if(weather_kind == kWeatherStorm) {
        return kSkies + "Stormy1.tga";
    }

    return kSkies + "winter_overcast.tga";  // snow, blizzard
}

// Ask the engine for a sky dome (once per change: a change re-bakes the sky)
void WeatherSetDome(const string &in dome) {
    if(dome != weather_dome) {
        weather_dome = dome;
        level.SendMessage("oo_sky " + dome);
    }
}

// The engine's answer: this build swaps skies
void WeatherSkyReceive(const string &in message) {
    if(message.length() > 11 && message.substr(0, 11) == "oo_sky_set ") {
        weather_sky_swaps = true;
    }
}

void WeatherSky(int is_paused) {
    vec3 sun_pos = weather_base_sun_pos;
    vec3 sun_col = weather_base_sun_col;
    vec3 tint = weather_base_tint;
    float ambient = weather_base_ambient;

    if(weather_time_on) {
        vec3 sun = WeatherArc(weather_hour);
        vec3 moon = WeatherArc(weather_hour + 12.0f);
        float bright = max(0.9f, WLuma(weather_base_sun_col));
        float day = WSmooth(-0.25f, 0.15f, sun.y);    // 0 night .. 1 day: about an hour of twilight
        float low = 1.0f - WSmooth(0.0f, 0.40f, sun.y);   // warm near the horizon
        vec3 sun_light = mix(vec3(1.0f, 0.96f, 0.90f), vec3(1.0f, 0.52f, 0.22f), low) * bright * WSmooth(-0.05f, 0.15f, sun.y);
        vec3 moon_light = vec3(0.28f, 0.36f, 0.55f) * 0.35f * bright * WSmooth(-0.05f, 0.25f, moon.y);

        // Whichever is up; both are dark where they cross the horizon
        sun_pos = sun.y >= moon.y ? sun : moon;
        sun_col = sun.y >= moon.y ? sun_light : moon_light;
        ambient = weather_base_ambient * (weather_sky_swaps ? 0.55f + 0.45f * day : 0.30f + 0.70f * day);
        tint = weather_base_tint * mix(vec3(0.05f, 0.07f, 0.13f), mix(vec3(1.0f), vec3(1.0f, 0.50f, 0.30f), low), day);
    }

    WeatherSetDome(WeatherDome());

    if(weather_sky_swaps && weather_dome != "own") {
        // The game's own sky for the hour and the weather: it brings its
        // colours, so the tint only dims it at night and in rain
        float day = weather_time_on ? WSmooth(-0.25f, 0.15f, WeatherArc(weather_hour).y) : 1.0f;
        float dim = weather_kind == kWeatherStorm ? 0.75f : (weather_kind == kWeatherRain ? 0.85f :
            (weather_kind == kWeatherSnow ? 0.85f : (weather_kind == kWeatherBlizzard ? 0.8f : 1.0f)));
        tint = vec3(max(0.8f, min(1.3f, WLuma(weather_base_tint))) * (0.65f + 0.35f * day) * dim);
    }

    // Weather: less sun, a greyer sky (the sky is bright: it takes a strong
    // tint to show, and less blue than red to turn a blue sky grey; a swapped
    // sky is grey already)
    if(weather_sky_swaps && weather_dome != "own") {
        float sun_k = weather_kind == kWeatherCloudy ? 0.35f : (weather_kind == kWeatherRain ? 0.20f :
            (weather_kind == kWeatherStorm ? 0.10f : (weather_kind == kWeatherSnow ? 0.45f : (weather_kind == kWeatherBlizzard ? 0.25f : 1.0f))));
        sun_col *= sun_k;
    } else if(weather_kind == kWeatherCloudy) {
        sun_col *= 0.35f;
        tint = tint * vec3(0.60f, 0.50f, 0.33f);
    } else if(weather_kind == kWeatherRain) {
        sun_col *= 0.20f;
        ambient *= 0.95f;
        tint = tint * vec3(0.32f, 0.27f, 0.15f);
    } else if(weather_kind == kWeatherStorm) {
        sun_col *= 0.10f;
        ambient *= 0.85f;
        tint = tint * vec3(0.17f, 0.14f, 0.08f);
    } else if(weather_kind == kWeatherSnow) {
        sun_col *= 0.45f;
        ambient *= 1.1f;
        tint = tint * vec3(0.75f, 0.73f, 0.66f);
    } else if(weather_kind == kWeatherBlizzard) {
        sun_col *= 0.25f;
        ambient *= 1.05f;
        tint = tint * vec3(0.55f, 0.54f, 0.50f);
    }

    if(weather_kind == kWeatherStorm) {
        WeatherLightning(is_paused, sun_pos, sun_col, tint, ambient);
    }

    SetSunPosition(sun_pos);
    SetSunColor(sun_col);
    SetSunAmbient(ambient);
    SetSkyTint(tint);
    weather_applied = true;
}

// A flash every 5 to 14 seconds (more often with more rain), thunder after it
// by how far it struck (hotspots/rain.as)
void WeatherLightning(int is_paused, vec3 &inout sun_pos, vec3 &inout sun_col, vec3 &inout tint, float &inout ambient) {
    if(is_paused == 0 && the_time >= weather_lightning_next) {
        weather_lightning_next = the_time + RangedRandomFloat(5.0f, 14.0f) / weather_amount;
        weather_lightning_at = the_time;
        weather_lightning_far = RangedRandomFloat(0.0f, 1.0f);
        weather_thunder_at = the_time + weather_lightning_far * 3.0f;
        weather_flash_dir = normalize(vec3(RangedRandomFloat(-1.0f, 1.0f), RangedRandomFloat(0.5f, 1.0f), RangedRandomFloat(-1.0f, 1.0f)));
    }

    if(weather_thunder_at >= 0.0f && the_time >= weather_thunder_at) {
        weather_thunder_at = -1.0f;
        PlaySoundGroup(weather_lightning_far < 0.3f ? "Data/Sounds/weather/thunder_strike_mike_koenig.xml" :
            "Data/Sounds/weather/tapio/thunder.xml");
    }

    float since = the_time - weather_lightning_at;

    if(since >= 0.0f && since < 0.5f) {
        float flash = (1.0f - since * 2.0f) * (1.0f - weather_lightning_far * 0.6f) * RangedRandomFloat(0.8f, 1.2f) * 2.5f;
        tint = mix(tint, vec3(2.5f), min(1.0f, flash));
        sun_col += vec3(flash * 1.5f);
        sun_pos = weather_flash_dir;
        ambient += flash * 0.5f;
    }
}

// The open sky above `pos` (no roof): rain and snow only fall there
bool WeatherOpenSky(const vec3 &in pos) {
    vec3 top = pos + vec3(0.0f, 40.0f, 0.0f);
    return col.GetRayCollision(pos, top) == top;
}

// Spawn `count` particles of `path` in a box around `center`
void WeatherSpawn(const string &in path, int count, const vec3 &in center, float half_width, float low, float high,
        const vec3 &in vel) {
    for(int i = 0; i < count; ++i) {
        vec3 pos = center + vec3(RangedRandomFloat(-half_width, half_width), RangedRandomFloat(low, high),
            RangedRandomFloat(-half_width, half_width));

        if(WeatherOpenSky(pos)) {
            MakeParticle(path, pos, vel);
        }
    }
}

int WeatherDue(float &inout debt, float per_second) {
    debt += per_second * time_step;
    int count = min(60, int(debt));  // (a long frame doesn't burst)
    debt -= float(int(debt));
    return count;
}

void WeatherFall() {
    vec3 cam = camera.GetPos();
    vec3 cam_vel = weather_cam_known ? (cam - weather_cam_prev) / time_step : vec3(0.0f);
    weather_cam_prev = cam;
    weather_cam_known = true;

    if(length(cam_vel) > 30.0f) {
        cam_vel = vec3(0.0f);  // a cut, not a move
    }

    vec3 wind = weather_kind == kWeatherBlizzard ? vec3(5.0f, 0.0f, 2.0f) :
        (weather_kind == kWeatherStorm ? vec3(2.5f, 0.0f, 1.0f) : vec3(0.8f, 0.0f, 0.3f));

    // Mostly in front of the camera, where it is seen
    vec3 ahead = camera.GetFacing();
    ahead.y = 0.0f;
    ahead = length(ahead) > 0.01f ? normalize(ahead) * 5.0f : vec3(0.0f);

    if(weather_kind == kWeatherRain || weather_kind == kWeatherStorm) {
        float rate = (weather_kind == kWeatherStorm ? 1400.0f : 900.0f) * weather_amount;
        WeatherSpawn("Data/Particles/overhaul/rain.xml", WeatherDue(weather_rain_debt, rate), cam + ahead + cam_vel * 0.3f, 8.0f,
            1.0f, 8.0f, wind + vec3(0.0f, -15.0f, 0.0f));
    } else if(weather_kind == kWeatherSnow || weather_kind == kWeatherBlizzard) {
        float rate = (weather_kind == kWeatherBlizzard ? 700.0f : 300.0f) * weather_amount;
        WeatherSpawn("Data/Particles/overhaul/snow.xml", WeatherDue(weather_snow_debt, rate), cam + ahead + cam_vel, 9.0f, -5.0f,
            9.0f, wind * 0.6f + vec3(0.0f, -0.6f, 0.0f));

        if(weather_kind == kWeatherBlizzard) {
            WeatherSpawn("Data/Particles/heavysnow.xml", WeatherDue(weather_flake_debt, 12.0f * weather_amount), cam + cam_vel,
                12.0f, -4.0f, 6.0f, wind * 1.2f);
        }
    }
}

void WeatherRainSound(float gain) {
    if(gain <= 0.0f) {
        if(weather_rain_sound != -1) {
            StopSound(weather_rain_sound);
            weather_rain_sound = -1;
        }

        return;
    }

    if(weather_rain_sound == -1) {
        weather_rain_sound = PlaySoundLoop("Data/Sounds/weather/rain.wav", gain);
        weather_rain_gain = gain;
    } else if(abs(gain - weather_rain_gain) > 0.01f) {
        SetSoundGain(weather_rain_sound, gain);
        weather_rain_gain = gain;
    }
}
