// Copyright (c) 2014-2021 Crown Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef DASH_SPECIALTX_H
#define DASH_SPECIALTX_H

#include <primitives/transaction.h>
#include <streams.h>
#include <version.h>
#include <validation.h>

class CBlock;
class CBlockIndex;
class CTransaction;
class TxValidationState;

bool CheckEvoTx(const CTransaction& tx, const CBlockIndex* pindexPrev, TxValidationState& state);
bool ProcessEvoTxsInBlock(const CBlock& block, const CBlockIndex* pindex, TxValidationState& state, bool fJustCheck, bool fCheckCbTxMerkleRoots = true);
bool UndoEvoTxsInBlock(const CBlock& block, const CBlockIndex* pindex);

template <typename T>
inline bool GetTxPayload(const std::vector<unsigned char>& payload, T& obj)
{
    CDataStream ds(payload, SER_NETWORK, PROTOCOL_VERSION);
    try {
        ds >> obj;
    } catch (std::exception& e) {
        return false;
    }
    return ds.empty();
}

template <typename T>
inline bool GetTxPayload(const CMutableTransaction& tx, T& obj)
{
    return GetTxPayload(tx.vExtraPayload, obj);
}

template <typename T>
inline bool GetTxPayload(const CTransaction& tx, T& obj)
{
    return GetTxPayload(tx.vExtraPayload, obj);
}

template <typename T>
void SetTxPayload(CMutableTransaction& tx, const T& payload)
{
    CDataStream ds(SER_NETWORK, PROTOCOL_VERSION);
    ds << payload;
    tx.vExtraPayload.assign(ds.begin(), ds.end());
}

uint256 CalcTxInputsHash(const CTransaction& tx);

#endif //DASH_SPECIALTX_H
