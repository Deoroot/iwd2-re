#include "CResBIO.h"

// NOTE: Inlined.
//
// Only ever emitted inline, in `CBaldurChitin::AllocResObject`'s case 1022 at
// 0x423637: `operator new(0x54)`, `CRes::CRes`, vtable 0x8480C8, `+0x50 = 0` --
// the same five steps as case 2050's CResINI a few bytes further down.
CResBIO::CResBIO()
{
    m_bParsed = FALSE;
}

// 0x423790
CResBIO::~CResBIO()
{
}
