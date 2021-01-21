// Copyright (c) 2014-2017 The Dash Core developers
// Copyright (c) 2014-2018 The Crown developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <crown/nodesync.h>

#include <crown/instantx.h>
#include <index/txindex.h>
#include <init.h>
#include <masternode/masternodeman.h>
#include <node/context.h>
#include <rpc/blockchain.h>
#include <rpc/util.h>
#include <script/sign.h>
#include <shutdown.h>
#include <systemnode/systemnodeman.h>
#include <util/message.h>
#include <util/system.h>
#include <util/time.h>
#include <wallet/rpcwallet.h>
#include <wallet/wallet.h>

#include <boost/algorithm/string/replace.hpp>
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/lexical_cast.hpp>

#include <algorithm>
#include <boost/assign/list_of.hpp>
#include <openssl/rand.h>

void ThreadNodeSync(CConnman& connman)
{
    util::ThreadRename("crown-nodesync");

    if (fReindex || fImporting)
        return;
    if (::ChainstateActive().IsInitialBlockDownload())
        return;
    if (ShutdownRequested())
        return;

    static unsigned int c1 = 0;
    static unsigned int c2 = 0;

    bool stageone = masternodeSync.IsBlockchainSynced();
    bool stagetwo = masternodeSync.IsSynced();

    // try to sync from all available nodes, one step at a time
    masternodeSync.Process(connman);
    if (stagetwo)
        systemnodeSync.Process(connman);

    if (stageone) {

        c1++;

        mnodeman.Check();

        // check if we should activate or ping every few minutes,
        // start right after sync is considered to be done
        if (c1 % MASTERNODE_PING_SECONDS == 15)
            activeMasternode.ManageStatus(connman);

        if (c1 % 60 == 0) {
            mnodeman.CheckAndRemove();
            mnodeman.ProcessMasternodeConnections(connman);
            masternodePayments.CheckAndRemove();
            instantSend.CheckAndRemove();
        }
    }

    if (stageone && stagetwo) {

        c2++;

        snodeman.Check();

        // check if we should activate or ping every few minutes,
        // start right after sync is considered to be done
        if (c2 % SYSTEMNODE_PING_SECONDS == 15)
            activeSystemnode.ManageStatus(connman);

        if (c2 % 60 == 0) {
            snodeman.CheckAndRemove();
            snodeman.ProcessSystemnodeConnections(connman);
            systemnodePayments.CheckAndRemove();
            instantSend.CheckAndRemove();
        }
    }
}
