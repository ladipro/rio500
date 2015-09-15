/**************************************************************************
    THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
   ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
   THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
   PARTICULAR PURPOSE.

   (c) Microsoft Corporation. All Rights Reserved.
**************************************************************************/

#include <windows.h>
#include <shlobj.h>
#include <propkey.h>
#include <shlwapi.h>
#include <strsafe.h>
#include <shellapi.h>
#include <new>  // std::nothrow
#include <mutex>
#include <condition_variable>

#include "resource.h"
#include "Utils.h"
#include "Guid.h"
#include "fvcommands.h"
#include "WorkerWindow.h"

const int g_nMaxLevel = 5;

PIDLIST_ABSOLUTE g_lastRootPIDL = NULL;

HRESULT CFolderViewCB_CreateInstance(REFIID riid, void **ppv);


#define MYOBJID 0x1234

// FVITEMID is allocated with a variable size, szName is the beginning
// of a NULL-terminated string buffer.
#pragma pack(1)
typedef struct tagObject
{
    USHORT  cb;
    WORD    MyObjID;
    WORD    nFolderIdx;
    WORD    nFileIdx;
    BYTE    nSize;
    BYTE    cchName;
    BOOL    fIsFolder;
    WCHAR   szName[1];
} FVITEMID;
#pragma pack()

typedef UNALIGNED FVITEMID *PFVITEMID;
typedef const UNALIGNED FVITEMID *PCFVITEMID;

class CFolderViewImplFolder : public IShellFolder2,
                              public IPersistFolder2,
                              public IShellIconOverlay,
                              public IQueryInfo,
                              public IShellPropSheetExt
{
public:
    CFolderViewImplFolder(int nFolderIdx, int nFileIdx);

    // IUnknown methods
    IFACEMETHODIMP QueryInterface(REFIID riid, void **ppv);
    IFACEMETHODIMP_(ULONG) AddRef();
    IFACEMETHODIMP_(ULONG) Release();

    // IShellFolder
    IFACEMETHODIMP ParseDisplayName(HWND hwnd, IBindCtx *pbc, PWSTR pszName,
                                    ULONG *pchEaten, PIDLIST_RELATIVE *ppidl, ULONG *pdwAttributes);
    IFACEMETHODIMP EnumObjects(HWND hwnd, DWORD grfFlags, IEnumIDList **ppenumIDList);
    IFACEMETHODIMP BindToObject(PCUIDLIST_RELATIVE pidl, IBindCtx *pbc, REFIID riid, void **ppv);
    IFACEMETHODIMP BindToStorage(PCUIDLIST_RELATIVE pidl, IBindCtx *pbc, REFIID riid, void **ppv);
    IFACEMETHODIMP CompareIDs(LPARAM lParam, PCUIDLIST_RELATIVE pidl1, PCUIDLIST_RELATIVE pidl2);
    IFACEMETHODIMP CreateViewObject(HWND hwnd, REFIID riid, void **ppv);
    IFACEMETHODIMP GetAttributesOf(UINT cidl, PCUITEMID_CHILD_ARRAY apidl, ULONG *rgfInOut);
    IFACEMETHODIMP GetUIObjectOf(HWND hwnd, UINT cidl, PCUITEMID_CHILD_ARRAY apidl,
                                 REFIID riid, UINT* prgfInOut, void **ppv);
    IFACEMETHODIMP GetDisplayNameOf(PCUITEMID_CHILD pidl, SHGDNF shgdnFlags, STRRET *pName);
    IFACEMETHODIMP SetNameOf(HWND hwnd, PCUITEMID_CHILD pidl, PCWSTR pszName, DWORD uFlags, PITEMID_CHILD * ppidlOut);

    // IShellFolder2
    IFACEMETHODIMP GetDefaultSearchGUID(GUID *pGuid);
    IFACEMETHODIMP EnumSearches(IEnumExtraSearch **ppenum);
    IFACEMETHODIMP GetDefaultColumn(DWORD dwRes, ULONG *pSort, ULONG *pDisplay);
    IFACEMETHODIMP GetDefaultColumnState(UINT iColumn, SHCOLSTATEF *pbState);
    IFACEMETHODIMP GetDetailsEx(PCUITEMID_CHILD pidl, const PROPERTYKEY *pkey, VARIANT *pv);
    IFACEMETHODIMP GetDetailsOf(PCUITEMID_CHILD pidl, UINT iColumn, SHELLDETAILS *pDetails);
    IFACEMETHODIMP MapColumnToSCID(UINT iColumn, PROPERTYKEY *pkey);

    // IPersist
    IFACEMETHODIMP GetClassID(CLSID *pClassID);

    // IPersistFolder
    IFACEMETHODIMP Initialize(PCIDLIST_ABSOLUTE pidl);

    // IPersistFolder2
    IFACEMETHODIMP GetCurFolder(PIDLIST_ABSOLUTE *ppidl);

    // IShellIconOverlay
    IFACEMETHODIMP GetOverlayIndex(PCUITEMID_CHILD pidl, int *pIndex);
    IFACEMETHODIMP GetOverlayIconIndex(PCUITEMID_CHILD pidl, int *pIconIndex);

    // IQueryInfo
    IFACEMETHODIMP GetInfoFlags(DWORD *pdwFlags);
    IFACEMETHODIMP GetInfoTip(DWORD dwFlags, PWSTR *ppwszTip);

    // IShellPropSheetExt
    IFACEMETHODIMP AddPages(LPFNADDPROPSHEETPAGE pfnAddPage, LPARAM lParam);
    IFACEMETHODIMP ReplacePage(UINT uPageID, LPFNADDPROPSHEETPAGE pfnReplacePage, LPARAM lParam);
    
    // IDList constructor public for the enumerator object
    HRESULT CreateChildID(PCWSTR pszName, int nFolderIdx, int nFileIdx, int nSize, BOOL fIsFolder, PITEMID_CHILD *ppidl);

private:
    ~CFolderViewImplFolder();

    HRESULT _GetName(PCUIDLIST_RELATIVE pidl, PWSTR pszName, int cchMax);
    HRESULT _GetName(PCUIDLIST_RELATIVE pidl, PWSTR *pszName);
    HRESULT _GetLevel(PCUIDLIST_RELATIVE pidl, int* pLevel);
    HRESULT _GetSize(PCUIDLIST_RELATIVE pidl, int* pSize);
    HRESULT _GetFolderness(PCUIDLIST_RELATIVE pidl, BOOL* pfIsFolder);
    HRESULT _ValidatePidl(PCUIDLIST_RELATIVE pidl);
    PCFVITEMID _IsValid(PCUIDLIST_RELATIVE pidl);

    HRESULT _GetColumnDisplayName(PCUITEMID_CHILD pidl, const PROPERTYKEY* pkey, VARIANT* pv, WCHAR* pszRet, UINT cch);

    long                    m_cRef;
    int                     m_nFolderIdx;
    int                     m_nFileIdx;
    PIDLIST_ABSOLUTE        m_pidl;             // where this folder is in the name space
    WCHAR                   m_szModuleName[MAX_PATH];
};

