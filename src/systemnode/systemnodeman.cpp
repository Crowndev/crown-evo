// Copyright (c) 2014-2018 The Crown developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <crown/legacysigner.h>
#include <net_processing.h>
#include <netmessagemaker.h>
#include <nodediag.h>

/** Systemnode manager */
CSystemnodeMan snodeman;

struct CompareLastPaid {
    bool operator()(const pair<int64_t, CTxIn>& t1,
        const pair<int64_t, CTxIn>& t2) const
    {
        return t1.first < t2.first;
    }
};

struct CompareScoreTxIn {
    bool operator()(const pair<int64_t, CTxIn>& t1,
        const pair<int64_t, CTxIn>& t2) const
    {
        return t1.first < t2.first;
    }
};

struct CompareScoreSN {
    bool operator()(const pair<int64_t, CSystemnode>& t1,
        const pair<int64_t, CSystemnode>& t2) const
    {
        return t1.first < t2.first;
    }
};

std::vector<pair<int, CSystemnode>> CSystemnodeMan::GetSystemnodeRanks(int64_t nBlockHeight, int minProtocol)
{
    std::vector<pair<int64_t, CSystemnode>> vecSystemnodeScores;
    std::vector<pair<int, CSystemnode>> vecSystemnodeRanks;

    //make sure we know about this block
    uint256 hash = uint256();
    if (!GetBlockHash(hash, nBlockHeight))
        return vecSystemnodeRanks;

    // scan for winner
    for (auto& sn : vSystemnodes) {
        sn.Check();
        if (sn.protocolVersion < minProtocol)
            continue;
        if (!sn.IsEnabled())
            continue;
        int64_t n2 = sn.CalculateScore(nBlockHeight).GetCompact(false);
        vecSystemnodeScores.push_back(make_pair(n2, sn));
    }
    sort(vecSystemnodeScores.rbegin(), vecSystemnodeScores.rend(), CompareScoreSN());

    int rank = 0;
    for (const auto& s : vecSystemnodeScores) {
        rank++;
        vecSystemnodeRanks.push_back(make_pair(rank, s.second));
    }

    return vecSystemnodeRanks;
}

void CSystemnodeMan::ProcessSystemnodeConnections(CConnman& connman)
{
    for (const auto& pnode : connman.CopyNodeVector()) {
        if (pnode->fSystemnode) {
            if (legacySigner.pSubmittedToSystemnode && pnode->addr == legacySigner.pSubmittedToSystemnode->addr)
                continue;
            LogPrint(BCLog::SYSTEMNODE, "Closing Systemnode connection %s \n", pnode->addr.ToString());
            pnode->fSystemnode = false;
            pnode->Release();
        }
    }
}

