#pragma once
#include "achievements_core.h"

namespace ach {
// Commands are handled by the existing game-thread tick. Nothing runs on DLL load.
void RequestRefresh();
void RequestUnlock();
void Tick();
Snapshot GetSnapshot();
}
