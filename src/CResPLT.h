#ifndef CRESPLT_H_
#define CRESPLT_H_

#include "CRes.h"

class CResPLT : public CRes {
public:
    CResPLT();
    ~CResPLT();

    /* 0050 */ int m_pPaletteData;
    /* 0054 */ int m_pHeader;
    /* 0058 */ int m_nSize;
};

#endif /* CRESPLT_H_ */
