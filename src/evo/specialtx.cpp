// Copyright (c) 2018-2019 The Dash Core developers
// Copyright (c) 2014-2020 Crown Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <evo/specialtx.h>

#include <clientversion.h>
#include <chainparams.h>
#include <consensus/validation.h>
#include <hash.h>
#include <primitives/block.h>
#include <primitives/transaction.h>
#include <util/system.h>

bool CheckEvoTx(const CTransaction& tx, const CBlockIndex* pindexPrev, TxValidationState& state)
{
    if (tx.nVersion != TX_EVO_VERSION || tx.nType == TRANSACTION_NORMAL) {
        return true;
    }

    if (pindexPrev && pindexPrev->nHeight + 1 < Params().GetConsensus().nDIP0003Height) {
        return state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-tx-type");
    }

    try {
        switch (tx.nType) {
        case TRANSACTION_PROVIDER_REGISTER:
        case TRANSACTION_PROVIDER_UPDATE_SERVICE:
        case TRANSACTION_PROVIDER_UPDATE_REGISTRAR:
        case TRANSACTION_PROVIDER_UPDATE_REVOKE:
        case TRANSACTION_COINBASE:
        case TRANSACTION_QUORUM_COMMITMENT:
            return true;
        }
    } catch (const std::exception& e) {
        LogPrintf("%s -- failed: %s\n", __func__, e.what());
        return state.Invalid(TxValidationResult::TX_CONSENSUS, "failed-check-special-tx");
    }
    return state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-tx-type-check");
}

bool ProcessEvoTx(const CTransaction& tx, const CBlockIndex* pindex, TxValidationState& state)
{
    if (tx.nVersion != TX_EVO_VERSION || tx.nType == TRANSACTION_NORMAL) {
        return true;
    }

    switch (tx.nType) {
    case TRANSACTION_PROVIDER_REGISTER:
    case TRANSACTION_PROVIDER_UPDATE_SERVICE:
    case TRANSACTION_PROVIDER_UPDATE_REGISTRAR:
    case TRANSACTION_PROVIDER_UPDATE_REVOKE:
        return true; // handled in batches per block
    case TRANSACTION_COINBASE:
        return true; // nothing to do
    case TRANSACTION_QUORUM_COMMITMENT:
        return true; // handled per block
    }

    return state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-tx-type-proc");
}

bool UndoEvoTx(const CTransaction& tx, const CBlockIndex* pindex)
{
    if (tx.nVersion != TX_EVO_VERSION || tx.nType == TRANSACTION_NORMAL) {
        return true;
    }

    switch (tx.nType) {
    case TRANSACTION_PROVIDER_REGISTER:
    case TRANSACTION_PROVIDER_UPDATE_SERVICE:
    case TRANSACTION_PROVIDER_UPDATE_REGISTRAR:
    case TRANSACTION_PROVIDER_UPDATE_REVOKE:
        return true; // handled in batches per block
    case TRANSACTION_COINBASE:
        return true; // nothing to do
    case TRANSACTION_QUORUM_COMMITMENT:
        return true; // handled per block
    }

    return false;
}

bool ProcessEvoTxsInBlock(const CBlock& block, const CBlockIndex* pindex, TxValidationState& state, bool fCheckCbTxMerkleRoots)
{
    int64_t nTimeLoop = 0;
    try {
        int64_t nTime1 = GetTimeMicros();
        for (int i = 0; i < (int)block.vtx.size(); i++) {
            const CTransaction& tx = *block.vtx[i];
            if (!CheckEvoTx(tx, pindex->pprev, state)) {
                return false;
            }
            if (!ProcessEvoTx(tx, pindex, state)) {
                return false;
            }
        }
        int64_t nTime2 = GetTimeMicros();
        nTimeLoop += nTime2 - nTime1;
        LogPrint(BCLog::BENCH, "        - ProcessEvoTxsInBlock: %.2fms [%.2fs]\n", 0.001 * (nTime2 - nTime1), nTimeLoop * 0.000001);
    } catch (const std::exception& e) {
        LogPrintf(strprintf("%s -- failed: %s\n", __func__, e.what()).c_str());
        return state.Invalid(TxValidationResult::TX_CONSENSUS, "failed-procspectxsinblock");
    }

    return true;
}

bool UndoEvoTxsInBlock(const CBlock& block, const CBlockIndex* pindex)
{
    int64_t nTimeLoop = 0;
    try {
        int64_t nTime1 = GetTimeMicros();
        for (int i = (int)block.vtx.size() - 1; i >= 0; --i) {
            const CTransaction& tx = *block.vtx[i];
            if (!UndoEvoTx(tx, pindex)) {
                return false;
            }
        }
        int64_t nTime2 = GetTimeMicros();
        nTimeLoop += nTime2 - nTime1;
        LogPrint(BCLog::BENCH, "        - UndoEvoTxsInBlock: %.2fms [%.2fs]\n", 0.001 * (nTime2 - nTime1), nTimeLoop * 0.000001);
    } catch (const std::exception& e) {
        return error(strprintf("%s -- failed: %s\n", __func__, e.what()).c_str());
    }

    return true;
}

uint256 CalcEvoTxInputsHash(const CTransaction& tx)
{
    CHashWriter hw(CLIENT_VERSION, SER_GETHASH);
    for (const auto& in : tx.vin) {
        hw << in.prevout;
    }
    return hw.GetHash();
}
