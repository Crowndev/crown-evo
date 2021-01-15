// Copyright (c) 2014-2017 The Dash Core developers
// Copyright (c) 2014-2018 The Crown developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <crown/legacysigner.h>

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

using namespace std;
using namespace boost;

CLegacySigner legacySigner;

bool CLegacySigner::IsVinAssociatedWithPubkey(CTxIn& vin, CPubKey& pubkey, int nodeType, const Consensus::Params& consensusParams)
{
    uint256 hash;
    CTransactionRef txVin;
    g_txindex->BlockUntilSyncedToCurrentChain();
    CScript payee = GetScriptForDestination(PKHash(pubkey));

    //! get tx from disk
    if (!g_txindex->FindTx(vin.prevout.hash, hash, txVin)) {
        LogPrintf("%s - could not retrieve tx %s\n", __func__, vin.prevout.hash.ToString());
        return false;
    }

    //! get specific vout
    unsigned int voutn = 0;
    for (const auto& out : txVin->vout) {
        if (vin.prevout.n != voutn)
            continue;
        LogPrintf("     vout %d - found %s - expected %s\n", voutn, out.scriptPubKey.ToString(), payee.ToString());
        if (out.scriptPubKey == payee) {
            if (out.nValue * COIN == nodeType ? consensusParams.nSystemnodeCollateral : consensusParams.nMasternodeCollateral) {
                return true;
            }
        }
        ++voutn;
    }

    return false;
}

bool CLegacySigner::SetCollateralAddress(std::string strAddress)
{
    CTxDestination dest = DecodeDestination(strAddress);
    if (!IsValidDestination(dest))
        return false;
    collateralPubKey = GetScriptForDestination(dest);
    return true;
}

bool CLegacySigner::SetKey(std::string strSecret, CKey& key, CPubKey& pubkey)
{
    auto m_wallet = GetMainWallet();
    EnsureLegacyScriptPubKeyMan(*m_wallet, true);

    key = DecodeSecret(strSecret);
    if (!key.IsValid())
        return false;
    pubkey = key.GetPubKey();

    return true;
}

bool CLegacySigner::SignMessage(const std::string& strMessage, std::vector<unsigned char>& vchSigRet, const CKey& key)
{
    CHashWriter ss(SER_GETHASH, 0);
    ss << MESSAGE_MAGIC;
    ss << strMessage;

    return CHashSigner::SignHash(ss.GetHash(), key, vchSigRet);
}

bool CLegacySigner::VerifyMessage(const CPubKey& pubkey, const std::vector<unsigned char>& vchSig, const std::string& strMessage, std::string& strErrorRet)
{
    return VerifyMessage(pubkey.GetID(), vchSig, strMessage, strErrorRet);
}

bool CLegacySigner::VerifyMessage(const CKeyID& keyID, const std::vector<unsigned char>& vchSig, const std::string& strMessage, std::string& strErrorRet)
{
    CHashWriter ss(SER_GETHASH, 0);
    ss << MESSAGE_MAGIC;
    ss << strMessage;

    return CHashSigner::VerifyHash(ss.GetHash(), keyID, vchSig, strErrorRet);
}

bool CHashSigner::SignHash(const uint256& hash, const CKey& key, std::vector<unsigned char>& vchSigRet)
{
    return key.SignCompact(hash, vchSigRet);
}

bool CHashSigner::VerifyHash(const uint256& hash, const CPubKey& pubkey, const std::vector<unsigned char>& vchSig, std::string& strErrorRet)
{
    return VerifyHash(hash, pubkey.GetID(), vchSig, strErrorRet);
}

bool CHashSigner::VerifyHash(const uint256& hash, const CKeyID& keyID, const std::vector<unsigned char>& vchSig, std::string& strErrorRet)
{
    CPubKey pubkeyFromSig;
    if (!pubkeyFromSig.RecoverCompact(hash, vchSig)) {
        strErrorRet = "Error recovering public key.";
        return false;
    }

    return true;
}

void ThreadCheckLegacySigner()
{
    util::ThreadRename("crown-legacysigner");

    if (fReindex || fImporting)
        return;
    if (::ChainstateActive().IsInitialBlockDownload())
        return;
    if (ShutdownRequested())
        return;

    static unsigned int c1 = 0;
    static unsigned int c2 = 0;

    // try to sync from all available nodes, one step at a time
    masternodeSync.Process(*g_rpc_node->connman);
    systemnodeSync.Process(*g_rpc_node->connman);

    if (masternodeSync.IsBlockchainSynced()) {

        c1++;

        // check if we should activate or ping every few minutes,
        // start right after sync is considered to be done
        if (c1 % MASTERNODE_PING_SECONDS == 15)
            activeMasternode.ManageStatus(*g_rpc_node->connman);

        if (c1 % 60 == 0) {
            mnodeman.CheckAndRemove();
            mnodeman.ProcessMasternodeConnections(*g_rpc_node->connman);
            masternodePayments.CheckAndRemove();
            instantSend.CheckAndRemove();
        }
    }

    if (systemnodeSync.IsBlockchainSynced()) {

        c2++;

        // check if we should activate or ping every few minutes,
        // start right after sync is considered to be done
        if (c2 % SYSTEMNODE_PING_SECONDS == 15)
            activeSystemnode.ManageStatus(*g_rpc_node->connman);

        if (c2 % 60 == 0) {
            snodeman.CheckAndRemove();
            snodeman.ProcessSystemnodeConnections(*g_rpc_node->connman);
            systemnodePayments.CheckAndRemove();
            instantSend.CheckAndRemove();
        }
    }
}
