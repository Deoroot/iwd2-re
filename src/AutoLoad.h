#ifndef AUTOLOAD_H_
#define AUTOLOAD_H_

namespace Iwd2AutoLoad {

bool IsEnabled();
bool IsAction(const char* action);
int GetSlot(int defaultSlot);
int GetParty(int defaultParty);
bool GetSaveName(char* value, int size);
void WriteResult(const char* status, const char* detail);

}

#endif
