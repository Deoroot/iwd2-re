#include "DebugLog.h"

#include "mfc.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static const char* IWD2_DEBUG_LOG_FILE = ".\\iwd2-re-debug.log";

void Iwd2DebugLog(const char* format, ...)
{
    char message[1024];

    va_list args;
    va_start(args, format);
    _vsnprintf(message, sizeof(message) - 1, format, args);
    va_end(args);
    message[sizeof(message) - 1] = '\0';

    char line[1200];
    _snprintf(line,
        sizeof(line) - 1,
        "[%lu][tid:%lu] %s\r\n",
        GetTickCount(),
        GetCurrentThreadId(),
        message);
    line[sizeof(line) - 1] = '\0';

    OutputDebugStringA(line);

    FILE* fp = fopen(IWD2_DEBUG_LOG_FILE, "ab");
    if (fp != NULL) {
        fwrite(line, 1, strlen(line), fp);
        fclose(fp);
    }
}

void Iwd2DebugLogReset()
{
    FILE* fp = fopen(IWD2_DEBUG_LOG_FILE, "wb");
    if (fp != NULL) {
        fclose(fp);
    }

    Iwd2DebugLog("debug log reset");
}
