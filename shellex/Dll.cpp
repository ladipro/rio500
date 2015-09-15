/**************************************************************************
    THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
   ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
   THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
   PARTICULAR PURPOSE.

   (c) Microsoft Corporation. All Rights Reserved.
**************************************************************************/

#include <windows.h>
#include <shlwapi.h>
#include <objbase.h>
#include <Shlobj.h>
#include <olectl.h>
//#include <usbiodef.h>
#include <strsafe.h>
#include "Utils.h"
#include <new>  // std::nothrow
#include <thread>
#include <mutex>
#include <condition_variable>

// The GUID for the FolderViewImpl
#include "GUID.h"
#include "WorkerThread.h"

#pragma comment(lib, "propsys.lib")
#pragma comment(lib, "shlwapi.lib")

#pragma comment(linker,"/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

HRESULT CFolderViewImplFolder_CreateInstance(REFIID riid, void **ppv);
HRESULT CFolderViewImplContextMenu_CreateInstance(REFIID riid, void **ppv);

typedef HRESULT (*PFNCREATEINSTANCE)(REFIID riid, void **ppvObject);
struct CLASS_OBJECT_INIT
{
    const CLSID *pClsid;
    PFNCREATEINSTANCE pfnCreate;
};

// add classes supported by this module here
const CLASS_OBJECT_INIT c_rgClassObjectInit[] =
{
    { &CLSID_FolderViewImpl,            CFolderViewImplFolder_CreateInstance },
    { &CLSID_FolderViewImplContextMenu, CFolderViewImplContextMenu_CreateInstance },
};


long                    g_cRefModule = 0;

// Handle the the DLL's module
HINSTANCE g_hInst = NULL;

// Standard DLL functions
STDAPI_(BOOL) DllMain(HINSTANCE hInstance, DWORD dwReason, void *)
{
    if (dwReason == DLL_PROCESS_ATTACH)
    {
        g_hInst = hInstance;
        DisableThreadLibraryCalls(hInstance);
    }
    else if (dwReason == DLL_PROCESS_DETACH)
    {
        StopWorkerThread(TRUE);
    }
    return TRUE;
}

STDAPI DllCanUnloadNow()
{
    // Only allow the DLL to be unloaded after all outstanding references have been released
    return (g_cRefModule == 0) ? S_OK : S_FALSE;
}

void DllAddRef()
{
  InterlockedIncrement(&g_cRefModule);
}

void DllRelease()
{
  InterlockedDecrement(&g_cRefModule);
}

class CClassFactory : public IClassFactory
{
public:
    static HRESULT CreateInstance(REFCLSID clsid, const CLASS_OBJECT_INIT *pClassObjectInits, size_t cClassObjectInits, REFIID riid, void **ppv)
    {
        *ppv = NULL;
        HRESULT hr = CLASS_E_CLASSNOTAVAILABLE;
        for (size_t i = 0; i < cClassObjectInits; i++)
        {
            if (clsid == *pClassObjectInits[i].pClsid)
            {
                IClassFactory *pClassFactory = new (std::nothrow) CClassFactory(pClassObjectInits[i].pfnCreate);
                hr = pClassFactory ? S_OK : E_OUTOFMEMORY;
                if (SUCCEEDED(hr))
                {
                    hr = pClassFactory->QueryInterface(riid, ppv);
                    pClassFactory->Release();
                }
                break; // match found
            }
        }

        // create the worker thread on first object instantiation
        StartWorkerThread();
        return hr;
    }

    CClassFactory(PFNCREATEINSTANCE pfnCreate) : _cRef(1), _pfnCreate(pfnCreate)
    {
        DllAddRef();
    }

    // IUnknown
    IFACEMETHODIMP QueryInterface(REFIID riid, void ** ppv)
    {
        static const QITAB qit[] =
        {
            QITABENT(CClassFactory, IClassFactory),
            { 0 }
        };
        return QISearch(this, qit, riid, ppv);
    }

    IFACEMETHODIMP_(ULONG) AddRef()
    {
        return InterlockedIncrement(&_cRef);
    }

    IFACEMETHODIMP_(ULONG) Release()
    {
        long cRef = InterlockedDecrement(&_cRef);
        if (cRef == 0)
        {
            delete this;
        }
        return cRef;
    }

    // IClassFactory
    IFACEMETHODIMP CreateInstance(IUnknown *punkOuter, REFIID riid, void **ppv)
    {
        return punkOuter ? CLASS_E_NOAGGREGATION : _pfnCreate(riid, ppv);
    }

