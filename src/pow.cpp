// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <pow.h>

#include <arith_uint256.h>
#include <auxpow.h>
#include <chain.h>
#include <primitives/block.h>
#include <uint256.h>
#include <validation.h>

unsigned int static DarkGravityWave(const CBlockIndex* pindexLast, const Consensus::Params& params)
{
    /* current difficulty formula, crown - DarkGravity v3, written by Evan Duffield - evan@crown.tech */
    arith_uint256 nProofOfWorkLimit = pindexLast->nHeight >= 1059780 ? UintToArith256(uint256S("000003ffff000000000000000000000000000000000000000000000000000000")) : UintToArith256(params.powLimit);
    const CBlockIndex* BlockLastSolved = pindexLast;
    const CBlockIndex* BlockReading = pindexLast;
    int64_t nActualTimespan = 0;
    int64_t LastBlockTime = 0;
    int64_t PastBlocksMin = 24;
    int64_t PastBlocksMax = 24;
    int64_t CountBlocks = 0;
    arith_uint256 PastDifficultyAverage;
    arith_uint256 PastDifficultyAveragePrev;

    bool isAdjustmentPeriod = BlockLastSolved->nHeight >= params.PoSStartHeight() - 1 && BlockLastSolved->nHeight < params.PoSStartHeight() + PastBlocksMax;
    if (BlockLastSolved == nullptr || BlockLastSolved->nHeight == 0 || BlockLastSolved->nHeight < PastBlocksMin || isAdjustmentPeriod) {
        if (Params().NetworkIDString() != CBaseChainParams::TESTNET)
            return nProofOfWorkLimit.GetCompact();
    }

    for (unsigned int i = 1; BlockReading && BlockReading->nHeight > 0; i++) {
        if (PastBlocksMax > 0 && i > PastBlocksMax) {
            break;
        }
        CountBlocks++;

        if (CountBlocks <= PastBlocksMin) {
            if (CountBlocks == 1) {
                PastDifficultyAverage.SetCompact(BlockReading->nBits);
            } else {
                PastDifficultyAverage = ((PastDifficultyAveragePrev * CountBlocks) + (arith_uint256().SetCompact(BlockReading->nBits))) / (CountBlocks + 1);
            }
            PastDifficultyAveragePrev = PastDifficultyAverage;
        }

        if (LastBlockTime > 0) {
            int64_t Diff = (LastBlockTime - BlockReading->GetBlockTime());
            nActualTimespan += Diff;
        }
        LastBlockTime = BlockReading->GetBlockTime();

        if (BlockReading->pprev == nullptr) {
            assert(BlockReading);
            break;
        }
        BlockReading = BlockReading->pprev;
    }

    arith_uint256 bnNew(PastDifficultyAverage);

    int64_t _nTargetTimespan = CountBlocks * params.nPowTargetSpacing;

    if (nActualTimespan < _nTargetTimespan / 3)
        nActualTimespan = _nTargetTimespan / 3;
    if (nActualTimespan > _nTargetTimespan * 3)
        nActualTimespan = _nTargetTimespan * 3;

    // Retarget
    bnNew *= nActualTimespan;
    bnNew /= _nTargetTimespan;

    if (bnNew > nProofOfWorkLimit) {
        bnNew = nProofOfWorkLimit;
    }

    return bnNew.GetCompact();
}

