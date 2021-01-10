// Copyright (c) 2014-2020 The Crown Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <mn_processing.h>

#include <chainparams.h>
#include <net_processing.h>
#include <netmessagemaker.h>
#include <protocol.h>
#include <util/system.h>
#include <util/strencodings.h>

#include <crown/instantx.h>
#include <crown/spork.h>
#include <masternode/masternodeman.h>
#include <systemnode/systemnodeman.h>
#include <masternode/masternode-budget.h>
#include <masternode/masternode-payments.h>
#include <systemnode/systemnode-payments.h>

#include <memory>
#include <typeinfo>

#if defined(NDEBUG)
# error "Bitcoin cannot be compiled without assertions."
#endif

//////////////////////////////////////////////////////////////////////////////
//
// Messages
//

bool AlreadyHaveMasternodeTypes(const CInv& inv, const CTxMemPool& mempool)
{
    switch (inv.type)
    {
        case MSG_TXLOCK_REQUEST:
            return instantSend.TxLockRequested(inv.hash);
        case MSG_TXLOCK_VOTE:
            return instantSend.AlreadyHave(inv.hash);
        case MSG_SPORK:
            return mapSporks.count(inv.hash);
        case MSG_MASTERNODE_WINNER:
            if(masternodePayments.mapMasternodePayeeVotes.count(inv.hash)) {
                masternodeSync.AddedMasternodeWinner(inv.hash);
                return true;
            }
            return false;
        case MSG_BUDGET_VOTE:
            if(budget.HasItem(inv.hash)) {
                masternodeSync.AddedBudgetItem(inv.hash);
                return true;
            }
            return false;
        case MSG_BUDGET_PROPOSAL:
            if(budget.HasItem(inv.hash)) {
                masternodeSync.AddedBudgetItem(inv.hash);
                return true;
            }
            return false;
        case MSG_BUDGET_FINALIZED_VOTE:
            if(budget.HasItem(inv.hash)) {
                masternodeSync.AddedBudgetItem(inv.hash);
                return true;
            }
            return false;
        case MSG_BUDGET_FINALIZED:
            if(budget.HasItem(inv.hash)) {
                masternodeSync.AddedBudgetItem(inv.hash);
                return true;
            }
            return false;
        case MSG_MASTERNODE_ANNOUNCE:
            if(mnodeman.mapSeenMasternodeBroadcast.count(inv.hash)) {
                masternodeSync.AddedMasternodeList(inv.hash);
                return true;
            }
            return false;
        case MSG_MASTERNODE_PING:
            return mnodeman.mapSeenMasternodePing.count(inv.hash);
        case MSG_SYSTEMNODE_WINNER:
            if(systemnodePayments.mapSystemnodePayeeVotes.count(inv.hash)) {
                systemnodeSync.AddedSystemnodeWinner(inv.hash);
                return true;
            }
            return false;
        case MSG_SYSTEMNODE_ANNOUNCE:
            if(snodeman.mapSeenSystemnodeBroadcast.count(inv.hash)) {
                systemnodeSync.AddedSystemnodeList(inv.hash);
                return true;
            }
            return false;
        case MSG_SYSTEMNODE_PING:
            return snodeman.mapSeenSystemnodePing.count(inv.hash);
    }
    // Don't know what it is, just say we already got one
    return true;
}