    IFACEMETHODIMP LockServer(BOOL fLock)
    {
        if (fLock)
        {
            DllAddRef();
        }
        else
        {
            DllRelease();
        }
        return S_OK;
    }

private:
    ~CClassFactory()
    {
        DllRelease();
    }

    long _cRef;
    PFNCREATEINSTANCE _pfnCreate;
};


STDAPI DllGetClassObject(REFCLSID clsid, REFIID riid, void **ppv)
{
    return CClassFactory::CreateInstance(clsid, c_rgClassObjectInit, ARRAYSIZE(c_rgClassObjectInit), riid, ppv);
}


#define MYCOMPUTER_NAMESPACE_GUID L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\MyComputer\\NameSpace\\%s"

const WCHAR g_szExtTitle[] =  L"Rio 500";

void RefreshFolderViews(UINT csidl)
{
    PIDLIST_ABSOLUTE pidl;
    if (SUCCEEDED(SHGetSpecialFolderLocation(NULL, csidl, &pidl)))
    {
        SHChangeNotify(SHCNE_UPDATEDIR, SHCNF_IDLIST, pidl, 0);
        CoTaskMemFree(pidl);
    }
}

// Structure to hold data for individual keys to register.
typedef struct
{
    HKEY  hRootKey;
    PCWSTR pszSubKey;
    PCWSTR pszClassID;
    PCWSTR pszValueName;
    BYTE *pszData;
    DWORD dwType;
} REGSTRUCT;

