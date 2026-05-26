#include "mfc.h"

#include <iostream>

#include "CBaldurChitin.h"
// 0x421820
static BOOL IsSupportedOS(DWORD& majorVersion, DWORD& minorVersion)
{
    OSVERSIONINFOEXA info = { 0 };
    BOOL supported = TRUE;
    int servicePack;

    info.dwOSVersionInfoSize = sizeof(OSVERSIONINFOEXA);

#pragma warning(suppress : 4996)
    if (!GetVersionExA(reinterpret_cast<LPOSVERSIONINFOA>(&info))) {
        info.dwOSVersionInfoSize = sizeof(OSVERSIONINFOA);

#pragma warning(suppress : 4996)
        if (!GetVersionExA(reinterpret_cast<LPOSVERSIONINFOA>(&info))) {
            return FALSE;
        }
    }

    switch (info.dwPlatformId) {
    case VER_PLATFORM_WIN32s:
        supported = FALSE;
        break;
    case VER_PLATFORM_WIN32_WINDOWS:
        if (info.dwMajorVersion < 4) {
            supported = FALSE;
        }
        break;
    case VER_PLATFORM_WIN32_NT:
        servicePack = 0;
        sscanf(info.szCSDVersion, "Service Pack %d", &servicePack);
        if (info.dwMajorVersion <= 3 || (info.dwMajorVersion == 4 && info.dwMinorVersion != 0 && servicePack < 5)) {
            supported = FALSE;
        }
    }

    majorVersion = info.dwMajorVersion;
    minorVersion = info.dwMinorVersion;
    return supported;
}

static LONG WINAPI CrashHandler(EXCEPTION_POINTERS* ep)
{
    if (ep && ep->ExceptionRecord && ep->ContextRecord) {
        FILE* f = fopen("iwd2-re-crash.log", "a");
        if (f) {
            HMODULE hMod = NULL;
            GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                (LPCSTR)ep->ContextRecord->Eip, &hMod);
            fprintf(f, "CRASH tid=%lu code=0x%08lX eip=0x%08lX (base=0x%08lX +0x%08lX) access=0x%08lX eax=0x%08lX ecx=0x%08lX edx=0x%08lX ebx=0x%08lX ebp=0x%08lX esp=0x%08lX esi=0x%08lX edi=0x%08lX\n",
                GetCurrentThreadId(),
                ep->ExceptionRecord->ExceptionCode,
                ep->ContextRecord->Eip,
                (DWORD)hMod,
                ep->ContextRecord->Eip - (DWORD)hMod,
                (ep->ExceptionRecord->NumberParameters >= 2) ? (DWORD)ep->ExceptionRecord->ExceptionInformation[1] : 0,
                ep->ContextRecord->Eax,
                ep->ContextRecord->Ecx,
                ep->ContextRecord->Edx,
                ep->ContextRecord->Ebx,
                ep->ContextRecord->Ebp,
                ep->ContextRecord->Esp,
                ep->ContextRecord->Esi,
                ep->ContextRecord->Edi);
            DWORD* stack = (DWORD*)ep->ContextRecord->Esp;
            fprintf(f, "STACK");
            for (int i = 0; i < 16; i++) {
                __try { fprintf(f, " 0x%08lX", stack[i]); }
                __except(1) { fprintf(f, " ????????"); }
            }
            fprintf(f, "\n");
            HMODULE hExe = GetModuleHandleA(NULL);
            DWORD* ebpChain = (DWORD*)ep->ContextRecord->Ebp;
            fprintf(f, "FRAMES");
            for (int i = 0; i < 10; i++) {
                __try {
                    DWORD retAddr = ebpChain[1];
                    fprintf(f, " 0x%08lX(+0x%08lX)", retAddr, retAddr - (DWORD)hExe);
                    ebpChain = (DWORD*)ebpChain[0];
                    if (ebpChain == NULL) break;
                } __except(1) { break; }
            }
            fprintf(f, "\n");
            fclose(f);
        }
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    SetUnhandledExceptionFilter(CrashHandler);

    // Enable CRT debug heap checking on every allocation


    std::cout << std::endl;
    std::cout << "BEGIN LOGGING SESSION";
    std::cout << std::endl;

    CChitin::GetGameVersionInfo(hInstance);

    AfxSetResourceHandle(hInstance);

    CChitin::FixReadonlyPermissions();

    DWORD majorVersion;
    DWORD minorVersion;
    if (!IsSupportedOS(majorVersion, minorVersion)) {
        const char* text = "Icewind Dale II requires one of these Microsoft Operating Systems to run:\r\n"
                           "\r\n"
                           "  \x95 Windows XP\r\n"
                           "  \x95 Windows 2000\r\n"
                           "  \x95 Windows NT 4 with Service Pack 5 or above\r\n"
                           "  \x95 Windows ME\r\n"
                           "  \x95 Windows 98\r\n"
                           "  \x95 Windows 95\r\n"
                           "\r\n"
                           "Press Enter to Exit";
        const char* caption = "Operating System Not Supported";
        MessageBoxA(NULL, text, caption, MB_ICONERROR);
        return 0;
    }

    char v1[] = "Global\\Icewind2";
    char v2[] = "Icewind2";
    char* mutexName = v2;
    if (majorVersion >= 5) {
        mutexName = v1;
    }

    HANDLE mutexHandle = CreateMutexA(NULL, FALSE, mutexName);

    // NOTE: Wrong check, should check for `INVALID_HANDLE_VALUE`.
    if (mutexHandle == NULL) {
        return 0;
    }

    if (WaitForSingleObject(mutexHandle, 0) == WAIT_TIMEOUT) {
        CloseHandle(mutexHandle);
        return 0;
    }

    CBaldurChitin baldurChitin;
    if (baldurChitin.field_1932 != 0) {
        CloseHandle(mutexHandle);
        return 0;
    }

    char currentDirectory[261];
    GetCurrentDirectoryA(sizeof(currentDirectory), currentDirectory);

    char currentDirectoryCopy[261];
    lstrcpyA(currentDirectoryCopy, currentDirectory);
    lstrcatA(currentDirectoryCopy, "\\register");
    SetCurrentDirectoryA(currentDirectoryCopy);

    STARTUPINFO startupInfo = { 0 };
    PROCESS_INFORMATION processInfo;
    if (CreateProcessA(NULL, "reg32.exe FALSE", NULL, NULL, FALSE, NORMAL_PRIORITY_CLASS, NULL, NULL, &startupInfo, &processInfo)) {
        WaitForSingleObject(processInfo.hProcess, INFINITE);
        CloseHandle(processInfo.hThread);
        CloseHandle(processInfo.hProcess);
    }

    SetCurrentDirectoryA(currentDirectory);
    // FIXME: Original hardcoded path doesn't exist on this machine.
    // SetCurrentDirectoryA("C:\\Program Files (x86)\\GOG.com\\Icewind Dale II");

    baldurChitin.Init(hInstance);
    int rc = baldurChitin.WinMain(hInstance, hPrevInstance, lpCmdLine, nCmdShow);
    CloseHandle(mutexHandle);
    return rc;
}
