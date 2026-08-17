#ifndef CRESBIO_H_
#define CRESBIO_H_

#include "CRes.h"

// The resource class for BIO (biography) files -- resource type 1022, the last
// entry in `CBaldurChitin::AllocResObject`'s table. Named from BG2's PDB, which
// spells out `CResHelper<CResBIO,1022>`; IWD2 has no such helper, so the class
// is only ever built by AllocResObject.
//
// BG2 gives CResBIO no data member of its own, so the 0x50 slot both its ctor
// and CResCRE's write really belongs to CRes -- whose declared layout stops at
// 0x4C and whose own ctor leaves 0x50 alone. Mirrored from CResCRE.h/CResINI.h
// rather than hoisted into CRes.h, which is a change of its own.
class CResBIO : public CRes {
public:
    CResBIO();
    ~CResBIO();

    /* 0050 */ BOOL m_bParsed;
};

#endif /* CRESBIO_H_ */
