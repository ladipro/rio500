#pragma once

#include <windows.h>
#include <shlobj.h>
#include <propkey.h>
#include <shlwapi.h>
#include <strsafe.h>

class IconProvider
{
public:
    // Figure out icon path & index for the given file name
    HRESULT GetIconForFileName(LPCWSTR wszFileName, LPWSTR wszIconPath, SIZE_T dwIconPath, int *piIcon);
};
