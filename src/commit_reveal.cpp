// Copyright (c) 2025 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <commit_reveal.h>
#include <coins.h>
#include <crypto/sha256.h>
#include <hash.h>
#include <pubkey.h>
#include <script/script.h>
#include <script/solver.h>
#include <uint256.h>

#include <algorithm>

const HashWriter HASHER_COMMIT_AID{TaggedHash("CommitAID")};
const HashWriter HASHER_COMMIT_SPD{TaggedHash("CommitSPD")};

uint256 CommitmentHashAID(const CPubKey& pubkey)
{
    return (HashWriter{HASHER_COMMIT_AID} << pubkey).GetSHA256();
}

uint256 CommitmentHashSDP(const CPubKey& pubkey, const uint256& txid)
{
    return (HashWriter{HASHER_COMMIT_SPD} << pubkey << txid).GetSHA256();
}

std::vector<unsigned char> Commitment::Serialize() const
{
    std::vector<unsigned char> result;
    result.reserve(COMMIT_FULL_SIZE);
    result.insert(result.end(), aid.begin(), aid.end());
    result.insert(result.end(), sdp.begin(), sdp.end());
    result.insert(result.end(), ctxid.begin(), ctxid.end());
    return result;
}

std::optional<Commitment> Commitment::Deserialize(const std::vector<unsigned char>& data)
{
    if (data.size() != COMMIT_FULL_SIZE) {
        return std::nullopt;
    }

    Commitment commitment;
    auto aid_end = data.begin() + COMMIT_AID_SIZE;
    auto spd_end = aid_end + COMMIT_SDP_SIZE;
    commitment.aid.assign(data.begin(), aid_end);
    commitment.sdp.assign(aid_end, spd_end);
    commitment.ctxid.assign(spd_end, data.end());

    return commitment;
}

Commitment CreateCommitment(const CTransaction& tx, const CPubKey& pubkey)
{
    // Compute the three components
    uint256 aid_hash = CommitmentHashAID(pubkey);
    const uint256& txid = tx.GetHash().ToUint256();
    uint256 sdp_hash = CommitmentHashSDP(pubkey, txid);

    // Truncate to 16 bytes each
    Commitment commitment;
    commitment.aid = TruncateHash(aid_hash, COMMIT_AID_SIZE);
    commitment.sdp = TruncateHash(sdp_hash, COMMIT_SDP_SIZE);
    commitment.ctxid = TruncateHash(txid, COMMIT_CTXID_SIZE);

    return commitment;
}

bool VerifyCommitment(const CTransaction& tx,
                             const Commitment& commitment,
                             const CPubKey& pubkey)
{
    if (!commitment.IsValid() || !pubkey.IsFullyValid()) {
        return false;
    }

    // Recompute the commitment with the revealed pubkey
    Commitment expected = CreateCommitment(tx, pubkey);

    // Check all three components match
    return commitment.aid == expected.aid &&
           commitment.sdp == expected.sdp &&
           commitment.ctxid == expected.ctxid;
}

std::optional<Commitment> ExtractCommitmentFromScript(const CScript& scriptPubKey)
{
    // Check if this is an OP_RETURN script
    if (scriptPubKey.empty() || scriptPubKey[0] != OP_RETURN) {
        return std::nullopt;
    }

    // Parse the OP_RETURN data
    // Format: OP_RETURN <48 bytes commitment data>
    CScript::const_iterator pc = scriptPubKey.begin() + 1; // Skip OP_RETURN
    std::vector<unsigned char> data;

    opcodetype opcode;
    if (!scriptPubKey.GetOp(pc, opcode, data)) {
        return std::nullopt;
    }

    // Check if this is a push of exactly 48 bytes
    if (data.size() != COMMIT_FULL_SIZE) {
        return std::nullopt;
    }

    return Commitment::Deserialize(data);
}

CScript CreateCommitmentScript(const Commitment& commitment)
{
    if (!commitment.IsValid()) {
        return CScript();
    }

    std::vector<unsigned char> data = commitment.Serialize();
    return CScript() << OP_RETURN << data;
}

CTransaction CreateCommitmentTransaction(const CTransaction& tx)
{

}