void CSystemnodeMan::ProcessMessage(CNode* pfrom, const std::string& strCommand, CDataStream& vRecv, CConnman* connman)
{
    if (!gArgs.GetBoolArg("-jumpstart", false)) {
        if (!systemnodeSync.IsBlockchainSynced())
            return;
    }

    //! systemnode broadcast
    if (strCommand == NetMsgType::SNBROADCAST) {
        CSystemnodeBroadcast snb;
        vRecv >> snb;
        if (nodeDiag)
            systemnodeDiag(&snb, nullptr);

        int nDoS = 0;
        if (CheckSnbAndUpdateSystemnodeList(snb, nDoS, *connman)) {
            // use announced Systemnode as a peer
            connman->addrman.Add(CAddress(snb.addr, NODE_NETWORK), pfrom->addr, 2 * 60 * 60);
        } else {
            if (nDoS > 0)
                Misbehaving(pfrom->GetId(), nDoS);
        }
    }

    //! systemnode ping
    else if (strCommand == NetMsgType::SNPING) {
        CSystemnodePing snp;
        vRecv >> snp;
        if (nodeDiag)
            systemnodeDiag(nullptr, &snp);

        LogPrint(BCLog::SYSTEMNODE, "snp - Systemnode ping, vin: %s\n", snp.vin.ToString());

        if (mapSeenSystemnodePing.count(snp.GetHash()))
            return; //seen
        mapSeenSystemnodePing.insert(make_pair(snp.GetHash(), snp));

        int nDoS = 0;
        if (snp.CheckAndUpdate(nDoS, *connman))
            return;

        if (nDoS > 0) {
            // if anything significant failed, mark that node
            Misbehaving(pfrom->GetId(), nDoS);
        } else {
            // if nothing significant failed, search existing Systemnode list
            CSystemnode* pmn = Find(snp.vin);
            // if it's known, don't ask for the mnb, just return
            if (pmn)
                return;
        }

        // something significant is broken or mn is unknown,
        // we might have to ask for a systemnode entry once
        AskForSN(pfrom, snp.vin, *connman);
    }

    else if (strCommand == NetMsgType::SNDSEG) {
        CTxIn vin;
        vRecv >> vin;

        if (vin == CTxIn()) { //only should ask for this once
            //local network
            bool isLocal = (pfrom->addr.IsRFC1918() || pfrom->addr.IsLocal());
            if (!isLocal) {
                std::map<CNetAddr, int64_t>::iterator i = mAskedUsForSystemnodeList.find(pfrom->addr);
                if (i != mAskedUsForSystemnodeList.end()) {
                    int64_t t = (*i).second;
                    if (GetTime() < t) {
                        Misbehaving(pfrom->GetId(), 34);
                        LogPrint(BCLog::SYSTEMNODE, "sndseg - peer already asked me for the list\n");
                        return;
                    }
                }
                int64_t askAgain = GetTime() + SYSTEMNODES_DSEG_SECONDS;
                mAskedUsForSystemnodeList[pfrom->addr] = askAgain;
            }
        } //else, asking for a specific node which is ok

        int nInvCount = 0;
        for (const auto& sn : vSystemnodes) {
            if (sn.IsEnabled()) {
                LogPrint(BCLog::SYSTEMNODE, "sndseg - Sending Systemnode entry - %s \n", sn.addr.ToString());
                if (vin == CTxIn() || vin == sn.vin) {
                    CSystemnodeBroadcast snb = CSystemnodeBroadcast(sn);
                    uint256 hash = snb.GetHash();
                    pfrom->PushInventory(CInv(MSG_SYSTEMNODE_ANNOUNCE, hash));
                    nInvCount++;
                    if (!mapSeenSystemnodeBroadcast.count(hash)) {
                        mapSeenSystemnodeBroadcast.insert(make_pair(hash, snb));
                    }
                    if (vin == sn.vin) {
                        LogPrint(BCLog::SYSTEMNODE, "sndseg - Sent 1 Systemnode entries to %s\n", pfrom->addr.ToString());
                        return;
                    }
                }
            }
        }

        if (vin == CTxIn()) {
            const CNetMsgMaker msgMaker(PROTOCOL_VERSION);
            connman->PushMessage(pfrom, msgMaker.Make(NetMsgType::SNSYNCSTATUS, SYSTEMNODE_SYNC_LIST, nInvCount));
            LogPrint(BCLog::SYSTEMNODE, "sndseg - Sent %d Systemnode entries to %s\n", nInvCount, pfrom->addr.ToString());
        }
    }
}

void CSystemnodeMan::AskForSN(CNode* pnode, CTxIn& vin, CConnman& connman)
{
    std::map<COutPoint, int64_t>::iterator i = mWeAskedForSystemnodeListEntry.find(vin.prevout);
    if (i != mWeAskedForSystemnodeListEntry.end()) {
        int64_t t = (*i).second;
        if (GetTime() < t)
            return; // we've asked recently
    }

    // ask for the snb info once from the node that sent snp
    LogPrint(BCLog::SYSTEMNODE, "CSystemnodeMan::AskForSN - Asking node for missing entry, vin: %s\n", vin.ToString());
    const CNetMsgMaker msgMaker(PROTOCOL_VERSION);
    connman.PushMessage(pnode, msgMaker.Make(NetMsgType::SNDSEG, vin));
    int64_t askAgain = GetTime() + SYSTEMNODE_MIN_SNP_SECONDS;
    mWeAskedForSystemnodeListEntry[vin.prevout] = askAgain;
}

