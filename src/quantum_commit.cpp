// Copyright (c) 2025 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <quantum_commit.h>

#include <coins.h>
#include <crypto/sha256.h>
#include <hash.h>
#include <pubkey.h>
#include <script/script.h>
#include <script/solver.h>
#include <uint256.h>

#include <algorithm>

// Tag for quantum commitment hashes
static const std::string QUANTUM_COMMIT_TAG = "Alas poor Koblitz curve, we knew it well";

uint256 QuantumCommitmentHashAID(const CPubKey& pubkey)
{
    // Tagged hash: SHA256(SHA256(tag) || SHA256(tag) || pubkey)
    return (TaggedHash(QUANTUM_COMMIT_TAG) << pubkey).GetSHA256();
}

uint256 QuantumCommitmentHashSDP(const CPubKey& pubkey, const uint256& txid)
{
    // Tagged hash: SHA256(SHA256(tag) || SHA256(tag) || pubkey || txid)
    return (TaggedHash(QUANTUM_COMMIT_TAG) << pubkey << txid).GetSHA256();
}

std::vector<unsigned char> QuantumCommitment::Serialize() const
{
    std::vector<unsigned char> result;
    result.reserve(QUANTUM_COMMITMENT_FULL_SIZE);
    result.insert(result.end(), aid.begin(), aid.end());
    result.insert(result.end(), sdp.begin(), sdp.end());
    result.insert(result.end(), ctxid.begin(), ctxid.end());
    return result;
}

std::optional<QuantumCommitment> QuantumCommitment::Deserialize(const std::vector<unsigned char>& data)
{
    if (data.size() != QUANTUM_COMMITMENT_FULL_SIZE) {
        return std::nullopt;
    }

    QuantumCommitment commitment;
    commitment.aid.assign(data.begin(), data.begin() + QUANTUM_COMMITMENT_AID_SIZE);
    commitment.sdp.assign(data.begin() + QUANTUM_COMMITMENT_AID_SIZE,
                         data.begin() + QUANTUM_COMMITMENT_AID_SIZE + QUANTUM_COMMITMENT_SDP_SIZE);
    commitment.ctxid.assign(data.begin() + QUANTUM_COMMITMENT_AID_SIZE + QUANTUM_COMMITMENT_SDP_SIZE,
                           data.end());

    return commitment;
}

QuantumCommitment CreateQuantumCommitment(const CTransaction& tx, const CPubKey& pubkey)
{
    // Compute the three components
    uint256 aid_hash = QuantumCommitmentHashAID(pubkey);
    const uint256& txid = tx.GetHash().ToUint256();
    uint256 sdp_hash = QuantumCommitmentHashSDP(pubkey, txid);

    // Truncate to 16 bytes each
    QuantumCommitment commitment;
    commitment.aid = TruncateHash(aid_hash, QUANTUM_COMMITMENT_AID_SIZE);
    commitment.sdp = TruncateHash(sdp_hash, QUANTUM_COMMITMENT_SDP_SIZE);
    commitment.ctxid = TruncateHash(txid, QUANTUM_COMMITMENT_CTXID_SIZE);

    return commitment;
}

bool VerifyQuantumCommitment(const CTransaction& tx,
                             const QuantumCommitment& commitment,
                             const CPubKey& pubkey)
{
    if (!commitment.IsValid() || !pubkey.IsFullyValid()) {
        return false;
    }

    // Recompute the commitment with the revealed pubkey
    QuantumCommitment expected = CreateQuantumCommitment(tx, pubkey);

    // Check all three components match
    return commitment.aid == expected.aid &&
           commitment.sdp == expected.sdp &&
           commitment.ctxid == expected.ctxid;
}

std::optional<QuantumCommitment> ExtractQuantumCommitmentFromScript(const CScript& scriptPubKey)
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
    if (data.size() != QUANTUM_COMMITMENT_FULL_SIZE) {
        return std::nullopt;
    }

    return QuantumCommitment::Deserialize(data);
}

CScript CreateQuantumCommitmentScript(const QuantumCommitment& commitment)
{
    if (!commitment.IsValid()) {
        return CScript();
    }

    std::vector<unsigned char> data = commitment.Serialize();
    return CScript() << OP_RETURN << data;
}

bool IsProofOfQuantumComputer(const CScript& scriptPubKey)
{
    // Check for the exact pattern: OP_SHA256 OP_CHECKSIG
    // This is a 2-byte script
    if (scriptPubKey.size() != 2) {
        return false;
    }

    return scriptPubKey[0] == OP_SHA256 && scriptPubKey[1] == OP_CHECKSIG;
}

bool TransactionProvidesPoQC(const CTransaction& tx, const CCoinsViewCache& inputs)
{
    // Check each input to see if it spends a PoQC output
    for (const auto& txin : tx.vin) {
        std::optional<Coin> coin = inputs.GetCoin(txin.prevout);
        if (!coin) {
            continue;
        }

        // Check if this output is a PoQC script
        if (IsProofOfQuantumComputer(coin->out.scriptPubKey)) {
            return true;
        }

        // For witness outputs, also check the witness program
        int witness_version;
        std::vector<unsigned char> witness_program;
        if (coin->out.scriptPubKey.IsWitnessProgram(witness_version, witness_program)) {
            // Check tapscript witness for PoQC pattern
            // In taproot, the PoQC could be in a script path
            if (txin.scriptWitness.stack.size() >= 2) {
                // Last item in tapscript witness is the script itself
                const auto& script_bytes = txin.scriptWitness.stack.back();
                CScript script(script_bytes.begin(), script_bytes.end());
                if (IsProofOfQuantumComputer(script)) {
                    return true;
                }
            }
        }
    }

    return false;
}
