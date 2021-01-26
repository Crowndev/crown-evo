// Copyright (c) 2018-2019 The Dash Core developers
// Copyright (c) 2014-2020 Crown Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <nft/specialtx.h>

#include <clientversion.h>
#include <chainparams.h>
#include <consensus/validation.h>
#include <hash.h>
#include <primitives/block.h>
#include <primitives/transaction.h>
#include <util/system.h>

bool CheckNftTx(const CTransaction& tx, const CBlockIndex* pindexLast, CValidationState& state)
{
    if (tx.nVersion != TX_NFT_VERSION || tx.nType == TRANSACTION_NORMAL) {
        return true;
    }

    try {
        switch (tx.nType) {
        case TRANSACTION_NF_TOKEN_REGISTER:
        case TRANSACTION_NF_TOKEN_PROTOCOL_REGISTER:
            return true;
        }
    } catch (const std::exception& e) {
        LogPrintf("%s -- failed: %s\n", __func__, e.what());
    }

    return true;
}

bool ProcessNftTx(const CTransaction& tx, const CBlockIndex* pindex, CValidationState& state)
{
    if (tx.nVersion != TX_NFT_VERSION || tx.nType == TRANSACTION_NORMAL) {
        return true;
    }

    switch (tx.nType) {
        case TRANSACTION_NF_TOKEN_REGISTER:
        case TRANSACTION_NF_TOKEN_PROTOCOL_REGISTER:
            return true;
    }

    return true;
}

bool UndoNftTx(const CTransaction& tx, const CBlockIndex* pindex)
{
    if (tx.nVersion != TX_NFT_VERSION || tx.nType == TRANSACTION_NORMAL) {
        return true;
    }

    switch (tx.nType) {
        case TRANSACTION_NF_TOKEN_REGISTER:
        case TRANSACTION_NF_TOKEN_PROTOCOL_REGISTER:
            return true;
    }

    return false;
}

bool ProcessNftTxsInBlock(const CBlock& block, const CBlockIndex* pindex, CValidationState& state)
{
    int64_t nTimeLoop = 0;
    try {
        int64_t nTime1 = GetTimeMicros();
        for (int i = 0; i < (int)block.vtx.size(); i++) {
            const CTransaction& tx = *block.vtx[i];
            if (!CheckNftTx(tx, pindex->pprev, state)) {
                return false;
            }
            if (!ProcessNftTx(tx, pindex, state)) {
                return false;
            }
        }
        int64_t nTime2 = GetTimeMicros();
        nTimeLoop += nTime2 - nTime1;
        LogPrint(BCLog::BENCH, "        - ProcessNftTxsInBlock: %.2fms [%.2fs]\n", 0.001 * (nTime2 - nTime1), nTimeLoop * 0.000001);
    } catch (const std::exception& e) {
        LogPrintf("%s -- failed: %s\n", __func__, e.what());
    }

    return true;
}

bool UndoNftTxsInBlock(const CBlock& block, const CBlockIndex* pindex)
{
    int64_t nTimeLoop = 0;
    try {
        int64_t nTime1 = GetTimeMicros();
        for (int i = (int)block.vtx.size() - 1; i >= 0; --i) {
            const CTransaction& tx = *block.vtx[i];
            if (!UndoNftTx(tx, pindex)) {
                return false;
            }
        }
        int64_t nTime2 = GetTimeMicros();
        nTimeLoop += nTime2 - nTime1;
        LogPrint(BCLog::BENCH, "        - UndoNftTxsInBlock: %.2fms [%.2fs]\n", 0.001 * (nTime2 - nTime1), nTimeLoop * 0.000001);
    } catch (const std::exception& e) {
        return error(strprintf("%s -- failed: %s\n", __func__, e.what()).c_str());
    }

    return true;
}

uint256 CalcNftTxInputsHash(const CTransaction& tx)
{
    CHashWriter hw(CLIENT_VERSION, SER_GETHASH);
    for (const auto& in : tx.vin) {
        hw << in.prevout;
    }
    return hw.GetHash();
}
