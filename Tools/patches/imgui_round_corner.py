"""Crash fix (applied by hand, recorded here): integer divide-by-zero in
ImDrawList::_PathArcToFastEx (ImGui 1.85), from a script's rounded rectangle
whose corner radius ended up just over 0 after PathRect's clamps (the Overhaul
HUD's shield pips, health bars running out, the revive bar starting):
_CalcCircleAutoSegmentCount gives 0 for radius_idx 0. Crash dump 2026-10-16
05:26, Occupied Farm. Libraries/imgui-1.85/imgui_draw.cpp: PathRect rounds
only from 0.5 (as 1.86), PathArcToFast draws a point under 0.5 (NaN too), and
the step division never divides by 0. The scripts round only rects big enough
(overhaul_hud.as HudRectFilled/HudRect, coop_revive.as ReviveRectFilled),
which also protects the stock game."""
