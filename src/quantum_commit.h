// Copyright (c) 2025 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QUANTUM_COMMIT_H
#define BITCOIN_QUANTUM_COMMIT_H

#include <hash.h>
#include <pubkey.h>
#include <primitives/transaction.h>
#include <uint256.h>

#include <optional>
#include <vector>

class CCoinsViewCache;

/**
 * Quantum-resistant commit/reveal scheme for Bitcoin.
 *
 * This implements a post-quantum protection mechanism that activates upon
 * detection of a Proof of Quantum Computer (PoQC). The scheme requires
 * transactions to commit to their spending in advance using a three-part
 * commitment scheme:
 *
 * 1. AID (Address ID): h(pubkey) - identifies which pubkey will sign
 * 2. SDP (Sequence Dependent Proof): h(pubkey, txid) - proves knowledge of pubkey
 * 3. CTXID (Commitment TXID): txid of the spending transaction
 *
 * where h() is a tagged SHA256 hash function.
 */

static constexpr size_t QUANTUM_COMMITMENT_AID_SIZE = 16;    // 16 bytes (128 bits)
static constexpr size_t QUANTUM_COMMITMENT_SDP_SIZE = 16;    // 16 bytes (128 bits)
static constexpr size_t QUANTUM_COMMITMENT_CTXID_SIZE = 16;  // 16 bytes (128 bits)
static constexpr size_t QUANTUM_COMMITMENT_FULL_SIZE =
    QUANTUM_COMMITMENT_AID_SIZE + QUANTUM_COMMITMENT_SDP_SIZE + QUANTUM_COMMITMENT_CTXID_SIZE;

/** Tagged hash for quantum commitment Address ID (AID) */
uint256 QuantumCommitmentHashAID(const CPubKey& pubkey);

/** Tagged hash for quantum commitment Sequence Dependent Proof (SDP) */
uint256 QuantumCommitmentHashSDP(const CPubKey& pubkey, const uint256& txid);

/**
 * Truncate a uint256 hash to the first n bytes for commitment
 * This is safe for quantum commitments as we're not defending against collision attacks
 */
inline std::vector<unsigned char> TruncateHash(const uint256& hash, size_t size)
{
    std::vector<unsigned char> result(hash.begin(), hash.begin() + size);
    return result;
}

/**
 * Represents a quantum commitment triple: (AID, SDP, CTXID)
 */
struct QuantumCommitment {
    std::vector<unsigned char> aid;    // Address ID: h(pubkey)
    std::vector<unsigned char> sdp;    // Sequence Dependent Proof: h(pubkey, txid)
    std::vector<unsigned char> ctxid;  // Commitment TXID (truncated)

    QuantumCommitment() = default;

    QuantumCommitment(std::vector<unsigned char> aid_,
                      std::vector<unsigned char> sdp_,
                      std::vector<unsigned char> ctxid_)
        : aid(std::move(aid_)), sdp(std::move(sdp_)), ctxid(std::move(ctxid_)) {}

    bool IsValid() const {
        return aid.size() == QUANTUM_COMMITMENT_AID_SIZE &&
               sdp.size() == QUANTUM_COMMITMENT_SDP_SIZE &&
               ctxid.size() == QUANTUM_COMMITMENT_CTXID_SIZE;
    }

    /** Serialize the commitment for storage or transmission */
    std::vector<unsigned char> Serialize() const;

    /** Deserialize from a byte vector */
    static std::optional<QuantumCommitment> Deserialize(const std::vector<unsigned char>& data);
};

/**
 * Create a quantum commitment for a transaction
 *
 * @param tx The transaction to commit to
 * @param pubkey The public key that will be used to sign the first input
 * @return The quantum commitment triple (AID, SDP, CTXID)
 */
QuantumCommitment CreateQuantumCommitment(const CTransaction& tx, const CPubKey& pubkey);

/**
 * Verify that a transaction matches a quantum commitment
 *
 * @param tx The transaction to verify
 * @param commitment The commitment to check against
 * @param pubkey The public key revealed in the transaction
 * @return true if the transaction matches the commitment
 */
bool VerifyQuantumCommitment(const CTransaction& tx,
                             const QuantumCommitment& commitment,
                             const CPubKey& pubkey);

/**
 * Extract quantum commitment from an OP_RETURN output script
 *
 * @param scriptPubKey The output script to parse
 * @return The commitment if valid, nullopt otherwise
 */
std::optional<QuantumCommitment> ExtractQuantumCommitmentFromScript(const CScript& scriptPubKey);

/**
 * Create an OP_RETURN script containing a quantum commitment
 *
 * @param commitment The commitment to embed
 * @return The OP_RETURN script
 */
CScript CreateQuantumCommitmentScript(const QuantumCommitment& commitment);

/**
 * Check if a script is a Proof of Quantum Computer (PoQC)
 *
 * A PoQC is a script that requires: OP_SHA256 OP_CHECKSIG
 * This script can only be satisfied if someone can create a valid signature
 * for an arbitrary EC point (the SHA256 hash of some preimage), proving
 * they have broken secp256k1.
 *
 * @param scriptPubKey The script to check
 * @return true if this is a PoQC script
 */
bool IsProofOfQuantumComputer(const CScript& scriptPubKey);

/**
 * Check if a transaction successfully spends a PoQC output
 *
 * @param tx The transaction to check
 * @param inputs The view of available coins (to check if any input is a PoQC)
 * @return true if this transaction proves existence of a quantum computer
 */
bool TransactionProvidesPoQC(const CTransaction& tx, const CCoinsViewCache& inputs);

#endif // BITCOIN_QUANTUM_COMMIT_H
