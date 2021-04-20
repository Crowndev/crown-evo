// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2020 The Bitcoin Core developers
// Copyright (c) 2020 The Crown developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CROWN_NODEWALLET_H
#define CROWN_NODEWALLET_H

#include <consensus/validation.h>
#include <masternode/activemasternode.h>
#include <masternode/masternode-budget.h>
#include <masternode/masternode-payments.h>
#include <masternode/masternode-sync.h>
#include <masternode/masternodeman.h>
#include <miner.h>
#include <node/context.h>
#include <pos/kernel.h>
#include <pos/stakeminer.h>
#include <rpc/blockchain.h>
#include <shutdown.h>
#include <systemnode/activesystemnode.h>
#include <systemnode/systemnode-payments.h>
#include <systemnode/systemnode-sync.h>
#include <systemnode/systemnodeman.h>
#include <util/translation.h>
#include <wallet/coincontrol.h>
#include <wallet/wallet.h>

class NodeWallet {
public:
    bool GetMasternodeVinAndKeys(CTxIn& txinRet, CPubKey& pubKeyRet, CKey& keyRet, std::shared_ptr<CWallet> pwallet = GetMainWallet());
    bool GetSystemnodeVinAndKeys(CTxIn& txinRet, CPubKey& pubKeyRet, CKey& keyRet, std::shared_ptr<CWallet> pwallet = GetMainWallet());
    bool GetVinAndKeysFromOutput(COutput out, CTxIn& txinRet, CPubKey& pubkeyRet, CKey& keyRet, std::shared_ptr<CWallet> pwallet = GetMainWallet());
    bool GetBudgetSystemCollateralTX(CTransactionRef& tx, uint256 hash, std::shared_ptr<CWallet> pwallet = GetMainWallet());
    bool CreateCoinStake(const int nHeight, const uint32_t& nBits, const uint32_t& nTime, CMutableTransaction& txCoinStake, uint32_t& nTxNewTime, StakePointer& stakePointer, std::shared_ptr<CWallet> pwallet = GetMainWallet());
    bool GetActiveMasternode(CMasternode*& activeStakingNode);
    bool GetActiveSystemnode(CSystemnode*& activeStakingNode);
    uint256 GenerateStakeModifier(const CBlockIndex* prewardBlockIndex) const;
    bool GetRecentStakePointers(std::vector<StakePointer>& vStakePointers);
};

void GetScriptForMining(CScript& script, std::shared_ptr<CWallet> wallet = GetMainWallet());
void NodeMinter(const CChainParams& chainparams, CConnman& connman);

extern NodeWallet currentNode;

#endif // CROWN_NODEWALLET_H