//  1. The classID must be created under HKCR\CLSID.
//     a. DefaultIcon must be set to <Path and Module>,0.
//     b. InprocServer32 set to path and module.
//        i. Threading model specified as Apartment.
//     c. ShellFolder attributes must be set.
//  2. If the extension in non-rooted, its GUID is entered at the desired folder.
//  3. It must be registered as approved for Windows NT or XP.
STDAPI DllRegisterServer()
{
    WCHAR szFolderViewImplClassID[64], szContextMenuClassID[64], szSubKey[MAX_PATH], szData[MAX_PATH];
    StringFromGUID2(CLSID_FolderViewImpl, szFolderViewImplClassID, ARRAYSIZE(szFolderViewImplClassID));      // Convert the IID to a string.
    StringFromGUID2(CLSID_FolderViewImplContextMenu, szContextMenuClassID, ARRAYSIZE(szContextMenuClassID)); // Convert the IID to a string.

    // Get the path and module name.
    WCHAR szModulePathAndName[MAX_PATH];
    GetModuleFileName(g_hInst, szModulePathAndName, ARRAYSIZE(szModulePathAndName));
    PWSTR pwzLastDot = StrRChr(szModulePathAndName, NULL, L'.');
    if (!pwzLastDot)
      return E_FAIL;
    *pwzLastDot = 0;
    StrCat(szModulePathAndName, L"Stub.dll");

    // two CLSID in registry path - doesn't quite fit the table below
    WCHAR szRootContextMenuHandlers[MAX_PATH];
    StringCchPrintf(
        szRootContextMenuHandlers,
        ARRAYSIZE(szRootContextMenuHandlers),
        L"Software\\Classes\\CLSID\\%%s\\shellex\\ContextMenuHandlers\\%s",
        szContextMenuClassID);


    // This will setup and register the basic ClassIDs.
    DWORD dwData = SFGAO_FOLDER | SFGAO_HASSUBFOLDER | /*SFGAO_GHOSTED |*/ SFGAO_REMOVABLE | SFGAO_DROPTARGET | SFGAO_NONENUMERATED;
    REGSTRUCT rgRegEntries[] =
    {
        HKEY_CURRENT_USER,   L"Software\\Classes\\CLSID\\%s",                  szFolderViewImplClassID, NULL,                   (LPBYTE)g_szExtTitle,            REG_SZ,
        HKEY_CURRENT_USER,   L"Software\\Classes\\CLSID\\%s",                  szFolderViewImplClassID, L"DescriptionID",       (LPBYTE)L"19",                   REG_SZ,
        HKEY_CURRENT_USER,   L"Software\\Classes\\CLSID\\%s",                  szFolderViewImplClassID, L"System.ItemTypeText", (LPBYTE)L"Digital audio player", REG_SZ,
        HKEY_CURRENT_USER,   L"Software\\Classes\\CLSID\\%s\\InprocServer32",  szFolderViewImplClassID, NULL,                   (LPBYTE)L"%s",                   REG_SZ,
        HKEY_CURRENT_USER,   L"Software\\Classes\\CLSID\\%s\\InprocServer32",  szFolderViewImplClassID, L"ThreadingModel",      (LPBYTE)L"Apartment",            REG_SZ,
        HKEY_CURRENT_USER,   L"Software\\Classes\\CLSID\\%s\\DefaultIcon",     szFolderViewImplClassID, NULL,                   (LPBYTE)L"C:\\Users\\ladipro\\Desktop\\Rio500\\ShellEx\\RioShellEx\\x64\\Debug\\RioShellEx.dll,-145"/*L"shell32.dll,-42"*/, REG_SZ,
        HKEY_CURRENT_USER,   L"Software\\Classes\\CLSID\\%s\\ShellFolder",     szFolderViewImplClassID, L"Attributes",          (LPBYTE)&dwData,                 REG_DWORD,
        HKEY_CURRENT_USER,   L"Software\\Classes\\CLSID\\%s\\ShellFolder",     szFolderViewImplClassID, L"QueryForOverlay",     (LPBYTE)L"",                     REG_SZ,
        HKEY_CURRENT_USER,   L"Software\\Classes\\CLSID\\%s\\ShellFolder",     szFolderViewImplClassID, L"CallForAttributes",   (LPBYTE)L"",                     REG_SZ,
        HKEY_CURRENT_USER,   L"Software\\Classes\\CLSID\\%s\\shellex",         szFolderViewImplClassID, NULL,                   (LPBYTE)NULL,                    REG_NONE,
        HKEY_CURRENT_USER,   L"Software\\Classes\\CLSID\\%s\\shellex\\ContextMenuHandlers", szFolderViewImplClassID, NULL,      (LPBYTE)NULL,                    REG_NONE,
        HKEY_CURRENT_USER,   szRootContextMenuHandlers,                        szFolderViewImplClassID, NULL,                   (LPBYTE)szContextMenuClassID,    REG_SZ,
        HKEY_CURRENT_USER,   L"Software\\Classes\\CLSID\\%s",                  szContextMenuClassID,    NULL,                   (LPBYTE)g_szExtTitle,            REG_SZ,
        HKEY_CURRENT_USER,   L"Software\\Classes\\CLSID\\%s\\InprocServer32",  szContextMenuClassID,    NULL,                   (LPBYTE)L"%s",                   REG_SZ,
        HKEY_CURRENT_USER,   L"Software\\Classes\\CLSID\\%s\\InprocServer32",  szContextMenuClassID,    L"ThreadingModel",      (LPBYTE)L"Apartment",            REG_SZ,

        // For performance, only context menu verbs that register this are considered when the user double-clicks.
        HKEY_CURRENT_USER,   L"Software\\Classes\\CLSID\\%s\\ShellEx\\MayChangeDefaultMenu",                szContextMenuClassID, NULL,  (LPBYTE)L"",                  REG_SZ,
        // register the context menu handler under the FolderViewSampleType type.
        HKEY_CURRENT_USER,   L"Software\\Classes\\FolderViewSampleType\\shellex\\ContextMenuHandlers\\%s",  szContextMenuClassID, NULL,  (LPBYTE)szContextMenuClassID, REG_SZ,
    };

    HKEY hKey = NULL;
    HRESULT hr = S_OK;

    for (int i = 0; SUCCEEDED(hr) && (i < ARRAYSIZE(rgRegEntries)); i++)
    {
        // Create the sub key string.
        hr = StringCchPrintf(szSubKey, ARRAYSIZE(szSubKey), rgRegEntries[i].pszSubKey, rgRegEntries[i].pszClassID);
        if (SUCCEEDED(hr))
        {
            long lResult = RegCreateKeyEx(rgRegEntries[i].hRootKey, szSubKey, 0, NULL,
                                          REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hKey, NULL);
            if (ERROR_SUCCESS == lResult)
            {
                // If this is a string entry, create the string.
                if (REG_SZ == rgRegEntries[i].dwType)
                {
                    hr = StringCchPrintf(szData, ARRAYSIZE(szData), (LPWSTR)rgRegEntries[i].pszData, szModulePathAndName);
                    if (SUCCEEDED(hr))
                    {
                        RegSetValueEx(hKey,
                                      rgRegEntries[i].pszValueName,
                                      0,
                                      rgRegEntries[i].dwType,
                                      (LPBYTE)szData,
                                      (lstrlen(szData) + 1) * sizeof(WCHAR));
                    }
                }
                else if (REG_DWORD == rgRegEntries[i].dwType)
                {
                    RegSetValueEx(hKey,
                                  rgRegEntries[i].pszValueName,
                                  0, rgRegEntries[i].dwType,
                                  rgRegEntries[i].pszData,
                                  sizeof(DWORD));
                }

                RegCloseKey(hKey);
            }
            else
            {
                hr = SELFREG_E_CLASS;
            }
        }
    }

    if (SUCCEEDED(hr))
    {
        hr = SELFREG_E_CLASS;

        // Now we are ready to register the namespace extension.
        // This will put our extension in My Computer.
        if (SUCCEEDED(StringCchPrintf(szSubKey, ARRAYSIZE(szSubKey), MYCOMPUTER_NAMESPACE_GUID, szFolderViewImplClassID)))
        {
            long lResult = RegCreateKeyEx(HKEY_CURRENT_USER, szSubKey, 0, NULL,
                                          REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hKey, NULL);
            if (ERROR_SUCCESS == lResult)
            {
                // Copy our name into the string.
                hr = StringCchCopy(szData, ARRAYSIZE(szData), g_szExtTitle);
                if (SUCCEEDED(hr))
                {
                    // Set the name of our extension.
                    lResult = RegSetValueEx(hKey, NULL, 0, REG_SZ, (LPBYTE)szData, (lstrlen(szData) + 1) * sizeof(szData[0]));
                    RegCloseKey(hKey);

                    // The Shell has to be notified that the change has been made.
                    RefreshFolderViews(CSIDL_DRIVES);
                }
            }
        }
    }

    if (SUCCEEDED(hr))
    {
        // Note: The property description file needs to be in the same
        // directory as RioShellEx.dll when registering the DLL.
        // Opportunistically attempt to register -- if regsvr32 is run elevated
        // then this will succeed, otherwise it will fail and some functionality
        // (such as exposing properties in the details pane) will not work.
        PSRegisterPropertySchema(L"RioShellEx.propdesc");
    }

    if (FAILED(hr))
    {
        // Remove the stuff we added.
        DllUnregisterServer();
    }

    return hr;
}

