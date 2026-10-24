#pragma once
#include <cstdint>

namespace missions {
// Reveals the game's existing chapter menu. Game thread only; never starts a game or writes saves.
void Tick(uint64_t now);
}
