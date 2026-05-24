#include "AutoLoad.h"

#include "mfc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace Iwd2AutoLoad {

static DWORD GetEnv(const char* name, char* value, DWORD size)
{
    DWORD result = GetEnvironmentVariableA(name, value, size);
    if (result >= size && size > 0) {
        value[size - 1] = '\0';
    }
    return result;
}

bool IsEnabled()
{
    char value[32];
    return GetEnv("IWD2_RE_AUTO_ACTION", value, sizeof(value)) > 0
        || GetEnv("IWD2_RE_AUTO_LOAD_ORIGINAL", value, sizeof(value)) > 0;
}

bool IsAction(const char* action)
{
    char value[32];
    if (GetEnv("IWD2_RE_AUTO_ACTION", value, sizeof(value)) == 0) {
        if (strcmp(action, "load") == 0) {
            return GetEnv("IWD2_RE_AUTO_LOAD_ORIGINAL", value, sizeof(value)) > 0;
        }
        return false;
    }

    return _stricmp(value, action) == 0;
}

int GetSlot(int defaultSlot)
{
    char value[32];
    if (GetEnv("IWD2_RE_AUTO_SLOT", value, sizeof(value)) == 0) {
        return defaultSlot;
    }

    return atoi(value);
}

int GetParty(int defaultParty)
{
    char value[32];
    if (GetEnv("IWD2_RE_AUTO_PARTY", value, sizeof(value)) == 0) {
        return defaultParty;
    }

    return atoi(value);
}

void WriteResult(const char* status, const char* detail)
{
    char path[MAX_PATH];
    if (GetEnv("IWD2_RE_AUTO_RESULT", path, sizeof(path)) == 0) {
        return;
    }

    FILE* fp = fopen(path, "wb");
    if (fp == NULL) {
        return;
    }

    fprintf(fp, "status=%s\n", status);
    fprintf(fp, "detail=%s\n", detail);
    fclose(fp);
}

}
