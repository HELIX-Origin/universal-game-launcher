#pragma once

#include <windows.h>

namespace GameExplorer {

HRESULT RegisterServer(HMODULE hModule);
HRESULT UnregisterServer();

} // namespace GameExplorer