// Registry keys are removed here.
STDAPI DllUnregisterServer()
{
    WCHAR szSubKey[MAX_PATH], szFolderClassID[MAX_PATH], szContextMenuClassID[MAX_PATH];

    //Delete the context menu entries.
    StringFromGUID2(CLSID_FolderViewImplContextMenu, szContextMenuClassID, ARRAYSIZE(szContextMenuClassID));
    HRESULT hrCM = StringCchPrintf(szSubKey, ARRAYSIZE(szSubKey), L"CLSID\\%s", szContextMenuClassID);
    if (SUCCEEDED(hrCM))
    {
        hrCM = HRESULT_FROM_WIN32(RegDeleteTree(HKEY_CLASSES_ROOT, szSubKey));
        if (hrCM == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND))
        {
            hrCM = S_OK;
        }
    }

    // Delete the foldertype key.
    HRESULT hrFT = HRESULT_FROM_WIN32(RegDeleteTree(HKEY_CLASSES_ROOT, L"FolderViewSampleType"));
    if (hrFT == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND))
    {
        hrFT = S_OK;
    }

    // Delete the namespace extension entries
    StringFromGUID2(CLSID_FolderViewImpl, szFolderClassID, ARRAYSIZE(szFolderClassID));
    HRESULT hrSF = StringCchPrintf(szSubKey, ARRAYSIZE(szSubKey), MYCOMPUTER_NAMESPACE_GUID, szFolderClassID);
    if (SUCCEEDED(hrSF))
    {
        hrSF = HRESULT_FROM_WIN32(RegDeleteTree(HKEY_CURRENT_USER, szSubKey));
        if (hrSF == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND))
        {
            hrSF = S_OK;
        }
        if (SUCCEEDED(hrSF))
        {
            // Delete the object's registry entries
            hrSF = StringCchPrintf(szSubKey, ARRAYSIZE(szSubKey), L"CLSID\\%s", szFolderClassID);
            if (SUCCEEDED(hrSF))
            {
                hrSF = HRESULT_FROM_WIN32(RegDeleteTree(HKEY_CLASSES_ROOT, szSubKey));
                if (hrSF == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND))
                {
                    hrSF = S_OK;
                }
                if (SUCCEEDED(hrSF))
                {
                    // Refresh the folder views that might be open
                    RefreshFolderViews(CSIDL_DRIVES);
                }
            }
        }
    }

    // Remove the property description, this is expected to fail if it is not run elevated.
    PSUnregisterPropertySchema(L"RioShellEx.propdesc");

    return (SUCCEEDED(hrCM) && SUCCEEDED(hrSF) && SUCCEEDED(hrFT)) ? S_OK : SELFREG_E_CLASS;
}
