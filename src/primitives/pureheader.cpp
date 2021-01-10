// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Syscoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <hash.h>
#include <primitives/pureheader.h>

void CBlockVersion::SetBaseVersion(int32_t nBaseVersion, int32_t nChainId)
{
    assert(nBaseVersion >= 1 && nBaseVersion < VERSION_AUXPOW);
    assert(!IsAuxpow());
    nVersion = nBaseVersion | (nChainId * VERSION_CHAIN_START);
}

uint256 CPureBlockHeader::GetHash() const
{
    return SerializeHash(*this);
}