#pragma once
#include <string>

namespace overlay {

// Waits for the game window, then hooks DXGI Present (DX11 and DX12). Call from the trainer thread.
bool Install();

// "DX11", "DX12", or "..." until the first frame has been seen.
const char* ApiName();
bool Drawing(); // renderer up and running: the menu is actually visible when open

// Name and memory of the graphics card the game renders with (empty until the first frame). Render thread only.
const std::string& GpuName();

} // namespace overlay