typedef struct
{
    int nIndex;
    int nSize;
    BOOL fIsFolder;
    WCHAR szName[MAX_PATH];
} ITEMDATA;

class CFolderViewImplEnumIDList : public IEnumIDList
{
public:
    CFolderViewImplEnumIDList(DWORD grfFlags, int nCurrent, CFolderViewImplFolder *pFolderViewImplShellFolder);

    // IUnknown methods
    IFACEMETHODIMP QueryInterface(REFIID riid, void **ppv);
    IFACEMETHODIMP_(ULONG) AddRef();
    IFACEMETHODIMP_(ULONG) Release();

    // IEnumIDList
    IFACEMETHODIMP Next(ULONG celt, PITEMID_CHILD *rgelt, ULONG *pceltFetched);
    IFACEMETHODIMP Skip(DWORD celt);
    IFACEMETHODIMP Reset();
    IFACEMETHODIMP Clone(IEnumIDList **ppenum);

    HRESULT Initialize();

private:
    ~CFolderViewImplEnumIDList();

    long m_cRef;
    DWORD m_grfFlags;
    int m_nItem;
    int m_nFolderIdx; //  0 == root, folder index otherwise
    ITEMDATA *m_paData;
    long m_aDataLen;

    CFolderViewImplFolder *m_pFolder;
};

HRESULT CFolderViewImplFolder_CreateInstance(REFIID riid, void **ppv)
{
    *ppv = NULL;
    CFolderViewImplFolder* pFolderViewImplShellFolder = new (std::nothrow) CFolderViewImplFolder(0, 0);
    HRESULT hr = pFolderViewImplShellFolder ? S_OK : E_OUTOFMEMORY;
    if (SUCCEEDED(hr))
    {
        hr = pFolderViewImplShellFolder->QueryInterface(riid, ppv);
        pFolderViewImplShellFolder->Release();
    }
    return hr;
}

CFolderViewImplFolder::CFolderViewImplFolder(int nFolderIdx, int nFileIdx)
  : m_cRef(1), m_nFolderIdx(nFolderIdx), m_nFileIdx(nFileIdx), m_pidl(NULL)
{
    DllAddRef();
}

CFolderViewImplFolder::~CFolderViewImplFolder()
{
    CoTaskMemFree(m_pidl);
    DllRelease();
}

HRESULT CFolderViewImplFolder::QueryInterface(REFIID riid, void **ppv)
{
    static const QITAB qit[] =
    {
        QITABENT(CFolderViewImplFolder, IShellFolder),
        QITABENT(CFolderViewImplFolder, IShellFolder2),
        QITABENT(CFolderViewImplFolder, IPersist),
        QITABENT(CFolderViewImplFolder, IPersistFolder),
        QITABENT(CFolderViewImplFolder, IPersistFolder2),
        QITABENT(CFolderViewImplFolder, IShellIconOverlay),
        QITABENT(CFolderViewImplFolder, IQueryInfo),
        QITABENT(CFolderViewImplFolder, IShellPropSheetExt),
        { 0 },
    };
    return QISearch(this, qit, riid, ppv);
}

ULONG CFolderViewImplFolder::AddRef()
{
    return InterlockedIncrement(&m_cRef);
}

ULONG CFolderViewImplFolder::Release()
{
    long cRef = InterlockedDecrement(&m_cRef);
    if (0 == cRef)
    {
        delete this;
    }
    return cRef;
}

//  Translates a display name into an item identifier list.
HRESULT CFolderViewImplFolder::ParseDisplayName(HWND hwnd, IBindCtx *pbc, PWSTR pszName,
                                                ULONG *pchEaten, PIDLIST_RELATIVE *ppidl, ULONG *pdwAttributes)
{
    UNREFERENCED_PARAMETER(hwnd);
    UNREFERENCED_PARAMETER(pbc);
    UNREFERENCED_PARAMETER(pszName);
    UNREFERENCED_PARAMETER(pchEaten);
    UNREFERENCED_PARAMETER(ppidl);
    UNREFERENCED_PARAMETER(pdwAttributes);
    HRESULT hr = E_INVALIDARG;
    /*
    if (NULL != pszName)
    {
        WCHAR szNameComponent[MAX_PATH] = {};

        // extract first component of the display name
        PWSTR pszNext = PathFindNextComponent(pszName);
        if (pszNext && *pszNext)
        {
            hr = StringCchCopyN(szNameComponent, ARRAYSIZE(szNameComponent), pszName, lstrlen(pszName) - lstrlen(pszNext));
        }
        else
        {
            hr = StringCchCopy(szNameComponent, ARRAYSIZE(szNameComponent), pszName);
        }

        if (SUCCEEDED(hr))
        {
            PathRemoveBackslash(szNameComponent);

            UINT uIndex = 0;
            hr = GetIndexFromDisplayString(szNameComponent, &uIndex);
            if (SUCCEEDED(hr))
            {
                BOOL fIsFolder = ISFOLDERFROMINDEX(uIndex);
                PIDLIST_RELATIVE pidlCurrent = NULL;
                hr = CreateChildID(szNameComponent, m_nLevel + 1, uIndex, fIsFolder, &pidlCurrent);
                if (SUCCEEDED(hr))
                {
                    // If there are more components to parse, delegate to the child folder to handle the rest.
                    if (pszNext && *pszNext)
                    {
                        // Bind to current item
                        IShellFolder *psf;
                        hr = BindToObject(pidlCurrent, pbc, IID_PPV_ARGS(&psf));
                        if (SUCCEEDED(hr))
                        {
                            PIDLIST_RELATIVE pidlNext = NULL;
                            hr = psf->ParseDisplayName(hwnd, pbc, pszNext, pchEaten, &pidlNext, pdwAttributes);
                            if (SUCCEEDED(hr))
                            {
                                *ppidl = ILCombine(pidlCurrent, pidlNext);
                                ILFree(pidlNext);
                            }
                            psf->Release();
                        }

                        ILFree(pidlCurrent);
                    }
                    else
                    {
                        // transfer ownership to caller
                        *ppidl = pidlCurrent;
                    }
                }
            }
        }
    }
    */
    return hr;
}

//  Allows a client to determine the contents of a folder by
//  creating an item identifier enumeration object and returning
//  its IEnumIDList interface. The methods supported by that
//  interface can then be used to enumerate the folder's contents.
HRESULT CFolderViewImplFolder::EnumObjects(HWND /* hwnd */, DWORD grfFlags, IEnumIDList **ppenumIDList)
{
    HRESULT hr;
    CFolderViewImplEnumIDList *penum = new (std::nothrow) CFolderViewImplEnumIDList(grfFlags, m_nFileIdx, this);
    hr = penum ? S_OK : E_OUTOFMEMORY;
    if (SUCCEEDED(hr))
    {
        hr = penum->Initialize();
        if (SUCCEEDED(hr))
        {
            hr = penum->QueryInterface(IID_PPV_ARGS(ppenumIDList));
        }
        penum->Release();
    }

    return hr;
}


