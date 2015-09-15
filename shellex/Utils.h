/**************************************************************************
THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
PARTICULAR PURPOSE.

(c) Microsoft Corporation. All Rights Reserved.
**************************************************************************/

#pragma once

#define ISFOLDERFROMINDEX(u) (BOOL)(u % 2)

HRESULT LoadFolderViewImplDisplayString(UINT uIndex, PWSTR psz, UINT cch);
HRESULT GetIndexFromDisplayString(PCWSTR psz, UINT *puIndex);
STDAPI StringToStrRet(PCWSTR pszName, STRRET *pStrRet);
HRESULT DisplayItem(IShellItemArray *psia, HWND hwndParent);

#ifndef ResultFromShort
#define ResultFromShort(i)      MAKE_HRESULT(SEVERITY_SUCCESS, 0, (USHORT)(i))
#endif

extern HINSTANCE g_hInst;

void DllAddRef();
void DllRelease();
