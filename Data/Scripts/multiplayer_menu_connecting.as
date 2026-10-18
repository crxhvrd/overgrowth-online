//-----------------------------------------------------------------------------
//           Name: multiplayer_menu_connecting.as
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

#include "save/general.as"
#include "menu_common.as"
#include "music_load.as"

MusicLoad ml("Data/Music/menu.xml");

IMGUI@ imGUI;

bool back_event_queued = false;
float dot_counter = 0;
IMText@ connecting_text;

// Overgrowth Online: Cancel, and no endless wait: an answer comes within
// kGiveUpSeconds or the attempt ends with a word on why
const float kGiveUpSeconds = 20.0f;
float waited = 0.0f;
float leave_at = -1.0f;       // showing why it failed until then
FontSetup reason_font("edosz", 55, HexColor("#E0B060"), true);
IMText@ reason_text;

bool HasFocus() {
    return false;
}

void Initialize() {
    @imGUI = CreateIMGUI();

    // We're going to want a 100 'gui space' pixel header/footer
    imGUI.setHeaderHeight(200);
    imGUI.setFooterHeight(200);

    imGUI.setFooterPanels(200.0f, 1400.0f);
    imGUI.setup();

    BuildUI();
    setBackGround();
}

void BuildUI() {
    IMDivider header_divider("header_div", DOHorizontal);
    header_divider.setAlignment(CACenter, CACenter);
    AddTitleHeader("Connecting", header_divider);
    imGUI.getHeader().setElement(header_divider);

    IMDivider column("column", DOVertical);
    column.setAlignment(CALeft, CACenter);

    @connecting_text = IMText("Waiting for the host...", button_font);
    IMDivider text_row(DOHorizontal);
    text_row.append(IMSpacer(DOHorizontal, 40));
    text_row.append(connecting_text);
    column.append(text_row);

    @reason_text = IMText("", reason_font);
    IMDivider reason_row(DOHorizontal);
    reason_row.append(IMSpacer(DOHorizontal, 40));
    reason_row.append(reason_text);
    column.append(reason_row);
    column.append(IMSpacer(DOVertical, 40));

    // (Overgrowth Online: closing a connection still being made works now)
    AddButton("Cancel", column, exit_icon);

    IMDivider mainDiv( "mainDiv", DOHorizontal );
    mainDiv.append(column);
    imGUI.getMain().setElement( @mainDiv );
}

void Dispose() {
    imGUI.clear();
}

bool CanGoBack() {
    return back_event_queued;
}

void Update() {
    UpdateController();
    UpdateKeyboardMouse();

    // Animate connecting text
    if(leave_at < 0.0f) {
        UpdateConnectingText();
    }

    // No answer in time: stop trying, say so, then back
    waited += time_step;
    if(leave_at < 0.0f && waited > kGiveUpSeconds && !back_event_queued) {
        Online_Close();
        connecting_text.setText("No answer");
        reason_text.setText("The host didn't answer. Check the address, or that their game is open.");
        leave_at = waited + 3.0f;
    }
    if(leave_at >= 0.0f && waited > leave_at) {
        back_event_queued = true;
    }

    // This is a weak check for connection failures.
    // We expect to either move out of this menu on success, or fail connecting with a popup message
    // If the popup doesn't show up for whatever reason, we might end up with a softlock
    if(HasPopup()) {
        back_event_queued = true;
    } else if(back_event_queued) {
        this_ui.SendCallback("back");
    }

    // process any messages produced from the update
    while(imGUI.getMessageQueueSize() > 0) {
        IMMessage@ message = imGUI.getNextMessage();

        if(message.name == "Cancel") {
            Online_Close();  // (the connection still being made closes too: Online::StopMultiplayer)
            back_event_queued = true;
        }
    }

    // Do the general GUI updating
    imGUI.update();
}

void UpdateConnectingText() {
    dot_counter += time_step * 2;
    uint count = uint(dot_counter % 3 + 1);

    string dots = "";
    for(uint i = 0; i < count; i++) {
        dots += ".";
    }
    connecting_text.setText("Waiting for the host" + dots);
}

void Resize() {
    imGUI.doScreenResize(); // This must be called first
    setBackGround();
}

void ScriptReloaded() {
    // Clear the old GUI
    imGUI.clear();
    // Rebuild it
    Initialize();
}

void DrawGUI() {
    imGUI.render();
}

void Draw() {

}

void Init(string str) {

}
