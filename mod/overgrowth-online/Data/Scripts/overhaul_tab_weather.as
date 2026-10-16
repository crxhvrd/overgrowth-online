//-----------------------------------------------------------------------------
//           Name: overhaul_tab_weather.as
//    Description: "Weather" tab of the Overgrowth Online window: time of day
//                 and weather (ow_* config keys). Runtime: overhaul_weather.as.
//                 Online the host's; players see what the host set.
//-----------------------------------------------------------------------------

const array<string> kWeatherTimeNames = {"Dawn", "Morning", "Noon", "Afternoon", "Dusk", "Night"};
const array<float> kWeatherTimeHours = {6.4f, 9.0f, 12.0f, 15.0f, 17.6f, 23.0f};

void WeatherSet() {
    weather_read_next = 0.0f;  // at once, even paused
}

void DrawWeatherTab() {
    if(MPClient()) {
        Header("Weather and time");
        Note("The host's: " + WeatherSummary() + ".");
        return;
    }

    if(MPHost()) {
        Note("Everyone in your game gets your weather and time.");
    } else {
        Note("They stay on in every level until you turn them off.");
    }

    Header("Time of day");
    bool time_on = CfgBool("ow_time_on", false);

    if(ImGui_Checkbox("Set the time of day", time_on)) {
        SetConfigValueBool("ow_time_on", time_on);
        WeatherSet();
    }

    Tip("Off: the level's own light.");

    if(time_on) {
        float hour = weather_hour;

        if(ImGui_SliderFloat("Time##ow_hour", hour, 0.0f, 24.0f, WeatherClock(hour))) {
            SetConfigValueFloat("ow_hour", max(0.0f, min(24.0f, hour)));
            WeatherSet();
        }

        Tip("Where the sun is, and its light. At night a moon lights the level.");

        for(uint i = 0; i < kWeatherTimeNames.length(); ++i) {
            if(i > 0) {
                ImGui_SameLine();
            }

            if(ImGui_Button(kWeatherTimeNames[i])) {
                SetConfigValueFloat("ow_hour", kWeatherTimeHours[i]);
                WeatherSet();
            }
        }

        bool runs = CfgBool("ow_time_runs", false);

        if(ImGui_Checkbox("Time passes", runs)) {
            SetConfigValueBool("ow_time_runs", runs);
            WeatherSet();
        }

        Tip("The sun moves across the sky, and day turns to night.");

        if(runs) {
            float minutes = CfgFloat("ow_day_minutes", 24.0f);

            if(ImGui_SliderFloat("A day lasts", minutes, 2.0f, 120.0f, "%.0f minutes")) {
                SetConfigValueFloat("ow_day_minutes", minutes);
                WeatherSet();
            }
        }
    }

    Header("Weather");
    int kind = CfgInt("ow_kind", kWeatherOwn);

    if(ImGui_Combo("Weather", kind, kWeatherNames)) {
        SetConfigValueInt("ow_kind", kind);
        WeatherSet();
    }

    Tip("A greyer sky and less sun; rain and snow fall around you, not under a roof.");

    if(kind == kWeatherRain || kind == kWeatherStorm || kind == kWeatherSnow || kind == kWeatherBlizzard) {
        float amount = CfgFloat("ow_amount", 1.0f);

        if(ImGui_SliderFloat("How much falls", amount, 0.2f, 2.0f, "%.1fx")) {
            SetConfigValueFloat("ow_amount", amount);
            WeatherSet();
        }

        Tip("More rain or snow" + (kind == kWeatherStorm ? ", and more lightning." : "."));
    }
}

void ResetWeatherTab() {
    SetConfigValueBool("ow_time_on", false);
    SetConfigValueFloat("ow_hour", 12.0f);
    SetConfigValueBool("ow_time_runs", false);
    SetConfigValueFloat("ow_day_minutes", 24.0f);
    SetConfigValueInt("ow_kind", kWeatherOwn);
    SetConfigValueFloat("ow_amount", 1.0f);
    WeatherSet();
}
