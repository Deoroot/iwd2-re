#ifndef AUTOUI_H_
#define AUTOUI_H_

// TEST SCAFFOLDING -- not a mirror of IWD2.exe.
//
// Same shape as AutoLoad.h: an isolated namespace, inert unless an environment
// variable points it at a script, called from exactly one guarded site. It adds
// no game behaviour; it only replays the input the engine already accepts.

class CWarp;

namespace Iwd2AutoUI {

// True only when IWD2_RE_UI_SCRIPT names a readable script. Everything else in
// this module is unreachable when this returns false.
bool IsEnabled();

// One action per call, from the tail of CChitin::AsynchronousUpdate. Never
// blocks: a step that needs to wait simply returns and is retried next tick.
void Tick(CWarp* pActiveEngine);

}

#endif
