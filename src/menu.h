#pragma once
#include <string>

namespace menu {

void Init(void* dllModule, const std::string& gameVersion); // settings, hotkeys; the module holds the fonts
void SetupImGui();          // fonts + theme, right after ImGui::CreateContext()
void Draw();                // menu (when open) and toasts; between ImGui::NewFrame() and ImGui::Render()
bool IsOpen();
bool WantsFrame();          // open, or toasts still on screen
bool OnKeyDown(unsigned vk); // window thread; true = key consumed (menu key, hotkeys, key binding)
void Toast(std::string text); // small notification, top right; any thread

} // namespace menu
