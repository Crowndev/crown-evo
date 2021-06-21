#include <key_io.h>
#include <masternode/masternode.h>
#include <masternode/masternodeman.h>
#include <systemnode/systemnode.h>
#include <systemnode/systemnodeman.h>
#include <util/system.h>

bool host_filter{false};

std::string host_address()
{
    return "95.216.167.217";
}

void printRawMessage(char *msgin, int sizeIn)
{
    char rawDataIn[1536];
    memset(rawDataIn, 0, sizeof(rawDataIn));
    for (int i=0; i < sizeIn; i++) {
        sprintf(&rawDataIn[2*i], "%02hhx", msgin[i]);
    }
    LogPrintf("raw: %s\n", rawDataIn);
}

void masternodeDiag(CMasternodeBroadcast *mnb, CMasternodePing *mnp)
{
    if (mnb) {
        uint256 mnb_hash = mnb->GetHash();
        CTxIn mnb_vin = mnb->vin;
        std::string mnb_addr = mnb->addr.ToString();
        CPubKey mnb_pubkey = mnb->pubkey;
        CPubKey mnb_pubkey2 = mnb->pubkey2;
//      std::vector<unsigned char> sig;
//      memcpy(sig,mnb->sig,sizeof(mnb->sig));
        int64_t mnb_sigtime = mnb->sigTime;
        int mnb_protocolVersion = mnb->protocolVersion;
        uint256 mnb_lastping = mnb->lastPing.blockHash;
        int64_t mnb_lastdsq = mnb->nLastDsq;

        if (!host_filter || (host_filter && (mnb_addr.find(host_address()) != std::string::npos))) {
             LogPrintf("mnb (%s) - vin %s addr %s pubkey %s pubkey2 %s sig NA sigTime %d protocol %d lastPing %s lastDsq %d\n",
                  mnb_hash.ToString(), mnb_vin.ToString(), mnb_addr, EncodeDestination(PKHash(mnb_pubkey)),
                  EncodeDestination(PKHash(mnb_pubkey2)), mnb_sigtime, mnb_protocolVersion, mnb_lastping.ToString(), mnb_lastdsq);
             printRawMessage(reinterpret_cast<char*>(&mnb), 467);
        }

    } else if (mnp) {
        uint256 mnp_hash = mnp->GetHash();
        CTxIn mnp_vin = mnp->vin;
        uint256 mnp_blockHash = mnp->blockHash;
        int mnp_sigtime = mnp->sigTime;
//      std::vector<unsigned char> vchSig;
//      memcpy(vchSig,mnp->vchSig,sizeof(mnp->vchSig));
        int mnp_nVersion = mnp->nVersion;
//      uint256 mnp_prevblockHash = mnp->vPrevBlockHash;
//      std::vector<unsigned char> vchSigPrevBlocks;
//      memcpy(vchSigPrevBlocks,mnp->vchSigPrevBlocks,sizeof(mnp->vchSigPrevBlocks));

        if (!host_filter) {
            LogPrintf("mnp (%s) - vin %s blockhash %s sigtime %d version %d\n",
                  mnp_hash.ToString(), mnp_vin.ToString(), mnp_blockHash.ToString(), mnp_sigtime, mnp_nVersion);
            printRawMessage(reinterpret_cast<char*>(&mnp), 535);
        }
    }
    return;
}

void systemnodeDiag(CSystemnodeBroadcast *snb, CSystemnodePing *snp)
{
    if (snb) {
        uint256 snb_hash = snb->GetHash();
        CTxIn snb_vin = snb->vin;
        std::string snb_addr = snb->addr.ToString();
        CPubKey snb_pubkey = snb->pubkey;
        CPubKey snb_pubkey2 = snb->pubkey2;
//      std::vector<unsigned char> sig;
//      memcpy(sig,snb->sig,sizeof(snb->sig));
        int64_t snb_sigtime = snb->sigTime;
        int snb_protocolVersion = snb->protocolVersion;
        uint256 snb_lastping = snb->lastPing.blockHash;

        if (!host_filter || (host_filter && (snb_addr.find(host_address()) != std::string::npos))) {
            LogPrintf("snb (%s) - vin %s addr %s pubkey %s pubkey2 %s sig NA sigTime %d protocol %d lastPing %s\n",
                  snb_hash.ToString(), snb_vin.ToString(), snb_addr, EncodeDestination(PKHash(snb_pubkey)),
                  EncodeDestination(PKHash(snb_pubkey2)), snb_sigtime, snb_protocolVersion, snb_lastping.ToString());
        }
    } else if (snp) {
        uint256 snp_hash = snp->GetHash();
        CTxIn snp_vin = snp->vin;
        uint256 snp_blockHash = snp->blockHash;
        int snp_sigtime = snp->sigTime;
//      std::vector<unsigned char> vchSig;
//      memcpy(vchSig,snp->vchSig,sizeof(snp->vchSig));
//      int snp_nVersion = snp->nVersion;
//      uint256 snp_prevblockHash = snp->vPrevBlockHash;
//      std::vector<unsigned char> vchSigPrevBlocks;
//      memcpy(vchSigPrevBlocks,snp->vchSigPrevBlocks,sizeof(snp->vchSigPrevBlocks));

        if (!host_filter) {
            LogPrintf("snp (%s) - vin %s blockhash %s sigtime %d\n",
                  snp_hash.ToString(), snp_vin.ToString(), snp_blockHash.ToString(), snp_sigtime/*,snp_version*/);
            printRawMessage(reinterpret_cast<char*>(&snp), 147);
        }
    }
    return;
}
