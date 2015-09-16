#include "IconProvider.h"

HRESULT IconProvider::GetIconForFileName(LPCWSTR wszFileName, LPWSTR wszIconPath, SIZE_T dwIconPath, int *piIcon)
{
    PWSTR wszLastDot = StrRChr(wszFileName, NULL, L'.');
    if (!wszLastDot)
    {
        return E_INVALIDARG;
    }

    // TODO: Cache extension -> path/index associations

    WCHAR wszBuffer1[_MAX_PATH];
    DWORD cbBuffer = sizeof(wszBuffer1);
    if (RegGetValue(
        HKEY_CLASSES_ROOT,
        wszLastDot,
        NULL,
        RRF_RT_REG_SZ,
        NULL,
        wszBuffer1,
        &cbBuffer) != ERROR_SUCCESS) {
        return E_NOT_SET;
    }
    StrCat(wszBuffer1, L"\\DefaultIcon");

    WCHAR wszBuffer2[_MAX_PATH];
    cbBuffer = sizeof(wszBuffer2);
    if (RegGetValue(
        HKEY_CLASSES_ROOT,
        wszBuffer1,
        NULL,
        RRF_RT_REG_SZ,
        NULL,
        wszBuffer2,
        &cbBuffer) != ERROR_SUCCESS) {
        return E_NOT_SET;
    }

    PWSTR wszLastComma = StrRChr(wszBuffer2, NULL, L',');
    if (!wszLastComma)
    {
        *piIcon = 0;
    }
    else
    {
        *wszLastComma = 0;
        *piIcon = _wtoi(wszLastComma + 1);
    }

    if (wcslen(wszBuffer2) + 1 > dwIconPath)
    {
        return E_INVALIDARG;
    }
    StrCpy(wszIconPath, wszBuffer2);
    return S_OK;
}
