#include "CChitin3d.h"

#include "CChitin.h"

#include <mbstring.h>
#include <winver.h>

static void InitOpenGL(HMODULE hOpenGL);

// 0xA0E170
PFNSWAPBUFFERSPROC CChitin3d::SwapBuffers;

// 0xA0E174
PFNWGLMAKECURRENTPROC CChitin3d::wglMakeCurrent;

// 0xA0E178
PFNWGLCREATECONTEXTPROC CChitin3d::wglCreateContext;

// 0xA0E17C
PFNWGLGETCURRENTDCPROC CChitin3d::wglGetCurrentDC;

// 0xA0E180
PFNWGLDELETECONTEXTPROC CChitin3d::wglDeleteContext;

// 0xA0E184
PFNSETPIXELFORMATPROC CChitin3d::SetPixelFormat;

// 0xA0E188
PFNCHOOSEPIXELFORMATPROC CChitin3d::ChoosePixelFormat;

// -----------------------------------------------------------------------------

// GLSetup driver detection -- forward decl + active-driver state used by Init3d
// (full definitions are below, after InitOpenGL).
static int GLSetupSelectDriver(unsigned int nDriver);

// Resolved WGL entry points of the driver selected via GLSetup (written by the
// GL capability probe, read by Init3d).
// 0x9074C8
static PFNSWAPBUFFERSPROC      g_pfnGLSetupSwapBuffers;
// 0x9074D0
static PFNWGLCREATECONTEXTPROC g_pfnGLSetupWglCreateContext;
// 0x9074D8
static PFNWGLDELETECONTEXTPROC g_pfnGLSetupWglDeleteContext;
// 0x9074DC
static PFNWGLGETCURRENTDCPROC  g_pfnGLSetupWglGetCurrentDC;
// 0x9074E4
static PFNWGLMAKECURRENTPROC   g_pfnGLSetupWglMakeCurrent;

// -----------------------------------------------------------------------------

// #binary-identical
// 0x7C8980
void CChitin::InitVariables3D()
{
    m_hOpenGL = 0;
    field_2F4 = 0;
}

// 0x7C8990
BOOL CChitin::Init3d()
{
    // GLSetup: if enabled, pick and probe a detected OpenGL driver and adopt its
    // resolved WGL entry points; otherwise fall back to a plain opengl32.dll.
    if (GetPrivateProfileIntA("Program Options", "Use GLSetup", 1, GetIniFileName()) != 0) {
        int nDriver = GetPrivateProfileIntA("Program Options", "GLSetup Driver", 0, GetIniFileName());
        if (GLSetupSelectDriver(nDriver) == 0) {
            CChitin3d::SwapBuffers = g_pfnGLSetupSwapBuffers;
            CChitin3d::wglMakeCurrent = g_pfnGLSetupWglMakeCurrent;
            CChitin3d::wglCreateContext = g_pfnGLSetupWglCreateContext;
            CChitin3d::wglGetCurrentDC = g_pfnGLSetupWglGetCurrentDC;
            CChitin3d::wglDeleteContext = g_pfnGLSetupWglDeleteContext;
            CChitin3d::SetPixelFormat = SetPixelFormat;
            CChitin3d::ChoosePixelFormat = ChoosePixelFormat;
            return TRUE;
        }
    }

    CString sMessage;
    sMessage.LoadStringA(GetIDSOpenGLDll());

    m_hOpenGL = LoadLibraryA("opengl32.dll");
    sMessage += "opengl32.dll";

    if (m_hOpenGL == NULL) {
        MessageBoxA(NULL, sMessage, m_sGameName, 0);
        return FALSE;
    }

    InitOpenGL(m_hOpenGL);

    return TRUE;
}

// 0x7C8B10
void CChitin::Shutdown3D()
{
    // NOTE: Original code is slightly different - it checks for current
    // driver's `wglCreateContext`.
    if (m_hOpenGL) {
        FreeLibrary(m_hOpenGL);
    }
}

// -----------------------------------------------------------------------------

