// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2020 The Bitcoin Core developers
// Copyright (c) 2020 The Crown developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <crown/nodewallet.h>

bool CWallet::GetMasternodeVinAndKeys(CTxIn& txinRet, CPubKey& pubKeyRet, CKey& keyRet, std::string strTxHash, std::string strOutputIndex)
{
    LOCK(cs_wallet);
    std::vector<COutput> vPossibleCoins;
    AvailableCoins(vPossibleCoins, true, nullptr, Params().GetConsensus().nMasternodeCollateral, Params().GetConsensus().nMasternodeCollateral);
    if (vPossibleCoins.empty()) {
        LogPrintf("CWallet::GetMasternodeVinAndKeys -- Could not locate any valid masternode vin\n");
        return false;
    }

    uint256 txHash = uint256S(strTxHash);
    int nOutputIndex = atoi(strOutputIndex.c_str());

    for (COutput& out : vPossibleCoins) {
        if (out.tx->GetHash() == txHash && out.i == nOutputIndex) {
            return GetVinAndKeysFromOutput(out, txinRet, pubKeyRet, keyRet);
        }
    }

    LogPrintf("CWallet::GetMasternodeVinAndKeys - Could not locate specified masternode vin\n");
    return false;
}

bool CWallet::GetSystemnodeVinAndKeys(CTxIn& txinRet, CPubKey& pubKeyRet, CKey& keyRet, std::string strTxHash, std::string strOutputIndex)
{
    LOCK(cs_wallet);
    std::vector<COutput> vPossibleCoins;
    AvailableCoins(vPossibleCoins, true, nullptr, Params().GetConsensus().nSystemnodeCollateral, Params().GetConsensus().nSystemnodeCollateral);
    if (vPossibleCoins.empty()) {
        LogPrintf("CWallet::GetSystemnodeVinAndKeys -- Could not locate any valid systemnode vin\n");
        return false;
    }

    uint256 txHash = uint256S(strTxHash);
    int nOutputIndex = atoi(strOutputIndex.c_str());

    for (COutput& out : vPossibleCoins) {
        if (out.tx->GetHash() == txHash && out.i == nOutputIndex) {
            return GetVinAndKeysFromOutput(out, txinRet, pubKeyRet, keyRet);
        }
    }

    LogPrintf("CWallet::GetSystemnodeVinAndKeys - Could not locate specified systemnode vin\n");
    return false;
}

bool CWallet::GetVinAndKeysFromOutput(COutput out, CTxIn& txinRet, CPubKey& pubkeyRet, CKey& keyRet)
{
    CScript pubScript;
    CKeyID keyID;

    txinRet = CTxIn(out.tx->tx->GetHash(), out.i);
    pubScript = out.tx->tx->vout[out.i].scriptPubKey;

    CTxDestination address;
    ExtractDestination(pubScript, address);
    auto key_id = boost::get<PKHash>(&address);
    keyID = ToKeyID(*key_id);
    if (!key_id) {
        LogPrintf("GetVinFromOutput -- Address does not refer to a key\n");
        return false;
    }

    LegacyScriptPubKeyMan* spk_man = GetLegacyScriptPubKeyMan();
    if (!spk_man) {
        LogPrintf("GetVinFromOutput -- This type of wallet does not support this command\n");
        return false;
    }

    if (!spk_man->GetKey(keyID, keyRet)) {
        LogPrintf("GetVinFromOutput -- Private key for address is not known\n");
        return false;
    }

    pubkeyRet = keyRet.GetPubKey();
    return true;
}

bool CWallet::GetBudgetSystemCollateralTX(CTransactionRef& tx, uint256 hash)
{
    const CAmount BUDGET_FEE_TX = (25 * COIN);

    CScript scriptChange;
    scriptChange << OP_RETURN << ToByteVector(hash);

    std::vector<CRecipient> vecSend;
    vecSend.push_back((CRecipient) { scriptChange, BUDGET_FEE_TX, false });

    CCoinControl coinControl;
    int nChangePosRet = -1;
    CAmount nFeeRequired = 0;
    bilingual_str error;
    FeeCalculation fee_calc_out;

    return CreateTransaction(vecSend, tx, nFeeRequired, nChangePosRet, error, coinControl, fee_calc_out);
}