//  Factory for handlers for the specified item.
HRESULT CFolderViewImplFolder::BindToObject(PCUIDLIST_RELATIVE pidl,
                                            IBindCtx *pbc, REFIID riid, void **ppv)
{
    *ppv = NULL;
    PCFVITEMID pMyObj = _IsValid(pidl);
    HRESULT hr = (pMyObj ? S_OK : E_INVALIDARG);
    if (SUCCEEDED(hr))
    {
        CFolderViewImplFolder* pCFolderViewImplFolder = new (std::nothrow) CFolderViewImplFolder(pMyObj->nFolderIdx, pMyObj->nFileIdx);
        hr = pCFolderViewImplFolder ? S_OK : E_OUTOFMEMORY;
        if (SUCCEEDED(hr))
        {
            // Initialize it.
            PITEMID_CHILD pidlFirst = ILCloneFirst(pidl);
            hr = pidlFirst ? S_OK : E_OUTOFMEMORY;
            if (SUCCEEDED(hr))
            {
                PIDLIST_ABSOLUTE pidlBind = ILCombine(m_pidl, pidlFirst);
                hr = pidlBind ? S_OK : E_OUTOFMEMORY;
                if (SUCCEEDED(hr))
                {
                    hr = pCFolderViewImplFolder->Initialize(pidlBind);
                    if (SUCCEEDED(hr))
                    {
                        PCUIDLIST_RELATIVE pidlNext = ILNext(pidl);
                        if (ILIsEmpty(pidlNext))
                        {
                            // If we're reached the end of the idlist, return the interfaces we support for this item.
                            // Other potential handlers to return include IPropertyStore, IStream, IStorage, etc.
                            hr = pCFolderViewImplFolder->QueryInterface(riid, ppv);
                        }
                        else
                        {
                            // Otherwise we delegate to our child folder to let it bind to the next level.
                            hr = pCFolderViewImplFolder->BindToObject(pidlNext, pbc, riid, ppv);
                        }
                    }
                    CoTaskMemFree(pidlBind);
                }
                ILFree(pidlFirst);
            }
            pCFolderViewImplFolder->Release();
        }
    }
    return hr;
}

HRESULT CFolderViewImplFolder::BindToStorage(PCUIDLIST_RELATIVE pidl,
                                             IBindCtx *pbc, REFIID riid, void **ppv)
{
    return BindToObject(pidl, pbc, riid, ppv);
}


//  Helper function to help compare relative IDs.
HRESULT _ILCompareRelIDs(IShellFolder *psfParent, PCUIDLIST_RELATIVE pidl1, PCUIDLIST_RELATIVE pidl2,
                         LPARAM lParam)
{
    HRESULT hr;
    PCUIDLIST_RELATIVE pidlRel1 = ILNext(pidl1);
    PCUIDLIST_RELATIVE pidlRel2 = ILNext(pidl2);

    if (ILIsEmpty(pidlRel1))
    {
        if (ILIsEmpty(pidlRel2))
        {
            hr = ResultFromShort(0);  // Both empty
        }
        else
        {
            hr = ResultFromShort(-1);   // 1 is empty, 2 is not.
        }
    }
    else
    {
        if (ILIsEmpty(pidlRel2))
        {
            hr = ResultFromShort(1);  // 2 is empty, 1 is not
        }
        else
        {
            // pidlRel1 and pidlRel2 point to something, so:
            //  (1) Bind to the next level of the IShellFolder
            //  (2) Call its CompareIDs to let it compare the rest of IDs.
            PIDLIST_RELATIVE pidlNext = ILCloneFirst(pidl1);    // pidl2 would work as well
            hr = pidlNext ? S_OK : E_OUTOFMEMORY;
            if (pidlNext)
            {
                IShellFolder *psfNext;
                hr = psfParent->BindToObject(pidlNext, NULL, IID_PPV_ARGS(&psfNext));
                if (SUCCEEDED(hr))
                {
                    // We do not want to pass the lParam is IShellFolder2 isn't supported.
                    // Although it isn't important for this example it shoud be considered
                    // if you are implementing this for other situations.
                    IShellFolder2 *psf2;
                    if (SUCCEEDED(psfNext->QueryInterface(&psf2)))
                    {
                        psf2->Release();  // We can use the lParam
                    }
                    else
                    {
                        lParam = 0;       // We can't use the lParam
                    }

                    // Also, the column mask will not be relevant and should never be passed.
                    hr = psfNext->CompareIDs((lParam & ~SHCIDS_COLUMNMASK), pidlRel1, pidlRel2);
                    psfNext->Release();
                }
                CoTaskMemFree(pidlNext);
            }
        }
    }
    return hr;
}

//  Called to determine the equivalence and/or sort order of two idlists.
HRESULT CFolderViewImplFolder::CompareIDs(LPARAM lParam, PCUIDLIST_RELATIVE pidl1, PCUIDLIST_RELATIVE pidl2)
{
    HRESULT hr;
    if (lParam & (SHCIDS_CANONICALONLY | SHCIDS_ALLFIELDS))
    {
        // First do a "canonical" comparison, meaning that we compare with the intent to determine item
        // identity as quickly as possible.  The sort order is arbitrary but it must be consistent.
        PWSTR psz1;
        hr = _GetName(pidl1, &psz1);
        if (SUCCEEDED(hr))
        {
            PWSTR psz2;
            hr = _GetName(pidl2, &psz2);
            if (SUCCEEDED(hr))
            {
                hr = ResultFromShort(StrCmp(psz1, psz2));
                CoTaskMemFree(psz2);
            }
            CoTaskMemFree(psz1);
        }

        // If we've been asked to do an all-fields comparison, test for any other fields that
        // may be different in an item that shares the same identity.  For example if the item
        // represents a file, the identity may be just the filename but the other fields contained
        // in the idlist may be file size and file modified date, and those may change over time.
        // In our example let's say that "level" is the data that could be different on the same item.
        if ((ResultFromShort(0) == hr) && (lParam & SHCIDS_ALLFIELDS))
        {
            int cLevel1 = 0, cLevel2 = 0;
            hr = _GetLevel(pidl1, &cLevel1);
            if (SUCCEEDED(hr))
            {
                hr = _GetLevel(pidl2, &cLevel2);
                if (SUCCEEDED(hr))
                {
                    hr = ResultFromShort(cLevel1 - cLevel2);
                }
            }
        }
    }
    else
    {
        // Compare child ids by column data (lParam & SHCIDS_COLUMNMASK).
        hr = ResultFromShort(0);
        switch (lParam & SHCIDS_COLUMNMASK)
        {
            case 0: // Column one, Name.
            {
                if (SUCCEEDED(hr))
                {
                    PWSTR psz1;
                    hr = _GetName(pidl1, &psz1);
                    if (SUCCEEDED(hr))
                    {
                        PWSTR psz2;
                        hr = _GetName(pidl2, &psz2);
                        if (SUCCEEDED(hr))
                        {
                            hr = ResultFromShort(StrCmp(psz1, psz2));
                            CoTaskMemFree(psz2);
                        }
                        CoTaskMemFree(psz1);
                    }
                }
                break;
            }
            case 1: // Column two, Size.
            {
                int nSize1 = 0, nSize2 = 0;
                hr = _GetSize(pidl1, &nSize1);
                if (SUCCEEDED(hr))
                {
                    hr = _GetSize(pidl2, &nSize2);
                    if (SUCCEEDED(hr))
                    {
                        hr = ResultFromShort(nSize1 - nSize2);
                    }
                }
                break;
            }
            default:
            {
                hr = ResultFromShort(1);
            }
        }
    }

    if (ResultFromShort(0) == hr)
    {
        // Continue on by binding to the next level.
        hr = _ILCompareRelIDs(this, pidl1, pidl2, lParam);
    }
    return hr;
}