// 0x7D8E80
void InitOpenGL(HMODULE hOpenGL)
{
    CChitin3d::wglCreateContext = (PFNWGLCREATECONTEXTPROC)GetProcAddress(hOpenGL, "wglCreateContext");
    CChitin3d::wglDeleteContext = (PFNWGLDELETECONTEXTPROC)GetProcAddress(hOpenGL, "wglDeleteContext");
    CChitin3d::wglMakeCurrent = (PFNWGLMAKECURRENTPROC)GetProcAddress(hOpenGL, "wglMakeCurrent");
    CChitin3d::wglGetCurrentDC = (PFNWGLGETCURRENTDCPROC)GetProcAddress(hOpenGL, "wglGetCurrentDC");
    CChitin3d::ChoosePixelFormat = ChoosePixelFormat;
    CChitin3d::SetPixelFormat = SetPixelFormat;
    CChitin3d::SwapBuffers = SwapBuffers;
    CVideo3d::glBegin = (PFNGLBEGINPROC)GetProcAddress(hOpenGL, "glBegin");
    CVideo3d::glBindTexture = (PFNGLBINDTEXTUREPROC)GetProcAddress(hOpenGL, "glBindTexture");
    CVideo3d::glBlendFunc = (PFNGLBLENDFUNCPROC)GetProcAddress(hOpenGL, "glBlendFunc");
    CVideo3d::glClear = (PFNGLCLEARPROC)GetProcAddress(hOpenGL, "glClear");
    CVideo3d::glClearColor = (PFNGLCLEARCOLORPROC)GetProcAddress(hOpenGL, "glClearColor");
    CVideo3d::glClearDepth = (PFNGLCLEARDEPTHPROC)GetProcAddress(hOpenGL, "glClearDepth");
    CVideo3d::glColor4f = (PFNGLCOLOR4FPROC)GetProcAddress(hOpenGL, "glColor4f");
    CVideo3d::glCullFace = (PFNGLCULLFACEPROC)GetProcAddress(hOpenGL, "glCullFace");
    CVideo3d::glDeleteTextures = (PFNGLDELETETEXTURESPROC)GetProcAddress(hOpenGL, "glDeleteTextures");
    CVideo3d::glDisable = (PFNGLDISABLEPROC)GetProcAddress(hOpenGL, "glDisable");
    CVideo3d::glEnable = (PFNGLENABLEPROC)GetProcAddress(hOpenGL, "glEnable");
    CVideo3d::glEnd = (PFNGLENDPROC)GetProcAddress(hOpenGL, "glEnd");
    CVideo3d::glFlush = (PFNGLFLUSHPROC)GetProcAddress(hOpenGL, "glFlush");
    CVideo3d::glFrontFace = (PFNGLFRONTFACEPROC)GetProcAddress(hOpenGL, "glFrontFace");
    CVideo3d::glGenTextures = (PFNGLGENTEXTURESPROC)GetProcAddress(hOpenGL, "glGenTextures");
    CVideo3d::glGetError = (PFNGLGETERRORPROC)GetProcAddress(hOpenGL, "glGetError");
    CVideo3d::glGetIntegerv = (PFNGLGETINTEGERVPROC)GetProcAddress(hOpenGL, "glGetIntegerv");
    CVideo3d::glIsTexture = (PFNGLISTEXTUREPROC)GetProcAddress(hOpenGL, "glIsTexture");
    CVideo3d::glLineWidth = (PFNGLLINEWIDTHPROC)GetProcAddress(hOpenGL, "glLineWidth");
    CVideo3d::glLoadIdentity = (PFNGLLOADIDENTITYPROC)GetProcAddress(hOpenGL, "glLoadIdentity");
    CVideo3d::glMatrixMode = (PFNGLMATRIXMODEPROC)GetProcAddress(hOpenGL, "glMatrixMode");
    CVideo3d::glOrtho = (PFNGLORTHOPROC)GetProcAddress(hOpenGL, "glOrtho");
    CVideo3d::glPixelStorei = (PFNGLPIXELSTOREIPROC)GetProcAddress(hOpenGL, "glPixelStorei");
    CVideo3d::glPointSize = (PFNGLPOINTSIZEPROC)GetProcAddress(hOpenGL, "glPointSize");
    CVideo3d::glPolygonStipple = (PFNGLPOLYGONSTIPPLEPROC)GetProcAddress(hOpenGL, "glPolygonStipple");
    CVideo3d::glPushMatrix = (PFNGLPUSHMATRIXPROC)GetProcAddress(hOpenGL, "glPushMatrix");
    CVideo3d::glReadBuffer = (PFNGLREADBUFFERPROC)GetProcAddress(hOpenGL, "glReadBuffer");
    CVideo3d::glReadPixels = (PFNGLREADPIXELSPROC)GetProcAddress(hOpenGL, "glReadPixels");
    CVideo3d::glScissor = (PFNGLSCISSORPROC)GetProcAddress(hOpenGL, "glScissor");
    CVideo3d::glShadeModel = (PFNGLSHADEMODELPROC)GetProcAddress(hOpenGL, "glShadeModel");
    CVideo3d::glTexCoord2f = (PFNGLTEXCOORD2FPROC)GetProcAddress(hOpenGL, "glTexCoord2f");
    CVideo3d::glTexEnvf = (PFNGLTEXENVFPROC)GetProcAddress(hOpenGL, "glTexEnvf");
    CVideo3d::glTexImage2D = (PFNGLTEXIMAGE2DPROC)GetProcAddress(hOpenGL, "glTexImage2D");
    CVideo3d::glTexParameterf = (PFNGLTEXPARAMETERFPROC)GetProcAddress(hOpenGL, "glTexParameterf");
    CVideo3d::glTexSubImage2D = (PFNGLTEXSUBIMAGE2DPROC)GetProcAddress(hOpenGL, "glTexSubImage2D");
    CVideo3d::glVertex3f = (PFNGLVERTEX3FPROC)GetProcAddress(hOpenGL, "glVertex3f");
    CVideo3d::glViewport = (PFNGLVIEWPORTPROC)GetProcAddress(hOpenGL, "glViewport");
}