void ProcessGetDataMasternodeTypes(CNode* pfrom, const CChainParams& chainparams, CConnman* connman, const CTxMemPool& mempool, const CInv& inv, bool& pushed) LOCKS_EXCLUDED(cs_main)
{
    const CNetMsgMaker msgMaker(PROTOCOL_VERSION);
    {
        //! common spork
        if (!pushed && inv.type == MSG_SPORK) {
            if(mapSporks.count(inv.hash)) {
                connman->PushMessage(pfrom, msgMaker.Make(NetMsgType::SPORK, mapSporks[inv.hash]));
                pushed = true;
            }
        }

        //! instantsend types
        if (!pushed && inv.type == MSG_TXLOCK_VOTE) {
            if(instantSend.GetLockVote(inv.hash)) {
//              connman->PushMessage(pfrom, msgMaker.Make("txlvote", instantSend.GetLockVote(inv.hash)));
                pushed = true;
            }
        }
        if (!pushed && inv.type == MSG_TXLOCK_REQUEST) {
            if(instantSend.GetLockReq(inv.hash)) {
//             connman->PushMessage(pfrom, msgMaker.Make("ix", instantSend.GetLockReq(inv.hash)));
                pushed = true;
            }
        }

        //! masternode types
        if (!pushed && inv.type == MSG_MASTERNODE_WINNER) {
            if (masternodePayments.mapMasternodePayeeVotes.count(inv.hash)) {
                connman->PushMessage(pfrom, msgMaker.Make(NetMsgType::MNWINNER, masternodePayments.mapMasternodePayeeVotes[inv.hash]));
                pushed = true;
            }
        }
        if (!pushed && inv.type == MSG_MASTERNODE_ANNOUNCE) {
            if(mnodeman.mapSeenMasternodeBroadcast.count(inv.hash)) {
                if (pfrom->nVersion < MIN_MNW_PING_VERSION)
                    connman->PushMessage(pfrom, msgMaker.Make("mnb", mnodeman.mapSeenMasternodeBroadcast[inv.hash]));
                else
                    connman->PushMessage(pfrom, msgMaker.Make("mnb_new", mnodeman.mapSeenMasternodeBroadcast[inv.hash]));
                pushed = true;
            }
        }
        if (!pushed && inv.type == MSG_MASTERNODE_PING) {
            if(mnodeman.mapSeenMasternodePing.count(inv.hash)){
                connman->PushMessage(pfrom, msgMaker.Make(NetMsgType::MNPING, mnodeman.mapSeenMasternodePing[inv.hash]));
                pushed = true;
            }
        }

        //! systemnode types
        if (!pushed && inv.type == MSG_SYSTEMNODE_WINNER) {
            if(systemnodePayments.mapSystemnodePayeeVotes.count(inv.hash)){
                connman->PushMessage(pfrom, msgMaker.Make("snw", masternodePayments.mapMasternodePayeeVotes[inv.hash]));
                pushed = true;
            }
        }
        if (!pushed && inv.type == MSG_SYSTEMNODE_ANNOUNCE) {
            if(snodeman.mapSeenSystemnodeBroadcast.count(inv.hash)){
                connman->PushMessage(pfrom, msgMaker.Make("snb", mnodeman.mapSeenMasternodeBroadcast[inv.hash]));
                pushed = true;
            }
        }
        if (!pushed && inv.type == MSG_SYSTEMNODE_PING) {
            if(snodeman.mapSeenSystemnodePing.count(inv.hash)){
                connman->PushMessage(pfrom, msgMaker.Make("snp", mnodeman.mapSeenMasternodePing[inv.hash]));
                pushed = true;
            }
        }

        //! budget types
        if (!pushed && inv.type == MSG_BUDGET_VOTE) {
            const CBudgetVote* item = budget.GetSeenVote(inv.hash);
            if(item){
                CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
                ss.reserve(1000);
                ss << *item;
                connman->PushMessage(pfrom, msgMaker.Make("mvote", ss));
                pushed = true;
            }
        }
        if (!pushed && inv.type == MSG_BUDGET_PROPOSAL) {
            const CBudgetProposalBroadcast* item = budget.GetSeenProposal(inv.hash);
            if(item){
                CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
                ss.reserve(1000);
                ss << *item;
                connman->PushMessage(pfrom, msgMaker.Make("mprop", ss));
                pushed = true;
            }
        }
        if (!pushed && inv.type == MSG_BUDGET_FINALIZED_VOTE) {
            const BudgetDraftVote* item = budget.GetSeenBudgetDraftVote(inv.hash);
            if(item){
                CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
                ss.reserve(1000);
                ss << *item;
                connman->PushMessage(pfrom, msgMaker.Make("fbvote", ss));
                pushed = true;
            }
        }
        if (!pushed && inv.type == MSG_BUDGET_FINALIZED) {
            const BudgetDraftBroadcast* item = budget.GetSeenBudgetDraft(inv.hash);
            if(item){
                CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
                ss.reserve(1000);
                ss << *item;
                connman->PushMessage(pfrom, msgMaker.Make("fbs", ss));
                pushed = true;
            }
        }
    }
}

bool ProcessMessageMasternodeTypes(CNode* pfrom, const std::string& msg_type, CDataStream& vRecv, const CChainParams& chainparams, CTxMemPool& mempool, CConnman* connman, BanMan* banman, const std::atomic<bool>& interruptMsgProc)
{
    bool found = false;
    const std::vector<std::string> &allMessages = getAllNetMessageTypes();
    for (const std::string msg : allMessages) {
        if (msg == msg_type) {
            found = true;
             break;
        }
    }

    if (found) {
            mnodeman.ProcessMessage(pfrom, msg_type, vRecv, connman);
            snodeman.ProcessMessage(pfrom, msg_type, vRecv, connman);
            budget.ProcessMessage(pfrom, msg_type, vRecv, connman);
            masternodePayments.ProcessMessageMasternodePayments(pfrom, msg_type, vRecv, connman);
            systemnodePayments.ProcessMessageSystemnodePayments(pfrom, msg_type, vRecv, connman);
            instantSend.ProcessMessage(pfrom, msg_type, vRecv, connman);
            ProcessSpork(pfrom, connman, msg_type, vRecv);
            masternodeSync.ProcessMessage(pfrom, msg_type, vRecv, connman);
            systemnodeSync.ProcessMessage(pfrom, msg_type, vRecv, connman);
    }

    return true;
}