//  Called by the Shell to create the View Object and return it.
HRESULT CFolderViewImplFolder::CreateViewObject(HWND hwnd, REFIID riid, void **ppv)
{
    *ppv = NULL;

    HRESULT hr = E_NOINTERFACE;
    if (riid == IID_IShellView)
    {
        SFV_CREATE csfv = { sizeof(csfv), 0 };
        hr = QueryInterface(IID_PPV_ARGS(&csfv.pshf));
        if (SUCCEEDED(hr))
        {
            // Add our callback to the SFV_CREATE.  This is optional.  We
            // are adding it so we can enable searching within our
            // namespace.
            hr = CFolderViewCB_CreateInstance(IID_PPV_ARGS(&csfv.psfvcb));
            if (SUCCEEDED(hr))
            {
                hr = SHCreateShellFolderView(&csfv, (IShellView**)ppv);
                csfv.psfvcb->Release();
            }
            csfv.pshf->Release();
        }
    }
    else if (riid == IID_IContextMenu)
    {
        // This is the background context menu for the folder itself, not the context menu on items within it.
        DEFCONTEXTMENU dcm = { hwnd, NULL, m_pidl, static_cast<IShellFolder2 *>(this), 0, NULL, NULL, 0, NULL };
        hr = SHCreateDefaultContextMenu(&dcm, riid, ppv);
    }
    else if (riid == IID_IExplorerCommandProvider)
    {
        CFolderViewCommandProvider *pProvider = new (std::nothrow) CFolderViewCommandProvider();
        hr = pProvider ? S_OK : E_OUTOFMEMORY;
        if (SUCCEEDED(hr))
        {
            hr = pProvider->QueryInterface(riid, ppv);
            pProvider->Release();
        }
    }
    else if (riid == IID_IDropTarget)
    {
        // TODO: Implement this!
        hr = E_NOINTERFACE;
    }
    return hr;
}

//  Retrieves the attributes of one or more file objects or subfolders.
HRESULT CFolderViewImplFolder::GetAttributesOf(UINT cidl, PCUITEMID_CHILD_ARRAY apidl, ULONG *rgfInOut)
{
    // If SFGAO_FILESYSTEM is returned, GetDisplayNameOf(SHGDN_FORPARSING) on that item MUST
    // return a filesystem path.
    HRESULT hr = E_INVALIDARG;
    if (1 == cidl)
    {
        int nLevel = 0;
        hr = _GetLevel(apidl[0], &nLevel);
        if (SUCCEEDED(hr))
        {
            BOOL fIsFolder = FALSE;
            hr = _GetFolderness(apidl[0], &fIsFolder);
            if (SUCCEEDED(hr))
            {
                DWORD dwAttribs = 0;
                if (fIsFolder)
                {
                    dwAttribs |= SFGAO_FOLDER;
                }
                if (nLevel < g_nMaxLevel)
                {
                    dwAttribs |= SFGAO_HASSUBFOLDER;
                }
                *rgfInOut &= dwAttribs;
            }
        }
    }
    else if (0 == cidl) {
      *rgfInOut = g_bRioConnected ?
        SFGAO_FOLDER | SFGAO_HASSUBFOLDER | /*SFGAO_GHOSTED |*/ SFGAO_REMOVABLE | SFGAO_DROPTARGET :
        /*SFGAO_FOLDER | SFGAO_HASSUBFOLDER |*/ SFGAO_GHOSTED | SFGAO_REMOVABLE | SFGAO_DROPTARGET | SFGAO_NONENUMERATED;
      hr = S_OK;
    }
    return hr;
}

//  Retrieves an OLE interface that can be used to carry out
//  actions on the specified file objects or folders.
HRESULT CFolderViewImplFolder::GetUIObjectOf(HWND hwnd, UINT cidl, PCUITEMID_CHILD_ARRAY apidl,
                                             REFIID riid, UINT * /* prgfInOut */, void **ppv)
{
    *ppv = NULL;
    HRESULT hr;

    if (riid == IID_IContextMenu)
    {
        // The default context menu will call back for IQueryAssociations to determine the
        // file associations with which to populate the menu.
        DEFCONTEXTMENU const dcm = { hwnd, NULL, m_pidl, static_cast<IShellFolder2 *>(this),
                               cidl, apidl, NULL, 0, NULL };
        (void)dcm;
        hr = SHCreateDefaultContextMenu(&dcm, riid, ppv);
    }
    else if (riid == IID_IExtractIconW)
    {
        IDefaultExtractIconInit *pdxi;
        hr = SHCreateDefaultExtractIcon(IID_PPV_ARGS(&pdxi));
        if (SUCCEEDED(hr))
        {
            BOOL fIsFolder = FALSE;
            hr = _GetFolderness(apidl[0], &fIsFolder);
            if (SUCCEEDED(hr))
            {
                // This refers to icon indices in shell32.  You can also supply custom icons or
                // register IExtractImage to support general images.
                hr = pdxi->SetNormalIcon(L"shell32.dll", fIsFolder ? 4 : 1);
            }
            if (SUCCEEDED(hr))
            {
                hr = pdxi->QueryInterface(riid, ppv);
            }
            pdxi->Release();
        }
    }
    else if (riid == IID_IDataObject)
    {
        hr = SHCreateDataObject(m_pidl, cidl, apidl, NULL, riid, ppv);
    }
    else if (riid == IID_IQueryAssociations)
    {
        BOOL fIsFolder = FALSE;
        hr = _GetFolderness(apidl[0], &fIsFolder);
        if (SUCCEEDED(hr))
        {
            // the type of the item can be determined here.  we default to "FolderViewSampleType", which has
            // a context menu registered for it.
            if (fIsFolder)
            {
                ASSOCIATIONELEMENT const rgAssocFolder[] =
                {
                    { ASSOCCLASS_PROGID_STR, NULL, L"FolderViewSampleType"},
                    { ASSOCCLASS_FOLDER, NULL, NULL},
                };
                hr = AssocCreateForClasses(rgAssocFolder, ARRAYSIZE(rgAssocFolder), riid, ppv);
            }
            else
            {
                ASSOCIATIONELEMENT const rgAssocItem[] =
                {
                    { ASSOCCLASS_PROGID_STR, NULL, L"FolderViewSampleType"},
                };
                hr = AssocCreateForClasses(rgAssocItem, ARRAYSIZE(rgAssocItem), riid, ppv);
            }
        }
    }
    else
    {
        hr = E_NOINTERFACE;
    }
    return hr;
}