// -----------------------------------------------------------------------------
// GLSetup driver detection
//
// The original Init3d drives GLSetup (the circa-2000 3D-driver detection
// product; ini keys "Use GLSetup" / "GLSetup Driver" under [Program Options]).
// These helpers enumerate the installed OpenGL/Glide driver DLLs, validate each
// by its file-version resource, and probe the card by creating a GL context and
// drawing test primitives before committing to a driver. Dormant on modern
// systems, but recovered here for the IWD2EE OpenGL renderer.
// -----------------------------------------------------------------------------

#pragma pack(push, 2)
// One DLL slot of a detected driver: last-write time, file version, and
// canonical path.
struct GLSETUP_DRIVER_FILE {
    /* 0x00 */ DWORD dwWriteTimeHigh;
    /* 0x04 */ DWORD dwWriteTimeLow;
    /* 0x08 */ DWORD dwVersionMS;
    /* 0x0C */ DWORD dwVersionLS;
    /* 0x10 */ CHAR szPath[0x104];
};

// One detected GLSetup driver. sizeof == 0x32C; the driver table is
// g_aGLSetupDrivers[] at 0x906A34 with this stride.
struct GLSETUP_DRIVER {
    /* 0x000 */ DWORD dwFlags;   // 0x80000000 valid, 0x40 mini ver fail, 0x60 mini
                                 //   invalid, 0x10 no ver ("Unknown"), 0x08 in system
                                 //   dir, 0x04 is the system opengl32.dll
    /* 0x004 */ CHAR szId[0x100];             // "Company - Description" / "Unknown"
    /* 0x104 */ GLSETUP_DRIVER_FILE primary;
    /* 0x218 */ GLSETUP_DRIVER_FILE mini;
};
#pragma pack(pop)

// GLSetup control flags: bit0 skips mini-driver validation, bit1 skips the
// primary/mini same-directory check.
// 0x9074AC
static DWORD g_dwGLSetupControl;

// Number of drivers detected by GLSetupEnumDrivers.
// 0x9074B0
static unsigned int g_nGLSetupDrivers;

// Detected driver table (up to 3 kept).
// 0x906920
static GLSETUP_DRIVER g_aGLSetupDrivers[3];

// Index of the driver selected by GLSetupSelectDriver, or -1.
// 0x8BB0A0
static int g_nSelectedGLSetupDriver = -1;

// Candidate OpenGL driver DLLs probed, in priority order. dwFlags is merged
// into the driver record's flags on a successful validate.
struct GLSETUP_CANDIDATE {
    DWORD dwFlags;
    const char* pszPrimary;
    const char* pszSecondary;
    const char* pszDisplay;
};

static const GLSETUP_CANDIDATE g_aGLSetupCandidates[4] = {
    { 0x04, "\\opengl32.dll", "\\glu32.dll",    "Default OpenGL Driver" },
    { 0x03, "\\3dfxogl.dll",  "\\3dfxoglu.dll", "3dfx Installed OpenGL Driver" },
    { 0x80, "\\opengl.dll",   "\\glu.dll",      "SGI Software OpenGL Driver" },
    { 0x03, "\\3dfxvgl.dll",  "\\3dfxvglu.dll", "3dfx Standalone OpenGL Driver" },
};