unsigned int GetNextWorkRequired(const CBlockIndex* pindexLast, const CBlockHeader* pblock, const Consensus::Params& params)
{
    assert(pindexLast != nullptr);
    unsigned int nProofOfWorkLimit = 0;
    unsigned int retarget = DIFF_DGW;

    if (pindexLast->nHeight + 1 >= 1059780)
        retarget = DIFF_DGW;
    else
        retarget = DIFF_BTC;
    if (Params().NetworkIDString() == CBaseChainParams::TESTNET && pindexLast->nHeight >= 140400)
        retarget = DIFF_DGW;
    if (retarget == DIFF_BTC) {
        nProofOfWorkLimit = UintToArith256(params.powLimit).GetCompact();

        // Only change once per difficulty adjustment interval
        if ((pindexLast->nHeight + 1) % params.DifficultyAdjustmentInterval() != 0) {
            if (params.fPowAllowMinDifficultyBlocks) {
                // Special difficulty rule for testnet:
                // If the new block's timestamp is more than 2* 10 minutes
                // then allow mining of a min-difficulty block.
                if (pblock->GetBlockTime() > pindexLast->GetBlockTime() + params.nPowTargetSpacing * 2)
                    return nProofOfWorkLimit;
                else {
                    // Return the last non-special-min-difficulty-rules-block
                    const CBlockIndex* pindex = pindexLast;
                    while (pindex->pprev && pindex->nHeight % params.DifficultyAdjustmentInterval() != 0 && pindex->nBits == nProofOfWorkLimit)
                        pindex = pindex->pprev;
                    return pindex->nBits;
                }
            }
            return pindexLast->nBits;
        }

        // Go back by what we want to be 14 days worth of blocks
        int nHeightFirst = pindexLast->nHeight - (params.DifficultyAdjustmentInterval() - 1);
        assert(nHeightFirst >= 0);
        const CBlockIndex* pindexFirst = pindexLast->GetAncestor(nHeightFirst);
        assert(pindexFirst);

        return CalculateNextWorkRequired(pindexLast, pindexFirst->GetBlockTime(), params);
    }
    // Retarget using Dark Gravity Wave 3
    else if (retarget == DIFF_DGW) {
        return DarkGravityWave(pindexLast, params);
    }

    return DarkGravityWave(pindexLast, params);
}

unsigned int CalculateNextWorkRequired(const CBlockIndex* pindexLast, int64_t nFirstBlockTime, const Consensus::Params& params)
{
    if (params.fPowNoRetargeting)
        return pindexLast->nBits;

    // Limit adjustment step
    int64_t nActualTimespan = pindexLast->GetBlockTime() - nFirstBlockTime;
    if (nActualTimespan < params.nPowTargetTimespan / 4)
        nActualTimespan = params.nPowTargetTimespan / 4;
    if (nActualTimespan > params.nPowTargetTimespan * 4)
        nActualTimespan = params.nPowTargetTimespan * 4;

    // Retarget
    const arith_uint256 bnPowLimit = UintToArith256(params.powLimit);
    arith_uint256 bnNew;
    bnNew.SetCompact(pindexLast->nBits);
    bnNew *= nActualTimespan;
    bnNew /= params.nPowTargetTimespan;

    if (bnNew > bnPowLimit)
        bnNew = bnPowLimit;

    return bnNew.GetCompact();
}

bool CheckProofOfWork(uint256 hash, unsigned int nBits, const Consensus::Params& params)
{
    bool fNegative;
    bool fOverflow;
    arith_uint256 bnTarget;

    bnTarget.SetCompact(nBits, &fNegative, &fOverflow);

    // Check range
    if (fNegative || bnTarget == 0 || fOverflow)
        return false;

    // Check proof of work matches claimed amount
    if (UintToArith256(hash) > bnTarget)
        return false;

    return true;
}

bool CheckProofOfWork(const CBlockHeader& block, const Consensus::Params& params)
{
    if (!block.nVersion.IsLegacy() && params.fStrictChainId && block.nVersion.GetChainId() != params.AuxpowChainId()) {
        return error("%s : block does not have our chain ID (got %d, expected %d, full nVersion %d)",
                     __func__, block.nVersion.GetChainId(), params.AuxpowChainId(), block.nVersion.GetFullVersion());
    }

    if (!block.auxpow) {
        if (block.nVersion.IsAuxpow())
            return error("%s : no AuxPow on block with AuxPow version", __func__);
        if (!CheckProofOfWork(block.GetHash(), block.nBits, params))
            return error("%s : non-AUX proof of work failed", __func__);
        return true;
    }

    if (!block.nVersion.IsAuxpow())
        return error("%s : AuxPow on block with non-AuxPow version", __func__);
    if (block.auxpow->getParentBlock().nVersion.IsAuxpow())
        return error("%s : AuxPow parent block has AuxPow version", __func__);
    if (!block.auxpow->check(block.GetHash(), block.nVersion.GetChainId(), params))
        return error("%s : AuxPow is not valid", __func__);
    if (!CheckProofOfWork(block.auxpow->getParentBlockPoWHash(), block.nBits, params))
        return error("%s : AuxPow work failed", __func__);

    return true;
}