bool CSystemnodeMan::CheckSnbAndUpdateSystemnodeList(CSystemnodeBroadcast snb, int& nDos, CConnman& connman)
{
    nDos = 0;
    LogPrint(BCLog::SYSTEMNODE, "CSystemnodeMan::CheckSnbAndUpdateSystemnodeList - Systemnode broadcast, vin: %s\n", snb.vin.ToString());

    uint256 snbHash = snb.GetHash();
    if (mapSeenSystemnodeBroadcast.count(snbHash)) {
        systemnodeSync.AddedSystemnodeList(snbHash);
        return true;
    }
    mapSeenSystemnodeBroadcast.insert(make_pair(snbHash, snb));

    LogPrint(BCLog::SYSTEMNODE, "CSystemnodeMan::CheckSnbAndUpdateSystemnodeList - Systemnode broadcast, vin: %s new\n", snb.vin.ToString());

    if (!snb.IsValidNetAddr()) {
        LogPrint(BCLog::SYSTEMNODE, "CMasternodeBroadcast::CheckSnbAndUpdateMasternodeList -- Invalid addr, rejected: masternode=%s  sigTime=%lld  addr=%s\n",
            snb.vin.prevout.ToStringShort(), snb.sigTime, snb.addr.ToString());
        return false;
    }

    if (mnodeman.Find(snb.addr)) {
        LogPrint(BCLog::SYSTEMNODE, "CSystemnodeMan::CheckSnbAndUpdateSystemnodeList - There is already a masternode with the same ip: %s\n", snb.addr.ToString());
        return false;
    }

    if (!snb.CheckAndUpdate(nDos, connman)) {
        LogPrint(BCLog::SYSTEMNODE, "CSystemnodeMan::CheckSnbAndUpdateSystemnodeList - Systemnode broadcast, vin: %s CheckAndUpdate failed\n", snb.vin.ToString());
        return false;
    }

    // make sure the vout that was signed is related to the transaction that spawned the Masternode
    //  - this is expensive, so it's only done once per Masternode
    if (!legacySigner.IsVinAssociatedWithPubkey(snb.vin, snb.pubkey, 1, Params().GetConsensus())) {
        LogPrint(BCLog::SYSTEMNODE, "CSystemnodeMan::CheckSnbAndUpdateSystemnodeList - Got mismatched pubkey and vin\n");
        return false;
    }

    // make sure it's still unspent
    //  - this is checked later by .check() in many places and by ThreadCheckDarkSendPool()
    if (snb.CheckInputsAndAdd(nDos, connman)) {
        systemnodeSync.AddedSystemnodeList(snbHash);
    } else {
        LogPrint(BCLog::SYSTEMNODE, "CSystemnodeMan::CheckSnbAndUpdateSystemnodeList - Rejected Systemnode entry %s\n", snb.addr.ToString());
        return false;
    }

    return true;
}

CSystemnode* CSystemnodeMan::Find(const CTxIn& vin)
{
    LOCK(cs);

    for (auto& sn : vSystemnodes) {
        if (sn.vin.prevout == vin.prevout)
            return &sn;
    }
    return nullptr;
}

CSystemnode* CSystemnodeMan::Find(const CPubKey& pubKeySystemnode)
{
    LOCK(cs);

    for (auto& sn : vSystemnodes) {
        if (sn.pubkey2 == pubKeySystemnode)
            return &sn;
    }
    return nullptr;
}

CSystemnode* CSystemnodeMan::Find(const CService& addr)
{
    LOCK(cs);

    for (auto& sn : vSystemnodes) {
        if (sn.addr == addr)
            return &sn;
    }
    return nullptr;
}

