#include <thread>
#include <shlobj.h>
#include <dbt.h>
#include <process.h>
#include "WorkerWindow.h"

bool                  g_bRioConnected = false;
HWND                  g_messageWindow = NULL;
Rio500Remix::IRio500 *g_pRio = NULL;

// TODO?
extern PIDLIST_ABSOLUTE g_lastRootPIDL;
extern HINSTANCE g_hInst;

LRESULT CALLBACK WindowProcedure(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

void CreateWorkerWindow()
{
    // TODO: Race?
    if (!g_messageWindow)
    {
        // instantiate Rio500Remix
        if (FAILED(CoCreateInstance(
            Rio500Remix::CLSID_Rio500,
            NULL,
            CLSCTX_INPROC_SERVER,
            Rio500Remix::IID_IRio500,
            (LPVOID *)&g_pRio)))
        {
            g_pRio = NULL;
        }

        WNDCLASSEX windowClass = {};
        windowClass.lpfnWndProc = WindowProcedure;
        LPCWSTR windowClassName = L"MessageOnlyWindow";
        windowClass.cbSize = sizeof(WNDCLASSEX);
        windowClass.lpszClassName = windowClassName;
        windowClass.hInstance = g_hInst;
        if (!RegisterClassEx(&windowClass))
        {
            OutputDebugString(L"Failed to register window class\n");
            return;
        }
        HWND messageWindow = CreateWindow(windowClassName, 0, 0, 0, 0, 0, 0, HWND_MESSAGE, 0, 0, 0);
        if (!messageWindow)
        {
            OutputDebugString(L"Failed to create message-only window\n");
            return;
        }

        // booya!
        g_messageWindow = messageWindow;

        DEV_BROADCAST_DEVICEINTERFACE NotificationFilter;
        GUID WceusbshGUID =
        {
            0xA5DCBF10, 0x6530, 0x11D2,
            0x90, 0x1F, 0x00, 0xC0, 0x4F, 0xB9, 0x51, 0xED
            //0x88BAE032, 0x5A81, 0x49f0,
            //0xBC, 0x3D, 0xA4, 0xFF, 0x13, 0x82, 0x16, 0xD6
            //0x25dbce51, 0x6c8f, 0x4a72,
            //0x8a, 0x6d, 0xb5, 0x4c, 0x2b, 0x4f, 0xc8, 0x35
        };

        ZeroMemory(&NotificationFilter, sizeof(NotificationFilter));
        NotificationFilter.dbcc_size = sizeof(DEV_BROADCAST_DEVICEINTERFACE);
        NotificationFilter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
        NotificationFilter.dbcc_classguid = WceusbshGUID;

        HDEVNOTIFY hDeviceNotify = RegisterDeviceNotification(
            messageWindow,              // events recipient
            &NotificationFilter,        // type of device
            DEVICE_NOTIFY_WINDOW_HANDLE | DEVICE_NOTIFY_ALL_INTERFACE_CLASSES// type of recipient handle
            );

        if (!hDeviceNotify)
        {
            OutputDebugString(L"RegisterDeviceNotification failed\n");
            return;
        }
    }
}

void UpdateConnectedness(bool bConnected)
{
    bool bOldConnected = g_bRioConnected;
    g_bRioConnected = bConnected;

    if (bOldConnected != g_bRioConnected)
    {
        SHChangeNotify(SHCNE_UPDATEITEM, SHCNF_IDLIST, g_lastRootPIDL, NULL);
    }
}

void PollForDevice()
{
    if (g_pRio && !g_bRioConnected)
    {
        HRESULT hr = g_pRio->Open();
        if (SUCCEEDED(hr))
        {
            g_pRio->Close();
            UpdateConnectedness(true);
        }
    }
}

LRESULT CALLBACK WindowProcedure(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    WCHAR buf[64];
    _itow_s(uMsg, buf, 16);
    wcscat_s(buf, L"h\n");
    OutputDebugString(buf);

    switch (uMsg)
    {
    case WM_CREATE:
    {
        PollForDevice();
        SetTimer(hWnd, 0, 15000, NULL); // try to open the Rio every now and then
        break;
    }

    case WM_TIMER:
    {
        PollForDevice();
        break;
    }

    case WM_DEVICECHANGE:
    {
        if (wParam == DBT_DEVICEARRIVAL || wParam == DBT_DEVICEREMOVECOMPLETE)
        {
            DEV_BROADCAST_HDR *pHdr = (DEV_BROADCAST_HDR *)lParam;
            if (pHdr->dbch_devicetype == DBT_DEVTYP_DEVICEINTERFACE)
            {
                DEV_BROADCAST_DEVICEINTERFACE *pDevInt = (DEV_BROADCAST_DEVICEINTERFACE *)pHdr;
                if (wcsstr(pDevInt->dbcc_name, L"VID_0841&PID_0001"))
                {
                    // +dbcc_name	0x0000000008bef54c L"\\\\?\\USB#VID_0841&PID_0001#5&28a57818&0&2#{a5dcbf10-6530-11d2-901f-00c04fb951ed}"	wchar_t[0x00000001]
                    UpdateConnectedness(wParam == DBT_DEVICEARRIVAL);
                }
            }
        }
        break;
    }

    case WM_FORMAT_DEVICE:
    {
        if (g_pRio)
        {
            g_pRio->Open();
            g_pRio->Format(0);
            g_pRio->Close();
        }
        PostMessage((HWND)lParam, WM_CLOSE, 0, 0);
        return TRUE;
    }

    case WM_DESTROY:
    {
        KillTimer(hWnd, 0);
        break;
    }
    }

    return DefWindowProc(hWnd, uMsg, wParam, lParam);
}

void PostWorkerWindowMessage(UINT Msg, WPARAM wParam, LPARAM lParam)
{
    PostMessage(
        g_messageWindow,
        Msg,
        wParam,
        lParam);
}

STDAPI DestroyWorkerWindow(BOOL bHard)
{
    UNREFERENCED_PARAMETER(bHard);
    if (g_messageWindow)
    {
        // TODO: Unregister device notifications?
        DestroyWindow(g_messageWindow);
        if (g_pRio)
        {
            g_pRio->Release();
            g_pRio = NULL;
        }
    }
    return S_OK;
}