//  Retrieves the display name for the specified file object or subfolder.
HRESULT CFolderViewImplFolder::GetDisplayNameOf(PCUITEMID_CHILD pidl, SHGDNF shgdnFlags, STRRET *pName)
{
    HRESULT hr = S_OK;
    if (shgdnFlags & SHGDN_FORPARSING)
    {
        WCHAR szDisplayName[MAX_PATH];
        if (shgdnFlags & SHGDN_INFOLDER)
        {
            // This form of the display name needs to be handled by ParseDisplayName.
            hr = _GetName(pidl, szDisplayName, ARRAYSIZE(szDisplayName));
        }
        else
        {
            PWSTR pszThisFolder;
            hr = SHGetNameFromIDList(m_pidl, (shgdnFlags & SHGDN_FORADDRESSBAR) ? SIGDN_DESKTOPABSOLUTEEDITING : SIGDN_DESKTOPABSOLUTEPARSING, &pszThisFolder);
            if (SUCCEEDED(hr))
            {
                StringCchCopy(szDisplayName, ARRAYSIZE(szDisplayName), pszThisFolder);
                StringCchCat(szDisplayName, ARRAYSIZE(szDisplayName), L"\\");

                WCHAR szName[MAX_PATH];
                hr = _GetName(pidl, szName, ARRAYSIZE(szName));
                if (SUCCEEDED(hr))
                {
                    StringCchCat(szDisplayName, ARRAYSIZE(szDisplayName), szName);
                }
                CoTaskMemFree(pszThisFolder);
            }
        }
        if (SUCCEEDED(hr))
        {
            hr = StringToStrRet(szDisplayName, pName);
        }
    }
    else
    {
        PWSTR pszName;
        hr = _GetName(pidl, &pszName);
        if (SUCCEEDED(hr))
        {
            hr = StringToStrRet(pszName, pName);
            CoTaskMemFree(pszName);
        }
    }
    return hr;
}

//  Sets the display name of a file object or subfolder, changing
//  the item identifier in the process.
HRESULT CFolderViewImplFolder::SetNameOf(HWND /* hwnd */, PCUITEMID_CHILD /* pidl */,
                                         PCWSTR /* pszName */,  DWORD /* uFlags */, PITEMID_CHILD *ppidlOut)
{
    HRESULT hr = E_NOTIMPL;
    *ppidlOut = NULL;
    return hr;
}

//  IPersist method
HRESULT CFolderViewImplFolder::GetClassID(CLSID *pClassID)
{
    *pClassID = CLSID_FolderViewImpl;
    return S_OK;
}

//  IPersistFolder method
HRESULT CFolderViewImplFolder::Initialize(PCIDLIST_ABSOLUTE pidl)
{
    m_pidl = ILCloneFull(pidl);
    PIDLIST_ABSOLUTE oldLast = (PIDLIST_ABSOLUTE)InterlockedExchangePointer((PVOID *)&g_lastRootPIDL, ILCloneFull(pidl));
    if (oldLast)
    {
      CoTaskMemFree(oldLast);
    }

    return m_pidl ? S_OK : E_FAIL;
}

//  IShellFolder2 methods
HRESULT CFolderViewImplFolder::EnumSearches(IEnumExtraSearch **ppEnum)
{
    *ppEnum = NULL;
    return E_NOINTERFACE;
}

//  Retrieves the default sorting and display column (indices from GetDetailsOf).
HRESULT CFolderViewImplFolder::GetDefaultColumn(DWORD /* dwRes */,
                                                ULONG *pSort,
                                                ULONG *pDisplay)
{
    *pSort = 0;
    *pDisplay = 0;
    return S_OK;
}

//  Retrieves the default state for a specified column.
HRESULT CFolderViewImplFolder::GetDefaultColumnState(UINT iColumn, SHCOLSTATEF *pcsFlags)
{
    HRESULT hr = (iColumn < 3) ? S_OK : E_INVALIDARG;
    if (SUCCEEDED(hr))
    {
        *pcsFlags = SHCOLSTATE_ONBYDEFAULT | SHCOLSTATE_TYPE_STR;
    }
    return hr;
}

//  Requests the GUID of the default search object for the folder.
HRESULT CFolderViewImplFolder::GetDefaultSearchGUID(GUID * /* pguid */)
{
    return E_NOTIMPL;
}

//  Helper function for getting the display name for a column.
//  IMPORTANT: If cch is set to 0 the value is returned in the VARIANT.
HRESULT CFolderViewImplFolder::_GetColumnDisplayName(PCUITEMID_CHILD pidl,
                                                     const PROPERTYKEY *pkey,
                                                     VARIANT *pv,
                                                     PWSTR pszRet,
                                                     UINT cch)
{
    BOOL fIsFolder = FALSE;
    HRESULT hr = _GetFolderness(pidl, &fIsFolder);
    if (SUCCEEDED(hr))
    {
        if (IsEqualPropertyKey(*pkey, PKEY_ItemNameDisplay))
        {
            PWSTR pszName;
            hr = _GetName(pidl, &pszName);
            if (SUCCEEDED(hr))
            {
                if (pv != NULL)
                {
                    pv->vt = VT_BSTR;
                    pv->bstrVal = SysAllocString(pszName);
                    hr = pv->bstrVal ? S_OK : E_OUTOFMEMORY;
                }
                else
                {
                    hr = StringCchCopy(pszRet, cch, pszName);
                }
                CoTaskMemFree(pszName);
            }
        }
        else if ((IsEqualPropertyKey(*pkey, PKEY_Microsoft_SDKSample_AreaSize)) &&
                 (!fIsFolder))
        {
            int nSize = 0;
            hr = _GetSize(pidl, &nSize);
            if (SUCCEEDED(hr))
            {
                //This property is declared as "String" type.  See:  RioShellEx.propdesc
                WCHAR szFormattedSize[12];
                hr = StringCchPrintf(szFormattedSize, ARRAYSIZE(szFormattedSize), L"%d Sq. Ft.", nSize);
                if (cch)
                {
                    hr = StringCchCopy(pszRet, cch, szFormattedSize);
                }
                else
                {
                    pv->vt = VT_BSTR;
                    pv->bstrVal = SysAllocString(szFormattedSize);
                    hr = pv->bstrVal ? S_OK : E_OUTOFMEMORY;
                }
            }
        }
        else if (IsEqualPropertyKey(*pkey, PKEY_Microsoft_SDKSample_DirectoryLevel))
        {
            int nLevel = 0;
            hr = _GetLevel(pidl, &nLevel);
            if (SUCCEEDED(hr))
            {
                if (cch)
                {
                    hr = StringCchPrintf(pszRet, cch, L"%d", nLevel);
                }
                else
                {
                    pv->vt = VT_I4;
                    pv->lVal = nLevel;
                }
            }
        }
        else
        {
            if (pv)
            {
                VariantInit(pv);
            }

            if (pszRet)
            {
                *pszRet = '\0';
            }
        }
    }
    return hr;
}