// Reads pszFilePath's version resource: stores dwFileVersionMS/LS into the
// driver record (+0x8 / +0xC), and when pszIdOut is given, formats a
// "Company - Description" identification string from the version resource.
// Returns TRUE when a VS_FIXEDFILEINFO block was found.
// 0x7BB5D0
static int GLSetupGetDriverVersion(LPCSTR pszFilePath, void* pDriverInfo, char* pszIdOut, unsigned int cbIdOut)
{
    int bFound = 0;
    DWORD dwHandle;
    DWORD dwSize = GetFileVersionInfoSizeA(pszFilePath, &dwHandle);
    *(DWORD*)((char*)pDriverInfo + 0xC) = 0;
    *(DWORD*)((char*)pDriverInfo + 0x8) = 0;
    if (pszIdOut != NULL && cbIdOut != 0) {
        *pszIdOut = 0;
    }
    if (dwSize == 0) {
        return 0;
    }

    void* pVerData = malloc(dwSize);
    if (pVerData != NULL) {
        if (GetFileVersionInfoA(pszFilePath, 0, dwSize, pVerData)) {
            VS_FIXEDFILEINFO* pFixed;
            UINT nFixedLen;
            if (VerQueryValueA(pVerData, "\\", (LPVOID*)&pFixed, &nFixedLen) != 0 &&
                nFixedLen == sizeof(VS_FIXEDFILEINFO) && pFixed != NULL &&
                pFixed->dwSignature == 0xFEEF04BD) {
                bFound = 1;
                *(DWORD*)((char*)pDriverInfo + 0x8) = pFixed->dwFileVersionMS;
                *(DWORD*)((char*)pDriverInfo + 0xC) = pFixed->dwFileVersionLS;
            }

            if (pszIdOut != NULL && cbIdOut != 0) {
                const DWORD aCodePage[2] = { 0x4B0, 0x4E4 };
                char szKey[1024];

                char* pszCompany = NULL;
                UINT cbCompany = 0;
                for (int i = 0; i < 2 && pszCompany == NULL; i++) {
                    if (strlen("CompanyName") + 0x18 < sizeof(szKey)) {
                        sprintf(szKey, "\\StringFileInfo\\%04X%04X\\%s",
                                GetUserDefaultLangID(), aCodePage[i], "CompanyName");
                        VerQueryValueA(pVerData, szKey, (LPVOID*)&pszCompany, &cbCompany);
                    }
                }

                char* pszFileDesc = NULL;
                UINT cbFileDesc = 0;
                for (int i = 0; i < 2 && pszFileDesc == NULL; i++) {
                    if (strlen("FileDescription") + 0x18 < sizeof(szKey)) {
                        sprintf(szKey, "\\StringFileInfo\\%04X%04X\\%s",
                                GetUserDefaultLangID(), aCodePage[i], "FileDescription");
                        VerQueryValueA(pVerData, szKey, (LPVOID*)&pszFileDesc, &cbFileDesc);
                    }
                }

                char* pszProduct = NULL;
                UINT cbProduct = 0;
                for (int i = 0; i < 2 && pszProduct == NULL; i++) {
                    if (strlen("ProductName") + 0x18 < sizeof(szKey)) {
                        sprintf(szKey, "\\StringFileInfo\\%04X%04X\\%s",
                                GetUserDefaultLangID(), aCodePage[i], "ProductName");
                        VerQueryValueA(pVerData, szKey, (LPVOID*)&pszProduct, &cbProduct);
                    }
                }

                UINT cbDesc = (cbFileDesc < cbProduct) ? cbProduct : cbFileDesc;
                if (cbDesc + 3 + cbCompany < cbIdOut) {
                    char* pszDesc = pszFileDesc;
                    if (pszFileDesc == NULL) {
                        pszDesc = pszProduct;
                        if (pszProduct == NULL) {
                            pszDesc = "";
                        }
                    }
                    char* pszSep;
                    if (pszCompany == NULL || (pszFileDesc == NULL && pszProduct == NULL)) {
                        pszSep = "";
                    } else {
                        pszSep = " - ";
                    }
                    char* pszComp = pszCompany;
                    if (pszCompany == NULL) {
                        pszComp = "";
                    }
                    sprintf(pszIdOut, "%s%s%s", pszComp, pszSep, pszDesc);
                }
            }
        }
        free(pVerData);
    }
    return bFound;
}