bool CWallet::GetActiveMasternode(CMasternode*& activeStakingNode)
{
    activeStakingNode = nullptr;
    if (activeMasternode.status == ACTIVE_MASTERNODE_STARTED)
        activeStakingNode = mnodeman.Find(activeMasternode.vin);
    return activeStakingNode != nullptr;
}

bool CWallet::GetActiveSystemnode(CSystemnode*& activeStakingNode)
{
    activeStakingNode = nullptr;
    if (activeSystemnode.status == ACTIVE_SYSTEMNODE_STARTED)
        activeStakingNode = snodeman.Find(activeSystemnode.vin);
    return activeStakingNode != nullptr;
}

uint256 CWallet::GenerateStakeModifier(const CBlockIndex* prewardBlockIndex) const
{
    if (!prewardBlockIndex)
        return uint256();

    const CBlockIndex* pstakeModBlockIndex = prewardBlockIndex->GetAncestor(prewardBlockIndex->nHeight - Params().GetConsensus().KernelModifierOffset());
    if (!pstakeModBlockIndex) {
        LogPrintf("GenerateStakeModifier -- Failed retrieving block index for stake modifier\n");
        return uint256();
    }

    return pstakeModBlockIndex->GetBlockHash();
}

void GetScriptForMining(CScript& script, std::shared_ptr<CWallet> wallet)
{
    auto pwallet = wallet.get();
    ReserveDestination reservedest(pwallet, OutputType::LEGACY);

    //! this requires a lock, yet cannot fail; so just remove it altogether

    CTxDestination dest;
    bool ret = reservedest.GetReservedDestination(dest, true);
    if (!ret) {
        LogPrintf("%s: keypool ran out, please call keypoolrefill first", __func__);
        return;
    }

    script = GetScriptForDestination(dest);
}

void NodeMinter(const CChainParams& chainparams, CConnman& connman)
{
    util::ThreadRename("crown-minter");

    auto pwallet = GetMainWallet();
    if (!pwallet)
        return;

    if (ShutdownRequested())
        return;
    if (!fMasterNode && !fSystemNode)
        return;
    if (fReindex || fImporting || pwallet->IsLocked())
        return;
    if (!gArgs.GetBoolArg("-jumpstart", false)) {
        if (connman.GetNodeCount(CConnman::CONNECTIONS_ALL) == 0 ||
            ::ChainActive().Tip()->nHeight+1 < chainparams.GetConsensus().PoSStartHeight() ||
            ::ChainstateActive().IsInitialBlockDownload() ||
            !masternodeSync.IsSynced() || !systemnodeSync.IsSynced()) {
                return;
        }
    }

    LogPrintf("%s: Attempting to stake..\n", __func__);

    unsigned int nExtraNonce = 0;

    CScript coinbaseScript;
    GetScriptForMining(coinbaseScript, pwallet);
    if (coinbaseScript.empty()) return;

    //
    // Create new block
    //
    CBlockIndex* pindexPrev = ::ChainActive().Tip();
    if (!pindexPrev) return;

    BlockAssembler assembler(*g_rpc_node->mempool, chainparams);
    auto pblocktemplate = assembler.CreateNewBlock(coinbaseScript, pwallet.get(), true);
    if (!pblocktemplate.get()) {
        LogPrintf("%s: Stake not found..\n", __func__);
        return;
    }

    auto pblock = std::make_shared<CBlock>(pblocktemplate->block);
    IncrementExtraNonce(pblock.get(), pindexPrev, nExtraNonce);

    // sign block
    LogPrintf("CPUMiner : proof-of-stake block found %s\n", pblock->GetHash().ToString());
    if (!SignBlock(pblock.get())) {
        LogPrintf("%s: SignBlock failed", __func__);
        return;
    }
    LogPrintf("%s : proof-of-stake block was signed %s\n", __func__, pblock->GetHash().ToString());

    // check if block is valid
    BlockValidationState state;
    if (!TestBlockValidity(state, chainparams, *pblock, pindexPrev, false, false)) {
        LogPrintf("%s: TestBlockValidity failed: %s", __func__, state.ToString());
        return;
    }

    //! guts of ProcessBlockFound()
    if (pblock->hashPrevBlock != ::ChainActive().Tip()->GetBlockHash()) {
        LogPrintf("%s - generated block is stale\n", __func__);
        return;
    } else {
        LOCK(cs_main);
        if (!g_chainman.ProcessNewBlock(chainparams, pblock, true, nullptr)) {
            LogPrintf("%s - ProcessNewBlock() failed, block not accepted\n", __func__);
            return;
        }
    }

    return;
}

