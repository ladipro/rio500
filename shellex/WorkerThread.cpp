#include <thread>
#include <shlobj.h>
#include <dbt.h>
#include <process.h>
#include "WorkerThread.h"

//HANDLE                g_workerThread = NULL;
bool                  g_bRioConnected = false;
HWND                  g_messageWindow = NULL;
Rio500Remix::IRio500 *g_pRio = NULL;

// TODO?
extern PIDLIST_ABSOLUTE g_lastRootPIDL;
extern HINSTANCE g_hInst;

void StartWorkerThread()
{
    //if (!g_workerThread)
    //{
    //    TCHAR fileName[MAX_PATH + 1];
    //    GetModuleFileName(g_hInst, fileName, MAX_PATH);
    //    if (!LoadLibrary(fileName))
    //    {
    //        MessageBox(NULL, L"Unable to LL self", L"The world is collapsing", MB_OK);
    //    }

    //    HANDLE thread = CreateThread(NULL, 0, &WorkerThreadFunction, NULL, CREATE_SUSPENDED, NULL);
    //    _ASSERT(thread);
    //    if (InterlockedCompareExchangePointer(&g_workerThread, thread, NULL))
    //    {
    //        // exchange did not take place, somebody beat us to it
    //        FreeLibrary(g_hInst);
    //        TerminateThread(thread, 0); // or we could just leak it
    //    }
    //    else
    //    {
    //        ResumeThread(thread);
    //    }
    //}

    if (!g_messageWindow)
    {
        WorkerThreadFunction(NULL);
    }
}

void UpdateConnectedness(bool bConnected)
{
    bool bOldConnected = g_bRioConnected;
    g_bRioConnected = bConnected;

    if (bOldConnected != g_bRioConnected)
    {
        LPCWSTR str = g_bRioConnected ?
            L"C:\\Users\\ladipro\\Desktop\\Rio500\\ShellEx\\RioShellEx\\x64\\Debug\\RioShellEx.dll,-145" :
            L"C:\\Users\\ladipro\\Desktop\\Rio500\\ShellEx\\RioShellEx\\x64\\Debug\\RioShellEx.dll,-144";
        HKEY hkey;
        LONG ret = RegOpenKeyEx(
            HKEY_CURRENT_USER,
            L"Software\\Classes\\CLSID\\{BA16CE0E-728C-4FC9-98E5-D0B35B384597}\\DefaultIcon",
            REG_OPTION_NON_VOLATILE,
            KEY_WRITE,
            &hkey);
        ret = RegSetValueEx(
            hkey,
            NULL,
            NULL,
            REG_SZ,
            (LPBYTE)str,
            (lstrlen(str) + 1) * sizeof(str[0]));
        (void)ret;

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
            //g_pRio->Format(0);

            Rio500Remix::IFolders *pFolders;
            g_pRio->get_Folders(0, &pFolders);
            long count;
            pFolders->get_Count(&count);
            for (long i = 0; i < count; i++)
            {
                Rio500Remix::IFolder *pFolder;
                pFolders->get_Item(i + 1, &pFolder);
                BSTR bstrName;
                pFolder->get_Name(&bstrName);
                OutputDebugString(bstrName);
            }

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

DWORD WINAPI WorkerThreadFunction(LPVOID lpParameter)
{
    UNREFERENCED_PARAMETER(lpParameter);

    // instantiate Rio500Remix
    //CoInitializeEx(NULL, COINIT_MULTITHREADED);
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
        return 1;
    }
    HWND messageWindow = CreateWindow(windowClassName, 0, 0, 0, 0, 0, 0, HWND_MESSAGE, 0, 0, 0);
    if (!messageWindow)
    {
        OutputDebugString(L"Failed to create message-only window\n");
        return 1;
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
        return 1;
    }

    //MSG msg;
    //while (GetMessage(&msg, 0, 0, 0) > 0)
    //{
    //    TranslateMessage(&msg);
    //    DispatchMessage(&msg);
    //}

    //if (g_pRio)
    //{
    //    g_pRio->Release();
    //}
    //CoUninitialize();

    //// TODO: Unregister device notification
    //// TODO: Ditch std::thread, go raw instead
    //// TODO: Debug WinRAR crashing here
    //OutputDebugString(L"Exitting thread!!!");
    //FreeLibraryAndExitThread(g_hInst, 0);
    return 0;
}

void PostWorkerThreadMessage(UINT Msg, WPARAM wParam, LPARAM lParam)
{
    PostMessage(
        g_messageWindow,
        Msg,
        wParam,
        lParam);
}

STDAPI StopWorkerThread(BOOL bHard)
{
    UNREFERENCED_PARAMETER(bHard);
    if (g_messageWindow)
    {
        //if (bHard)
        //{
        //    BOOL bResult = TerminateThread(g_workerThread, 0);
        //    _ASSERT(bResult);
        //    CloseHandle(g_workerThread);
        //    g_workerThread = NULL;
        //}
        //else
        //{
        //    PostThreadMessage(GetThreadId(g_workerThread), WM_QUIT, 0, 0);
        //}

        DestroyWindow(g_messageWindow);
        if (g_pRio)
        {
            g_pRio->Release();
            g_pRio = NULL;
        }
    }
    return S_OK;
}