//  Retrieves detailed information, identified by a
//  property set ID (FMTID) and property ID (PID),
//  on an item in a Shell folder.
HRESULT CFolderViewImplFolder::GetDetailsEx(PCUITEMID_CHILD pidl,
                                            const PROPERTYKEY *pkey,
                                            VARIANT *pv)
{
    BOOL pfIsFolder = FALSE;
    HRESULT hr = _GetFolderness(pidl, &pfIsFolder);
    if (SUCCEEDED(hr))
    {
        if (!pfIsFolder && IsEqualPropertyKey(*pkey, PKEY_PropList_PreviewDetails))
        {
            // This proplist indicates what properties are shown in the details pane at the bottom of the explorer browser.
            pv->vt = VT_BSTR;
            pv->bstrVal = SysAllocString(L"prop:Microsoft.SDKSample.AreaSize;Microsoft.SDKSample.NumberOfSides;Microsoft.SDKSample.DirectoryLevel");
            hr = pv->bstrVal ? S_OK : E_OUTOFMEMORY;
        }
        else
        {
            hr = _GetColumnDisplayName(pidl, pkey, pv, NULL, 0);
        }
    }
    return hr;
}

//  Retrieves detailed information, identified by a
//  column index, on an item in a Shell folder.
HRESULT CFolderViewImplFolder::GetDetailsOf(PCUITEMID_CHILD pidl,
                                            UINT iColumn,
                                            SHELLDETAILS *pDetails)
{
    PROPERTYKEY key;
    HRESULT hr = MapColumnToSCID(iColumn, &key);
    pDetails->cxChar = 24;
    WCHAR szRet[MAX_PATH];

    if (!pidl)
    {
        // No item means we're returning information about the column itself.
        switch (iColumn)
        {
        case 0:
            pDetails->fmt = LVCFMT_LEFT;
            hr = StringCchCopy(szRet, ARRAYSIZE(szRet), L"Name");
            break;
        case 1:
            pDetails->fmt = LVCFMT_CENTER;
            hr = StringCchCopy(szRet, ARRAYSIZE(szRet), L"Size");
            break;
        case 2:
            pDetails->fmt = LVCFMT_CENTER;
            hr = StringCchCopy(szRet, ARRAYSIZE(szRet), L"Sides");
            break;
        case 3:
            pDetails->fmt = LVCFMT_CENTER;
            hr = StringCchCopy(szRet, ARRAYSIZE(szRet), L"Level");
            break;
        default:
            // GetDetailsOf is called with increasing column indices until failure.
            hr = E_FAIL;
            break;
        }
    }
    else if (SUCCEEDED(hr))
    {
        hr = _GetColumnDisplayName(pidl, &key, NULL, szRet, ARRAYSIZE(szRet));
    }

    if (SUCCEEDED(hr))
    {
        hr = StringToStrRet(szRet, &pDetails->str);
    }

    return hr;
}

//  Converts a column name to the appropriate
//  property set ID (FMTID) and property ID (PID).
HRESULT CFolderViewImplFolder::MapColumnToSCID(UINT iColumn, PROPERTYKEY *pkey)
{
    // The property keys returned here are used by the categorizer.
    HRESULT hr = S_OK;
    switch (iColumn)
    {
    case 0:
        *pkey = PKEY_ItemNameDisplay;
        break;
    case 1:
        *pkey = PKEY_Microsoft_SDKSample_AreaSize;
        break;
    case 2:
        *pkey = PKEY_Microsoft_SDKSample_NumberOfSides;
        break;
    case 3:
        *pkey = PKEY_Microsoft_SDKSample_DirectoryLevel;
        break;
    default:
        hr = E_FAIL;
        break;
    }
    return hr;
}

//IPersistFolder2 methods
//  Retrieves the PIDLIST_ABSOLUTE for the folder object.
HRESULT CFolderViewImplFolder::GetCurFolder(PIDLIST_ABSOLUTE *ppidl)
{
    *ppidl = NULL;
    HRESULT hr = m_pidl ? S_OK : E_FAIL;
    if (SUCCEEDED(hr))
    {
        *ppidl = ILCloneFull(m_pidl);
        hr = *ppidl ? S_OK : E_OUTOFMEMORY;
    }
    return hr;
}

// IShellIconOverlay
HRESULT CFolderViewImplFolder::GetOverlayIndex(PCUITEMID_CHILD pidl, int *pIndex)
{
  UNREFERENCED_PARAMETER(pidl);
  UNREFERENCED_PARAMETER(pIndex);
  /*
  if (!g_bRioConnected)
  {
    *pIndex = SHGetIconOverlayIndex(NULL, IDO_SHGIOI_SLOWFILE);
    return S_OK;
  }
  */
  return S_FALSE;
}

HRESULT CFolderViewImplFolder::GetOverlayIconIndex(PCUITEMID_CHILD pidl, int *pIconIndex)
{
  UNREFERENCED_PARAMETER(pidl);
  UNREFERENCED_PARAMETER(pIconIndex);
  /*
  if (!g_bRioConnected)
  {
    *pIconIndex = INDEXTOOVERLAYMASK(SHGetIconOverlayIndex(NULL, IDO_SHGIOI_SLOWFILE));
    return S_OK;
  }
  */
  return S_FALSE;
}

HRESULT CFolderViewImplFolder::GetInfoFlags(DWORD *pdwFlags)
{
  (void)pdwFlags;
  return E_NOTIMPL;
}

HRESULT CFolderViewImplFolder::GetInfoTip(DWORD dwFlags, PWSTR *ppwszTip)
{
  if (dwFlags == QITIPF_DEFAULT)
  {
    SHStrDup(L"Whatup?", ppwszTip);
  }
  else
  {
    *ppwszTip = NULL;
  }
  return S_OK;
}

HRESULT CFolderViewImplFolder::AddPages(LPFNADDPROPSHEETPAGE pfnAddPage, LPARAM lParam)
{
  UNREFERENCED_PARAMETER(pfnAddPage);
  UNREFERENCED_PARAMETER(lParam);
  return S_OK;
}

HRESULT CFolderViewImplFolder::ReplacePage(UINT uPageID, LPFNADDPROPSHEETPAGE pfnReplacePage, LPARAM lParam)
{
  UNREFERENCED_PARAMETER(uPageID);
  UNREFERENCED_PARAMETER(pfnReplacePage);
  UNREFERENCED_PARAMETER(lParam);
  return E_NOTIMPL;
}

// Item idlists passed to folder methods are guaranteed to have accessible memory as specified
// by the cbSize in the itemid.  However they may be loaded from a persisted form (for example
// shortcuts on disk) where they could be corrupted.  It is the shell folder's responsibility
// to make sure it's safe against corrupted or malicious itemids.
PCFVITEMID CFolderViewImplFolder::_IsValid(PCUIDLIST_RELATIVE pidl)
{
    PCFVITEMID pidmine = NULL;
    if (pidl)
    {
        pidmine = (PCFVITEMID)pidl;
        if (!(pidmine->cb && MYOBJID == pidmine->MyObjID))
        {
            pidmine = NULL;
        }
    }
    return pidmine;
}