// Validates a candidate driver: records each DLL's last-write time and file
// version, classifies it (resides in the system dir, is the system
// opengl32.dll, ...) via canonical-path comparisons, and rejects a mini-driver
// that is not co-located with the primary. Returns 0 on success, 4 on failure.
// 0x7B7940
static int GLSetupValidateDriver(LPCSTR pszPrimaryPath, LPCSTR pszMiniPath, GLSETUP_DRIVER* pDriver)
{
    if (pszPrimaryPath == NULL) {
        return 4;
    }
    if (pDriver == NULL) {
        return 4;
    }

    CHAR szSysDir[0x104];
    CHAR szNormA[0x104];
    CHAR szNormB[0x104];
    LPSTR pFilePart;
    BY_HANDLE_FILE_INFORMATION fi;

    pDriver->primary.dwWriteTimeLow = 0;
    pDriver->primary.dwWriteTimeHigh = 0;

    HANDLE hFile = CreateFileA(pszPrimaryPath, GENERIC_READ, FILE_SHARE_READ, NULL,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        return 4;
    }
    BOOL bInfo = GetFileInformationByHandle(hFile, &fi);
    if (bInfo) {
        pDriver->primary.dwWriteTimeHigh = fi.ftLastWriteTime.dwHighDateTime;
        pDriver->primary.dwWriteTimeLow = fi.ftLastWriteTime.dwLowDateTime;
    }
    CloseHandle(hFile);
    if (!bInfo) {
        return 4;
    }

    pDriver->dwFlags = 0x80000000;

    BOOL bMiniHandled = FALSE;
    if (pszMiniPath != NULL && *pszMiniPath != '\0' && (g_dwGLSetupControl & 1) == 0) {
        pDriver->mini.dwWriteTimeLow = 0;
        pDriver->mini.dwWriteTimeHigh = 0;
        hFile = CreateFileA(pszMiniPath, GENERIC_READ, FILE_SHARE_READ, NULL,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile != INVALID_HANDLE_VALUE) {
            BOOL bMiniInfo = FALSE;
            if (GetFileInformationByHandle(hFile, &fi)) {
                pDriver->mini.dwWriteTimeHigh = fi.ftLastWriteTime.dwHighDateTime;
                pDriver->mini.dwWriteTimeLow = fi.ftLastWriteTime.dwLowDateTime;
                bMiniInfo = TRUE;
            }
            CloseHandle(hFile);
            if (bMiniInfo) {
                if (GLSetupGetDriverVersion(pszMiniPath, &pDriver->mini, NULL, 0) == 0) {
                    pDriver->dwFlags |= 0x40;
                }
                pDriver->mini.szPath[0] = '\0';
                if (GetFullPathNameA(pszMiniPath, 0x104, pDriver->mini.szPath, &pFilePart) != 0 &&
                    GetSystemDirectoryA(szSysDir, 0x104) != 0) {
                    int nDirLen = (int)(pFilePart - pDriver->mini.szPath);
                    _mbslwr((unsigned char*)pDriver->mini.szPath);
                    _mbslwr((unsigned char*)szSysDir);
                    if (nDirLen - 1 == (int)strlen(szSysDir)) {
                        strstr(pDriver->mini.szPath, szSysDir);
                    }
                    bMiniHandled = TRUE;
                }
            }
        }
    }
    if (!bMiniHandled) {
        pDriver->mini.szPath[0] = '\0';
        pDriver->dwFlags |= 0x60;
    }

    if (GLSetupGetDriverVersion(pszPrimaryPath, &pDriver->primary, pDriver->szId, 0x100) == 0) {
        strncpy(pDriver->szId, "Unknown", 0x100);
        pDriver->dwFlags |= 0x10;
    }

    pDriver->primary.szPath[0] = '\0';
    if (GetFullPathNameA(pszPrimaryPath, 0x104, pDriver->primary.szPath, &pFilePart) == 0 ||
        GetSystemDirectoryA(szSysDir, 0x104) == 0) {
        return 4;
    }
    {
        int nDirLen = (int)(pFilePart - pDriver->primary.szPath);
        _mbslwr((unsigned char*)pDriver->primary.szPath);
        _mbslwr((unsigned char*)szSysDir);
        if (nDirLen - 1 == (int)strlen(szSysDir) &&
            strstr(pDriver->primary.szPath, szSysDir) == pDriver->primary.szPath) {
            pDriver->dwFlags |= 8;
        }
    }

    GetSystemDirectoryA(szSysDir, 0x104);
    strcat(szSysDir, "\\opengl32.dll");
    GetFullPathNameA(szSysDir, 0x104, szNormA, &pFilePart);
    if (GetFullPathNameA(pDriver->primary.szPath, 0x104, szNormB, &pFilePart) != 0) {
        if (_mbsicmp((unsigned char*)szNormB, (unsigned char*)szNormA) == 0) {
            pDriver->dwFlags |= 4;
        }
    }

    if ((g_dwGLSetupControl & 2) == 0) {
        LPSTR pFilePartA = NULL;
        LPSTR pFilePartB = NULL;
        GetFullPathNameA(pDriver->primary.szPath, 0x104, szNormA, &pFilePartA);
        GetFullPathNameA(pDriver->mini.szPath, 0x104, szNormB, &pFilePartB);
        if (pFilePartA != NULL) {
            *pFilePartA = '\0';
        }
        if (pFilePartB != NULL) {
            *pFilePartB = '\0';
        }
        if (_mbsicmp((unsigned char*)szNormA, (unsigned char*)szNormB) != 0) {
            pDriver->mini.szPath[0] = '\0';
            pDriver->dwFlags |= 0x60;
        }
    }

    return 0;
}

