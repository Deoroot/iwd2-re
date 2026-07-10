#ifndef TSTRING_H_
#define TSTRING_H_

#include <afx.h>
#include <cstring>
#include <stdexcept>

// TString -- the engine's string class (BG2 PDB: TString, union sizeof 16). It is
// the VC6 Dinkumware reference-counted std::basic_string<char>: a 16-byte object
// laid out { <union head>[4], char* m_pData @4, int m_nLen @8, int m_nRes @0xc }
// whose heap buffer carries a one-byte reference count immediately ahead of m_pData
// (m_pData[-1]) and whose capacity is rounded up with | 0x1f. Reconstructed from the
// assign/grow/tidy helpers 0x44BC20 / 0x44BE10 / 0x448D50, the _Xlen throw 0x7F9AC2
// and the substring compare 0x4C5ED0.
//
// NOTE: our recovered call sites only default-construct, assign-from-char*, compare
// and destroy a TString -- they never copy or share one -- so the reference count
// stays at the "owned" sentinel (0) and the copy-on-write detach path is never
// reached. Assign therefore reproduces 0x44BC20's observable result (an owned heap
// buffer with the new contents, capacity rounded | 0x1f, a leading refcount byte)
// without the share/unshare branches that no live path exercises. Copy construction
// and copy assignment (0x44BA90) share the source buffer and bump the reference
// count in the binary; we model them as a deep copy into an owned buffer -- the same
// owned-only simplification as Assign, and observably identical for the resref keys
// std::set<TString> holds (CDerivedStats::m_cImmunitiesSpell).
#pragma pack(push, 4)
class TString {
public:
    /* 0000 */ char m_buf[4];   // union head; unused once heap-allocated. A caller may
                                // repurpose byte 0 (CGameAnimationTypeEffect stores its
                                // BYTE facing there -- the string ops never touch +0).
    /* 0004 */ char* m_pData;   // heap contents; m_pData[-1] holds the reference count
    /* 0008 */ int m_nLen;
    /* 000C */ int m_nRes;      // capacity, rounded up | 0x1f

    TString()
    {
        m_buf[0] = 0;
        m_buf[1] = 0;
        m_buf[2] = 0;
        m_buf[3] = 0;
        m_pData = NULL;
        m_nLen = 0;
        m_nRes = 0;
    }

    ~TString()
    {
        Tidy(TRUE);
    }

    TString& operator=(const char* s)
    {
        return Assign(s, static_cast<int>(strlen(s)));
    }

    // Deep-copy into an owned buffer (see the class NOTE: the binary shares +
    // ref-counts, we model the owned-only result).  Enables std::set<TString>.
    TString(const TString& other)
    {
        m_buf[0] = other.m_buf[0]; // preserve the union-head byte some callers stash
        m_buf[1] = 0;
        m_buf[2] = 0;
        m_buf[3] = 0;
        m_pData = NULL;
        m_nLen = 0;
        m_nRes = 0;
        if (other.m_pData != NULL && other.m_nLen > 0) {
            Assign(other.m_pData, other.m_nLen);
        }
    }

    TString& operator=(const TString& other)
    {
        if (this != &other) {
            if (other.m_pData != NULL && other.m_nLen > 0) {
                Assign(other.m_pData, other.m_nLen);
            } else {
                Tidy(TRUE);
            }
            m_buf[0] = other.m_buf[0];
        }
        return *this;
    }

    const char* Data() const { return m_pData; }
    int Length() const { return m_nLen; }

    // 0x4C5ED0: lexicographically compare [nPos, nPos + nCount) of this string
    // against the nLen2 bytes at pStr2, memcmp-style with a length tie-break.
    int Compare(int nPos, int nCount, const char* pStr2, int nLen2) const
    {
        if (static_cast<unsigned>(m_nLen) < static_cast<unsigned>(nPos)) {
            Xlen();
        }
        int nRem = m_nLen - nPos;
        if (nRem < nCount) {
            nCount = nRem;
        }
        int nScan = (nCount < nLen2) ? nCount : nLen2;
        const char* p = (m_pData != NULL) ? m_pData + nPos : NULL;
        int nResult = 0;
        for (int i = 0; i < nScan; i++) {
            if (static_cast<BYTE>(p[i]) != static_cast<BYTE>(pStr2[i])) {
                nResult = (static_cast<BYTE>(p[i]) < static_cast<BYTE>(pStr2[i])) ? -1 : 1;
                break;
            }
        }
        if (nResult == 0) {
            if (nCount < nLen2) {
                return -1;
            }
            nResult = (nCount != nLen2) ? 1 : 0;
        }
        return nResult;
    }

private:
    // 0x44BC20 (assign) + 0x44BE10 (grow) collapsed to the owned-buffer path.
    TString& Assign(const char* s, int nLen)
    {
        if (static_cast<unsigned>(nLen) > 0xFFFFFFFD) {
            Xlen();
        }
        if (m_pData == NULL || nLen > m_nRes) {
            Tidy(TRUE);
            if (nLen != 0) {
                Grow(nLen);
            }
        }
        if (nLen != 0) {
            memcpy(m_pData, s, nLen);
            m_pData[nLen] = 0;
            m_nLen = nLen;
        } else if (m_pData != NULL) {
            m_nLen = 0;
            m_pData[0] = 0;
        }
        return *this;
    }

    // 0x44BE10: allocate an owned buffer of capacity (nLen | 0x1f), preceded by the
    // reference-count byte (0 == owned) and followed by room for the terminator.
    void Grow(int nLen)
    {
        int nCap = nLen | 0x1f;
        if (static_cast<unsigned>(nCap) > 0xFFFFFFFD) {
            nCap = nLen;
        }
        char* pBuf = new char[nCap + 2];
        pBuf[0] = 0;
        m_pData = pBuf + 1;
        m_nRes = nCap;
    }

    // 0x448D50: release the buffer (free when owned -- refcount 0 or the 0xFF frozen
    // marker -- else decrement the shared count) and reset to empty.
    void Tidy(BOOL bDealloc)
    {
        if (bDealloc && m_pData != NULL) {
            char rc = m_pData[-1];
            if (rc == 0 || rc == static_cast<char>(0xFF)) {
                delete[] (m_pData - 1);
            } else {
                m_pData[-1] = rc - 1;
            }
        }
        m_pData = NULL;
        m_nLen = 0;
        m_nRes = 0;
    }

    // 0x7F9AC2: std::_String_base::_Xlen.
    static void Xlen()
    {
        throw std::length_error("string too long");
    }
};
#pragma pack(pop)

#endif /* TSTRING_H_ */