#define STAKE_SEARCH_INTERVAL 30
bool CWallet::CreateCoinStake(const int nHeight, const uint32_t& nBits, const uint32_t& nTime, CMutableTransaction& txCoinStake, uint32_t& nTxNewTime, StakePointer& stakePointer)
{
    CTxIn* pvinActiveNode;
    CPubKey* ppubkeyActiveNode;
    int nActiveNodeInputHeight;
    std::vector<StakePointer> vStakePointers;
    CAmount nAmountMN;

    //! Maybe have a polymorphic base class for masternode and systemnode?
    if (fMasterNode) {
        CMasternode* activeStakingNode;
        if (!GetActiveMasternode(activeStakingNode)) {
            LogPrintf("CreateCoinStake -- Couldn't find CMasternode object for active masternode\n");
            return false;
        }
        if (!GetRecentStakePointers(vStakePointers)) {
            LogPrintf("CreateCoinStake -- Couldn't find recent payment blocks for MN\n");
            return false;
        }
        pvinActiveNode = &activeStakingNode->vin;
        ppubkeyActiveNode = &activeStakingNode->pubkey;
        nActiveNodeInputHeight = ::ChainActive().Height() - activeStakingNode->GetMasternodeInputAge();
        nAmountMN = static_cast<CAmount>(Params().GetConsensus().nMasternodeCollateral);

    } else if (fSystemNode) {
        CSystemnode* activeStakingNode;
        if (!GetActiveSystemnode(activeStakingNode)) {
            LogPrintf("CreateCoinStake -- Couldn't find CSystemnode object for active systemnode\n");
            return false;
        }
        if (!GetRecentStakePointers(vStakePointers)) {
            LogPrintf("CreateCoinStake -- Couldn't find recent payment blocks for SN\n");
            return false;
        }
        pvinActiveNode = &activeStakingNode->vin;
        ppubkeyActiveNode = &activeStakingNode->pubkey;
        nActiveNodeInputHeight = ::ChainActive().Height() - activeStakingNode->GetSystemnodeInputAge();
        nAmountMN = static_cast<CAmount>(Params().GetConsensus().nSystemnodeCollateral);

    } else {
        LogPrintf("CreateCoinStake -- Must be masternode or systemnode to create coin stake!\n");
        return false;
    }

    //Create kernels for each valid stake pointer and see if any create a successful proof
    for (auto pointer : vStakePointers) {
        if (!g_chainman.BlockIndex().count(pointer.hashBlock))
            continue;

        CBlockIndex* pindex = g_chainman.BlockIndex().at(pointer.hashBlock);

        // Make sure this pointer is not too deep
        if (nHeight - pindex->nHeight >= Params().GetConsensus().ValidStakePointerDuration() + 1)
            continue;

        // check that collateral transaction happened long enough before this stake pointer
        if (pindex->nHeight - Params().GetConsensus().KernelModifierOffset() <= nActiveNodeInputHeight)
            continue;

        // generate stake modifier based off block that happened before this stake pointer
        uint256 nStakeModifier = GenerateStakeModifier(pindex);
        if (nStakeModifier == uint256())
            continue;

        auto pOutpoint = std::make_pair(pointer.txid, pointer.nPos);
        Kernel kernel(pOutpoint, nAmountMN, nStakeModifier, pindex->GetBlockTime(), nTxNewTime);
        uint256 nTarget = ArithToUint256(arith_uint256().SetCompact(nBits));
        nLastStakeAttempt = GetTime();

        if (!SearchTimeSpan(kernel, nTime, nTime + STAKE_SEARCH_INTERVAL, nTarget))
            continue;

        LogPrintf("%s: Found valid kernel for mn/sn collateral %s\n", __func__, pvinActiveNode->prevout.ToString());
        LogPrintf("%s: %s\n", __func__, kernel.ToString());

        //Add stake payment to coinstake tx
        CAmount nBlockReward = GetBlockValue(nHeight, 0, Params().GetConsensus()); //Do not add fees until after they are packaged into the block
        CScript scriptBlockReward = GetScriptForDestination(PKHash(*ppubkeyActiveNode));
        CTxOut out(nBlockReward, scriptBlockReward);
        txCoinStake.vout.emplace_back(out);
        nTxNewTime = kernel.GetTime();
        stakePointer = pointer;

        CTxIn txin;
        txin.scriptSig << OP_PROOFOFSTAKE;
        txCoinStake.vin.emplace_back(txin);

        return true;
    }

    return false;
}