HRESULT CFolderViewImplFolder::_GetName(PCUIDLIST_RELATIVE pidl, PWSTR pszName, int cchMax)
{
    PCFVITEMID pMyObj = _IsValid(pidl);
    HRESULT hr = pMyObj ? S_OK : E_INVALIDARG;
    if (SUCCEEDED(hr))
    {
        // StringCchCopy requires aligned strings, and itemids are not necessarily aligned.
        int i = 0;
        for ( ; i < cchMax; i++)
        {
            pszName[i] = pMyObj->szName[i];
            if (0 == pszName[i])
            {
                break;
            }
        }

        // Make sure the string is null-terminated.
        if (i == cchMax)
        {
            pszName[cchMax - 1] = 0;
        }
    }
    return hr;
}

HRESULT CFolderViewImplFolder::_GetName(PCUIDLIST_RELATIVE pidl, PWSTR *ppsz)
{
    *ppsz = 0;
    PCFVITEMID pMyObj = _IsValid(pidl);
    HRESULT hr = pMyObj ? S_OK : E_INVALIDARG;
    if (SUCCEEDED(hr))
    {
        int const cch = pMyObj->cchName;
        *ppsz = (PWSTR)CoTaskMemAlloc(cch * sizeof(**ppsz));
        hr = *ppsz ? S_OK : E_OUTOFMEMORY;
        if (SUCCEEDED(hr))
        {
            hr = _GetName(pidl, *ppsz, cch);
        }
    }
    return hr;
}


HRESULT CFolderViewImplFolder::_GetLevel(PCUIDLIST_RELATIVE pidl, int *pLevel)
{
    PCFVITEMID pMyObj = _IsValid(pidl);
    HRESULT hr = pMyObj ? S_OK : E_INVALIDARG;
    if (SUCCEEDED(hr))
    {
        if (pMyObj->nFolderIdx == 0)
        {
            *pLevel = 0;
        }
        else if (pMyObj->nFileIdx == 0)
        {
            *pLevel = 1;
        }
        else
        {
            *pLevel = 2;
        }
    }
    else  // If this fails we are at level zero.
    {
        *pLevel = 0;
    }
    return hr;
}

HRESULT CFolderViewImplFolder::_GetSize(PCUIDLIST_RELATIVE pidl, int *pSize)
{
    PCFVITEMID pMyObj = _IsValid(pidl);
    HRESULT hr = pMyObj ? S_OK : E_INVALIDARG;
    if (SUCCEEDED(hr))
    {
        *pSize = pMyObj->nSize;
    }
    return hr;
}

HRESULT CFolderViewImplFolder::_GetFolderness(PCUIDLIST_RELATIVE pidl, BOOL *pfIsFolder)
{
    PCFVITEMID pMyObj = _IsValid(pidl);
    HRESULT hr = pMyObj ? S_OK : E_INVALIDARG;
    if (SUCCEEDED(hr))
    {
        *pfIsFolder = pMyObj->fIsFolder;
    }
    return hr;
}

HRESULT CFolderViewImplFolder::_ValidatePidl(PCUIDLIST_RELATIVE pidl)
{
    PCFVITEMID pMyObj = _IsValid(pidl);
    return pMyObj ? S_OK : E_INVALIDARG;
}

HRESULT CFolderViewImplFolder::CreateChildID(PCWSTR pszName, int nFolderIdx, int nFileIdx, int nSize, BOOL fIsFolder, PITEMID_CHILD *ppidl)
{
    // Sizeof an object plus the next cb plus the characters in the string.
    UINT nIDSize = sizeof(FVITEMID) +
                   sizeof(USHORT) +
                   (lstrlen(pszName) * sizeof(WCHAR)) +
                   sizeof(WCHAR);

    // Allocate and zero the memory.
    FVITEMID *lpMyObj = (FVITEMID *)CoTaskMemAlloc(nIDSize);

    HRESULT hr = lpMyObj ? S_OK : E_OUTOFMEMORY;
    if (SUCCEEDED(hr))
    {
        ZeroMemory(lpMyObj, nIDSize);
        lpMyObj->cb = static_cast<short>(nIDSize - sizeof(lpMyObj->cb));
        lpMyObj->MyObjID    = MYOBJID;
        lpMyObj->cchName    = (BYTE)(lstrlen(pszName) + 1);
        lpMyObj->nFolderIdx = (WORD)nFolderIdx;
        lpMyObj->nFileIdx   = (WORD)nFileIdx;
        lpMyObj->nSize      = (BYTE)nSize;
        lpMyObj->fIsFolder  = (BOOL)fIsFolder;

        hr = StringCchCopy(lpMyObj->szName, lpMyObj->cchName, pszName);
        if (SUCCEEDED(hr))
        {
            *ppidl = (PITEMID_CHILD)lpMyObj;
        }
    }
    return hr;
}


CFolderViewImplEnumIDList::CFolderViewImplEnumIDList(DWORD grfFlags, int nFolderIdx, CFolderViewImplFolder *pFolderViewImplShellFolder) :
    m_cRef(1), m_grfFlags(grfFlags), m_nFolderIdx(nFolderIdx), m_nItem(0), m_pFolder(pFolderViewImplShellFolder), m_paData(NULL), m_aDataLen(0)
{
    m_pFolder->AddRef();
}

CFolderViewImplEnumIDList::~CFolderViewImplEnumIDList()
{
    m_pFolder->Release();
    CoTaskMemFree(m_paData);
}

HRESULT CFolderViewImplEnumIDList::QueryInterface(REFIID riid, void **ppv)
{
    static const QITAB qit[] = {
        QITABENT (CFolderViewImplEnumIDList, IEnumIDList),
        { 0 },
    };
    return QISearch(this, qit, riid, ppv);
}

ULONG CFolderViewImplEnumIDList::AddRef()
{
    return InterlockedIncrement(&m_cRef);
}

ULONG CFolderViewImplEnumIDList::Release()
{
    long cRef = InterlockedDecrement(&m_cRef);
    if (0 == cRef)
    {
        delete this;
    }
    return cRef;
}

