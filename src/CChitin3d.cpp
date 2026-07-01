#include "CChitin3d.h"

#include "CChitin.h"

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
    // NOTE: Original code is different. This function is an entry point to
    // several functions used to pick and setup various OpenGL drivers which
    // were available at that time:
    // - opengl32.dll + glu32.dll
    // - 3dfxogl.dll + 3dfoglu.dll
    // - opengl.dll + glu.dll
    // - 3dfxvgl.dll + 3dfxvglu.dll
    //
    // Since it's very unlikely someone still has these drivers and video cards,
    // this code is omitted.

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