//
// Deterministically select the oldest/best systemnode to pay on the network
//
CSystemnode* CSystemnodeMan::GetNextSystemnodeInQueueForPayment(int nBlockHeight, bool fFilterSigTime, int& nCount)
{
    LOCK(cs);

    CSystemnode* pBestSystemnode = nullptr;
    std::vector<pair<int64_t, CTxIn>> vecSystemnodeLastPaid;

    /*
        Make a vector with all of the last paid times
    */

    int nSnCount = CountEnabled();
    for (auto& sn : vSystemnodes) {
        sn.Check();
        if (!sn.IsEnabled())
            continue;

        //it's in the list (up to 8 entries ahead of current block to allow propagation) -- so let's skip it
        if (systemnodePayments.IsScheduled(sn, nBlockHeight))
            continue;

        // For security reasons and for network stability there is a delay to get the first reward.
        // The time is calculated as a product of 60 block and node count.
        if (fFilterSigTime && sn.sigTime + (nSnCount * 1 * 60) > GetAdjustedTime())
            continue;

        //make sure it has as many confirmations as there are systemnodes
        if (sn.GetSystemnodeInputAge() < nSnCount)
            continue;

        vecSystemnodeLastPaid.push_back(make_pair(sn.SecondsSincePayment(), sn.vin));
    }

    nCount = (int)vecSystemnodeLastPaid.size();

    //when the network is in the process of upgrading, don't penalize nodes that recently restarted
    if (fFilterSigTime && nCount < nSnCount / 3)
        return GetNextSystemnodeInQueueForPayment(nBlockHeight, false, nCount);

    // Sort them high to low
    sort(vecSystemnodeLastPaid.rbegin(), vecSystemnodeLastPaid.rend(), CompareLastPaid());

    // Look at 1/10 of the oldest nodes (by last payment), calculate their scores and pay the best one
    //  -- This doesn't look at who is being paid in the +8-10 blocks, allowing for double payments very rarely
    //  -- 1/100 payments should be a double payment on mainnet - (1/(3000/10))*2
    //  -- (chance per block * chances before IsScheduled will fire)
    int nTenthNetwork = CountEnabled() / 10;
    int nCountTenth = 0;
    arith_uint256 nHigh = 0;
    for (auto& s : vecSystemnodeLastPaid) {
        CSystemnode* pmn = Find(s.second);
        if (!pmn)
            break;

        arith_uint256 n = pmn->CalculateScore(nBlockHeight - 100);
        if (n > nHigh) {
            nHigh = n;
            pBestSystemnode = pmn;
        }
        nCountTenth++;
        if (nCountTenth >= nTenthNetwork)
            break;
    }
    return pBestSystemnode;
}

bool CSystemnodeMan::Add(CSystemnode& sn)
{
    LOCK(cs);

    if (!sn.IsEnabled())
        return false;

    CSystemnode* psn = Find(sn.vin);
    if (!psn) {
        LogPrint(BCLog::SYSTEMNODE, "CSystemnodeMan: Adding new Systemnode %s - %i now\n", sn.addr.ToString(), size() + 1);
        vSystemnodes.push_back(sn);
        return true;
    }

    return false;
}

void CSystemnodeMan::UpdateSystemnodeList(CSystemnodeBroadcast snb, CConnman& connman)
{
    auto snbHash = snb.GetHash();
    mapSeenSystemnodePing.insert(make_pair(snb.lastPing.GetHash(), snb.lastPing));
    mapSeenSystemnodeBroadcast.insert(make_pair(snbHash, snb));
    systemnodeSync.AddedSystemnodeList(snbHash);

    LogPrint(BCLog::SYSTEMNODE, "CSystemnodeMan::UpdateSystemnodeList() - addr: %s\n    vin: %s\n", snb.addr.ToString(), snb.vin.ToString());

    CSystemnode* psn = Find(snb.vin);
    if (!psn) {
        CSystemnode sn(snb);
        Add(sn);
    } else {
        psn->UpdateFromNewBroadcast(snb, connman);
    }
}

void CSystemnodeMan::Remove(CTxIn vin)
{
    LOCK(cs);

    vector<CSystemnode>::iterator it = vSystemnodes.begin();
    while (it != vSystemnodes.end()) {
        if ((*it).vin == vin) {
            LogPrint(BCLog::SYSTEMNODE, "CSystemnodeMan: Removing Systemnode %s - %i now\n", (*it).addr.ToString(), size() - 1);
            vSystemnodes.erase(it);
            break;
        }
        ++it;
    }
}

int CSystemnodeMan::CountEnabled(int protocolVersion)
{
    int i = 0;
    protocolVersion = protocolVersion == -1 ? systemnodePayments.GetMinSystemnodePaymentsProto() : protocolVersion;

    for (auto& sn : vSystemnodes) {
        sn.Check();
        if (sn.protocolVersion < protocolVersion || !sn.IsEnabled())
            continue;
        i++;
    }

    return i;
}