// This initializes the enumerator.  In this case we set up a default array of items, this represents our
// data source.  In a real-world implementation the array would be replaced and would be backed by some
// other data source that you traverse and convert into IShellFolder item IDs.
HRESULT CFolderViewImplEnumIDList::Initialize()
{
    Rio500Remix::IRio500 *pRio;
    HRESULT hr = CoCreateInstance(
        Rio500Remix::CLSID_Rio500,
        NULL,
        CLSCTX_INPROC_SERVER,
        Rio500Remix::IID_IRio500,
        (LPVOID *)&pRio);
    if (FAILED(hr))
    {
        return hr;
    }

    if (pRio)
    {
        hr = pRio->Open();
        if (FAILED(hr)) {
            WCHAR buf[16];
            _itow_s(hr, buf, 16);
            wcscat_s(buf, L"\n");
            OutputDebugString(buf);
            return hr;
        }

        if (m_nFolderIdx == 0)
        {
            Rio500Remix::IFolders *pFolders;
            hr = pRio->get_Folders(0, &pFolders);
            if (FAILED(hr))
            {
                return hr;
            }
            hr = pFolders->get_Count(&m_aDataLen);
            if (SUCCEEDED(hr) && m_aDataLen > 0)
            {
                m_paData = (ITEMDATA *)CoTaskMemAlloc(m_aDataLen * sizeof(ITEMDATA));
                for (long i = 0; i < m_aDataLen; i++)
                {
                    Rio500Remix::IFolder *pFolder;
                    if (SUCCEEDED(pFolders->get_Item(i + 1, &pFolder)))
                    {
                        BSTR bstrName;
                        if (SUCCEEDED(pFolder->get_Name(&bstrName)))
                        {
                            wcscpy_s(m_paData[i].szName, bstrName);
                            m_paData[i].nSize = i;
                            m_paData[i].fIsFolder = TRUE;

                            SysFreeString(bstrName);
                        }
                    }
                }
            }
        }
        else
        {
            Rio500Remix::ISongs *pSongs;
            hr = pRio->get_Songs(0, m_nFolderIdx - 1, &pSongs);
            if (FAILED(hr))
            {
                return hr;
            }
            hr = pSongs->get_Count(&m_aDataLen);
            if (SUCCEEDED(hr) && m_aDataLen > 0)
            {
                m_paData = (ITEMDATA *)CoTaskMemAlloc(m_aDataLen * sizeof(ITEMDATA));
                for (long i = 0; i < m_aDataLen; i++)
                {
                    Rio500Remix::ISong *pSong;
                    if (SUCCEEDED(pSongs->get_Item(i + 1, &pSong)))
                    {
                        BSTR bstrName;
                        if (SUCCEEDED(pSong->get_Name(&bstrName)))
                        {
                            wcscpy_s(m_paData[i].szName, bstrName);
                            m_paData[i].nSize = i;
                            m_paData[i].fIsFolder = FALSE;

                            SysFreeString(bstrName);
                        }
                    }
                }
            }
        }

        pRio->Close();
    }
    return hr;
}

// Retrieves the specified number of item identifiers in
// the enumeration sequence and advances the current position
// by the number of items retrieved.
HRESULT CFolderViewImplEnumIDList::Next(ULONG celt, PITEMID_CHILD *rgelt, ULONG *pceltFetched)
{
    ULONG celtFetched = 0;

    HRESULT hr = (pceltFetched || celt <= 1) ? S_OK : E_INVALIDARG;
    if (SUCCEEDED(hr))
    {
        ULONG i = 0;
        while (SUCCEEDED(hr) && i < celt && m_nItem < m_aDataLen)
        {
            BOOL fSkip = FALSE;
            if (!(m_grfFlags & SHCONTF_STORAGE))
            {
                if (m_paData[m_nItem].fIsFolder)
                {
                    if (!(m_grfFlags & SHCONTF_FOLDERS))
                    {
                        // this is a folder, but caller doesnt want folders
                        fSkip = TRUE;
                    }
                }
                else
                {
                    if (!(m_grfFlags & SHCONTF_NONFOLDERS))
                    {
                        // this is a file, but caller doesnt want files
                        fSkip = TRUE;
                    }
                }
            }

            if (!fSkip)
            {
                hr = m_pFolder->CreateChildID(m_paData[m_nItem].szName, m_nFolderIdx, m_nItem + 1, m_paData[m_nItem].nSize, m_paData[m_nItem].fIsFolder, &rgelt[i]);
                if (SUCCEEDED(hr))
                {
                    celtFetched++;
                    i++;
                }
            }

            m_nItem++;
        }
    }

    if (pceltFetched)
    {
        *pceltFetched = celtFetched;
    }

    return (celtFetched == celt) ? S_OK : S_FALSE;
}

HRESULT CFolderViewImplEnumIDList::Skip(DWORD celt)
{
    m_nItem += celt;
    return S_OK;
}

HRESULT CFolderViewImplEnumIDList::Reset()
{
    m_nItem = 0;
    return S_OK;
}

HRESULT CFolderViewImplEnumIDList::Clone(IEnumIDList **ppenum)
{
    // this method is rarely used and it's acceptable to not implement it.
    *ppenum = NULL;
    return E_NOTIMPL;
}


class CFolderViewCB : public IShellFolderViewCB,
                      public IFolderViewSettings
{
public:
    CFolderViewCB() : _cRef(1) { }

    // IUnknown
    IFACEMETHODIMP QueryInterface(REFIID riid, void **ppv)
    {
        static const QITAB qit[] =
        {
            QITABENT(CFolderViewCB, IShellFolderViewCB),
            QITABENT(CFolderViewCB, IFolderViewSettings),
            { 0 },
        };
        return QISearch(this, qit, riid, ppv);
    }

    IFACEMETHODIMP_(ULONG) AddRef() { return InterlockedIncrement(&_cRef); }
    IFACEMETHODIMP_(ULONG) Release()
    {
        long cRef = InterlockedDecrement(&_cRef);
        if (0 == cRef)
        {
            delete this;
        }
        return cRef;
    }

    // IShellFolderViewCB
    IFACEMETHODIMP MessageSFVCB(UINT /* uMsg */, WPARAM /* wParam */, LPARAM /* lParam */)
        { return E_NOTIMPL; }

    // IFolderViewSettings
    IFACEMETHODIMP GetColumnPropertyList(REFIID /* riid */, void **ppv)
        { *ppv = NULL; return E_NOTIMPL; }
    IFACEMETHODIMP GetGroupByProperty(PROPERTYKEY * /* pkey */, BOOL * /* pfGroupAscending */)
        { return E_NOTIMPL; }
    IFACEMETHODIMP GetViewMode(FOLDERLOGICALVIEWMODE * /* plvm */)
        { return E_NOTIMPL; }
    IFACEMETHODIMP GetIconSize(UINT * /* puIconSize */)
        { return E_NOTIMPL; }

    IFACEMETHODIMP GetFolderFlags(FOLDERFLAGS *pfolderMask, FOLDERFLAGS *pfolderFlags);

    IFACEMETHODIMP GetSortColumns(SORTCOLUMN * /* rgSortColumns */, UINT /* cColumnsIn */, UINT * /* pcColumnsOut */)
        { return E_NOTIMPL; }
    IFACEMETHODIMP GetGroupSubsetCount(UINT * /* pcVisibleRows */)
        { return E_NOTIMPL; }

private:
    ~CFolderViewCB() { };
    long _cRef;
};

// IFolderViewSettings
IFACEMETHODIMP CFolderViewCB::GetFolderFlags(FOLDERFLAGS *pfolderMask, FOLDERFLAGS *pfolderFlags)
{
    if (pfolderMask)
    {
        *pfolderMask = FWF_USESEARCHFOLDER;
    }

    if (pfolderFlags)
    {
        *pfolderFlags = FWF_USESEARCHFOLDER;
    }

    return S_OK;
}

HRESULT CFolderViewCB_CreateInstance(REFIID riid, void **ppv)
{
    *ppv = NULL;

    HRESULT hr = E_OUTOFMEMORY;
    CFolderViewCB *pfvcb = new (std::nothrow) CFolderViewCB();
    if (pfvcb)
    {
        hr = pfvcb->QueryInterface(riid, ppv);
        pfvcb->Release();
    }
    return hr;
}
