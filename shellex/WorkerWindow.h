#pragma once

#define WIN32_LEAN_AND_MEAN             // Exclude rarely-used stuff from Windows headers
// Windows Header Files:
#include <windows.h>

namespace Rio500Remix
{
#include "..\\..\\..\\Rio500Remix\\Rio500Remix.h"
}

#define WM_FORMAT_DEVICE (WM_USER + 0)

extern bool g_bRioConnected;
extern Rio500Remix::IRio500 *g_pRio;

void CreateWorkerWindow();
STDAPI DestroyWorkerWindow(BOOL bHard);
void PostWorkerWindowMessage(UINT Msg, WPARAM wParam, LPARAM lParam);