void CSystemnodeMan::DsegUpdate(CNode* pnode, CConnman& connman)
{
    LOCK(cs);

    if (!(pnode->addr.IsRFC1918() || pnode->addr.IsLocal())) {
        std::map<CNetAddr, int64_t>::iterator it = mWeAskedForSystemnodeList.find(pnode->addr);
        if (it != mWeAskedForSystemnodeList.end()) {
            if (GetTime() < (*it).second) {
                LogPrint(BCLog::SYSTEMNODE, "sndseg - we already asked %s for the list; skipping...\n", pnode->addr.ToString());
                return;
            }
        }
    }

    const CNetMsgMaker msgMaker(PROTOCOL_VERSION);
    connman.PushMessage(pnode, msgMaker.Make(NetMsgType::SNDSEG, CTxIn()));
    int64_t askAgain = GetTime() + SYSTEMNODES_DSEG_SECONDS;
    mWeAskedForSystemnodeList[pnode->addr] = askAgain;
}

std::string CSystemnodeMan::ToString() const
{
    std::ostringstream info;

    info << "Systemnodes: " << (int)vSystemnodes.size() << ", peers who asked us for Systemnode list: " << (int)mAskedUsForSystemnodeList.size() << ", peers we asked for Systemnode list: " << (int)mWeAskedForSystemnodeList.size() << ", entries in Systemnode list we asked for: " << (int)mWeAskedForSystemnodeListEntry.size();

    return info.str();
}

void CSystemnodeMan::Clear()
{
    LOCK(cs);
    vSystemnodes.clear();
    mAskedUsForSystemnodeList.clear();
    mWeAskedForSystemnodeList.clear();
    mWeAskedForSystemnodeListEntry.clear();
    mapSeenSystemnodeBroadcast.clear();
    mapSeenSystemnodePing.clear();
}

void CSystemnodeMan::Check()
{
    LOCK(cs);

    for (auto& sn : vSystemnodes) {
        sn.Check();
    }
}

CSystemnode* CSystemnodeMan::GetCurrentSystemNode(int mod, int64_t nBlockHeight, int minProtocol)
{
    int64_t score = 0;
    CSystemnode* winner = nullptr;

    // scan for winner
    for (auto& sn : vSystemnodes) {
        sn.Check();
        if (sn.protocolVersion < minProtocol || !sn.IsEnabled())
            continue;

        // calculate the score for each Systemnode
        arith_uint256 n = sn.CalculateScore(nBlockHeight);
        int64_t n2 = n.GetCompact(false);

        // determine the winner
        if (n2 > score) {
            score = n2;
            winner = &sn;
        }
    }

    return winner;
}

int CSystemnodeMan::GetSystemnodeRank(const CTxIn& vin, int64_t nBlockHeight, int minProtocol, bool fOnlyActive)
{
    std::vector<pair<int64_t, CTxIn>> vecSystemnodeScores;

    //make sure we know about this block
    uint256 hash = uint256();
    if (!GetBlockHash(hash, nBlockHeight))
        return -1;

    // scan for winner
    for (auto& sn : vSystemnodes) {
        if (sn.protocolVersion < minProtocol)
            continue;
        if (fOnlyActive) {
            sn.Check();
            if (!sn.IsEnabled())
                continue;
        }
        arith_uint256 n = sn.CalculateScore(nBlockHeight);
        int64_t n2 = n.GetCompact(false);

        vecSystemnodeScores.push_back(make_pair(n2, sn.vin));
    }

    sort(vecSystemnodeScores.rbegin(), vecSystemnodeScores.rend(), CompareScoreTxIn());

    int rank = 0;
    for (auto& s : vecSystemnodeScores) {
        rank++;
        if (s.second.prevout == vin.prevout) {
            return rank;
        }
    }

    return -1;
}