template <typename stakingnode>
bool GetPointers(stakingnode* pstaker, std::vector<StakePointer>& vStakePointers, int nPaymentSlot)
{
    bool found = false;
    // get block index of last mn payment
    std::vector<const CBlockIndex*> vBlocksLastPaid;
    if (!pstaker->GetRecentPaymentBlocks(vBlocksLastPaid, false)) {
        LogPrintf("GetRecentStakePointer -- Couldn't find last paid block\n");
        return false;
    }

    int nBestHeight = ::ChainActive().Height();
    for (auto pindex : vBlocksLastPaid) {
        if (budget.IsBudgetPaymentBlock(pindex->nHeight))
            continue;

        // Pointer has to be at least deeper than the max reorg depth
        const int nMaxReorganizationDepth = 100;
        if (nBestHeight - pindex->nHeight < nMaxReorganizationDepth)
            continue;

        CBlock blockLastPaid;
        if (!ReadBlockFromDisk(blockLastPaid, pindex, Params().GetConsensus())) {
            LogPrintf("GetRecentStakePointer -- Failed reading block from disk\n");
            return false;
        }

        CScript scriptMNPubKey;
        scriptMNPubKey = GetScriptForDestination(PKHash(pstaker->pubkey));
        for (auto& tx : blockLastPaid.vtx) {
            auto stakeSource = COutPoint(tx->GetHash(), nPaymentSlot);
            uint256 hashPointer = stakeSource.GetHash();
            if (mapUsedStakePointers.count(hashPointer))
                continue;
            if (tx->IsCoinBase() && tx->vout[nPaymentSlot].scriptPubKey == scriptMNPubKey) {
                StakePointer stakePointer;
                stakePointer.hashBlock = pindex->GetBlockHash();
                stakePointer.txid = tx->GetHash();
                stakePointer.nPos = nPaymentSlot;
                stakePointer.pubKeyProofOfStake = pstaker->pubkey;
                vStakePointers.emplace_back(stakePointer);
                found = true;
                continue;
            }
        }
    }

    return found;
}

bool CWallet::GetRecentStakePointers(std::vector<StakePointer>& vStakePointers)
{
    if (fMasterNode) {
        // find pointer to active CMasternode object
        CMasternode* pactiveMN;
        if (!GetActiveMasternode(pactiveMN))
            return error("GetRecentStakePointer -- Couldn't find CMasternode object for active masternode\n");

        return GetPointers(pactiveMN, vStakePointers, MN_PMT_SLOT);
    }

    CSystemnode* pactiveSN;
    if (!GetActiveSystemnode(pactiveSN))
        return error("GetRecentStakePointer -- Couldn't find CSystemnode object for active systemnode\n");

    return GetPointers(pactiveSN, vStakePointers, SN_PMT_SLOT);
}


