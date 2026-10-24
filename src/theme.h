#pragma once
// Palette shared by the menu and the ESP: near-black surfaces, violet #AC6EE2 accent, pink #F89FCD highlights.
#include <imgui.h>

namespace theme {

inline const ImVec4 Bg{0.043f, 0.043f, 0.059f, 1.f};      // #0B0B0F
inline const ImVec4 Panel{0.071f, 0.071f, 0.094f, 1.f};   // #121218
inline const ImVec4 Panel2{0.106f, 0.106f, 0.137f, 1.f};  // #1B1B23
inline const ImVec4 Border{0.149f, 0.149f, 0.192f, 1.f};  // #262631
inline const ImVec4 Text{0.949f, 0.949f, 0.965f, 1.f};    // #F2F2F6
inline const ImVec4 Muted{0.557f, 0.557f, 0.604f, 1.f};   // #8E8E9A
inline const ImVec4 Violet{0.675f, 0.431f, 0.886f, 1.f};  // #AC6EE2
inline const ImVec4 Pink{0.973f, 0.624f, 0.804f, 1.f};    // #F89FCD
inline const ImVec4 Black{0.f, 0.f, 0.f, 1.f};

inline ImFont* regular; // Inter Regular
inline ImFont* bold;    // Inter SemiBold

inline ImVec4 Alpha(ImVec4 c, float a) { return {c.x, c.y, c.z, c.w * a}; }
inline ImVec4 Mix(ImVec4 a, ImVec4 b, float t) { return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t}; }
inline ImU32 U32(ImVec4 c) { return ImGui::GetColorU32(c); } // honours BeginDisabled's fade
inline ImU32 Raw(ImVec4 c) { return ImGui::ColorConvertFloat4ToU32(c); }

} // namespace theme
