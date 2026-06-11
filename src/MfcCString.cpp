#include "mfc.h"

// IWD2.exe contains the VC6 static-MFC implementation. The VS2019 rebuild uses
// _AFXDLL, where CString is supplied by the MFC DLL instead.
#if _MSC_VER == 1200 && !defined(_AFXDLL)

// 0x7FCC1A
CString::~CString()
{
    if (GetData() != afxDataNil) {
        if (InterlockedDecrement(&GetData()->nRefs) <= 0) {
            FreeData(GetData());
        }
    }
}

#endif