// Enumerates the candidate OpenGL driver DLLs in the Windows system directory,
// validates each, and fills g_aGLSetupDrivers (up to 3). Returns the count.
// 0x7B7750
static unsigned int GLSetupEnumDrivers(void)
{
    CHAR szPrimaryPath[0x104];
    CHAR szSecondaryPath[0x104];

    g_nGLSetupDrivers = 0;
    if (GetSystemDirectoryA(szPrimaryPath, 0x104) == 0) {
        return g_nGLSetupDrivers;
    }
    int nSysLen = (int)strlen(szPrimaryPath);
    strcpy(szSecondaryPath, szPrimaryPath);

    for (int i = 0; i < 4; i++) {
        if (g_nGLSetupDrivers > 2) {
            return g_nGLSetupDrivers;
        }
        const GLSETUP_CANDIDATE* pCand = &g_aGLSetupCandidates[i];
        if ((int)strlen(pCand->pszPrimary) + nSysLen < 0x104 &&
            (int)strlen(pCand->pszSecondary) + nSysLen < 0x104) {
            strcpy(szPrimaryPath + nSysLen, pCand->pszPrimary);
            strcpy(szSecondaryPath + nSysLen, pCand->pszSecondary);
            GLSETUP_DRIVER* pDriver = &g_aGLSetupDrivers[g_nGLSetupDrivers];
            if (GLSetupValidateDriver(szPrimaryPath, szSecondaryPath, pDriver) == 0) {
                pDriver->dwFlags = (pDriver->dwFlags & 0x7fffffff) | pCand->dwFlags;
                if (pCand->pszDisplay[0] != '\0') {
                    strncpy(pDriver->szId, pCand->pszDisplay, 0x100);
                }
                g_nGLSetupDrivers++;
            }
        }
    }
    return g_nGLSetupDrivers;
}

// Loads the selected driver's DLLs, creates a GL context, draws test primitives
// to verify the card renders, and resolves its GL/WGL entry points into the
// module globals. Returns 0 on success.
// 0x7B7DA0
static int GLSetupProbeDriver(const char* pszPrimaryPath, const char* pszMiniPath)
{
    // Unrecovered: the ~11.5 KB GL capability probe. Left failing so
    // GLSetupSelectDriver reports no usable driver until it is recovered, which
    // keeps Init3d on its plain opengl32.dll fallback (no behaviour change).
    (void)pszPrimaryPath;
    (void)pszMiniPath;
    return 4;
}

// Ensures drivers are enumerated, then probes driver nDriver; on success records
// it as the active driver. Returns 0 on success, 4 for a bad index, else the
// probe's error code.
// 0x7B7D40
static int GLSetupSelectDriver(unsigned int nDriver)
{
    if (g_nGLSetupDrivers == 0) {
        GLSetupEnumDrivers();
    }
    if (nDriver >= g_nGLSetupDrivers) {
        return 4;
    }
    int nResult = GLSetupProbeDriver(g_aGLSetupDrivers[nDriver].primary.szPath,
                                     g_aGLSetupDrivers[nDriver].mini.szPath);
    if (nResult == 0) {
        g_nSelectedGLSetupDriver = nDriver;
        return 0;
    }
    return nResult;
}