void CSystemnodeMan::CheckAndRemove(bool forceExpiredRemoval)
{
    Check();

    LOCK(cs);

    //remove inactive and outdated
    vector<CSystemnode>::iterator it = vSystemnodes.begin();
    while (it != vSystemnodes.end()) {
        if ((*it).activeState == CSystemnode::SYSTEMNODE_REMOVE || (*it).activeState == CSystemnode::SYSTEMNODE_VIN_SPENT || (forceExpiredRemoval && (*it).activeState == CSystemnode::SYSTEMNODE_EXPIRED)) {

            LogPrint(BCLog::SYSTEMNODE, "CSystemnodeMan: Removing inactive Systemnode %s - %i now\n", (*it).addr.ToString(), size() - 1);

            //erase all of the broadcasts we've seen from this vin
            // -- if we missed a few pings and the node was removed, this will allow is to get it back without them
            //    sending a brand new snb
            map<uint256, CSystemnodeBroadcast>::iterator it3 = mapSeenSystemnodeBroadcast.begin();
            while (it3 != mapSeenSystemnodeBroadcast.end()) {
                if ((*it3).second.vin == (*it).vin) {
                    systemnodeSync.mapSeenSyncSNB.erase((*it3).first);
                    mapSeenSystemnodeBroadcast.erase(it3++);
                } else {
                    ++it3;
                }
            }

            // allow us to ask for this systemnode again if we see another ping
            map<COutPoint, int64_t>::iterator it2 = mWeAskedForSystemnodeListEntry.begin();
            while (it2 != mWeAskedForSystemnodeListEntry.end()) {
                if ((*it2).first == (*it).vin.prevout) {
                    mWeAskedForSystemnodeListEntry.erase(it2++);
                } else {
                    ++it2;
                }
            }

            it = vSystemnodes.erase(it);
        } else {
            ++it;
        }
    }

    // check who's asked for the Systemnode list
    map<CNetAddr, int64_t>::iterator it1 = mAskedUsForSystemnodeList.begin();
    while (it1 != mAskedUsForSystemnodeList.end()) {
        if ((*it1).second < GetTime()) {
            mAskedUsForSystemnodeList.erase(it1++);
        } else {
            ++it1;
        }
    }

    // check who we asked for the Systemnode list
    it1 = mWeAskedForSystemnodeList.begin();
    while (it1 != mWeAskedForSystemnodeList.end()) {
        if ((*it1).second < GetTime()) {
            mWeAskedForSystemnodeList.erase(it1++);
        } else {
            ++it1;
        }
    }

    // check which Systemnodes we've asked for
    map<COutPoint, int64_t>::iterator it2 = mWeAskedForSystemnodeListEntry.begin();
    while (it2 != mWeAskedForSystemnodeListEntry.end()) {
        if ((*it2).second < GetTime()) {
            mWeAskedForSystemnodeListEntry.erase(it2++);
        } else {
            ++it2;
        }
    }

    // remove expired mapSeenSystemnodeBroadcast
    auto it3 = mapSeenSystemnodeBroadcast.begin();
    while (it3 != mapSeenSystemnodeBroadcast.end()) {
        if ((*it3).second.lastPing.sigTime < GetTime() - SYSTEMNODE_REMOVAL_SECONDS * 2) {
            LogPrint(BCLog::SYSTEMNODE, "CSystemnodeMan::CheckAndRemove - Removing expired Systemnode broadcast %s\n", (*it3).second.GetHash().ToString());
            systemnodeSync.mapSeenSyncSNB.erase((*it3).second.GetHash());
            mapSeenSystemnodeBroadcast.erase(it3++);
        } else {
            ++it3;
        }
    }

    // remove expired mapSeenSystemnodePing
    map<uint256, CSystemnodePing>::iterator it4 = mapSeenSystemnodePing.begin();
    while (it4 != mapSeenSystemnodePing.end()) {
        if ((*it4).second.sigTime < GetTime() - (SYSTEMNODE_REMOVAL_SECONDS * 2)) {
            mapSeenSystemnodePing.erase(it4++);
        } else {
            ++it4;
        }
    }
}

CSystemnodeMan::CSystemnodeMan()
{
}

int CSystemnodeMan::CountSystemnodes(bool fEnabled)
{
    LOCK(cs);

    int nCount = 0;
    for (const auto& snpair : vSystemnodes) {
        if (fEnabled) {
            if (!snpair.IsEnabled())
                continue;
        }
        nCount++;
    }

    return nCount;
}
