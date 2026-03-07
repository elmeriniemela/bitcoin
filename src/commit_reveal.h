// Copyright (c) 2025-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_COMMIT_REVEAL_H
#define BITCOIN_COMMIT_REVEAL_H

#include <hash.h>
#include <pubkey.h>
#include <primitives/transaction.h>
#include <uint256.h>

#include <optional>
#include <vector>

class CCoinsViewCache;

static constexpr size_t COMMIT_AID_SIZE = 16;    // 16 bytes (128 bits)
static constexpr size_t COMMIT_SDP_SIZE = 16;    // 16 bytes (128 bits)
static constexpr size_t COMMIT_CTXID_SIZE = 16;  // 16 bytes (128 bits)
static constexpr size_t COMMIT_FULL_SIZE = COMMIT_AID_SIZE + COMMIT_SDP_SIZE + COMMIT_CTXID_SIZE;

/** Tagged hash for quantum commitment Address ID (AID) */
uint256 CommitmentHashAID(const CPubKey& pubkey);

/** Tagged hash for quantum commitment Sequence Dependent Proof (SDP) */
uint256 CommitmentHashSDP(const CPubKey& pubkey, const uint256& txid);

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
struct Commitment {
    std::vector<unsigned char> aid;    // Address ID: h(pubkey)
    std::vector<unsigned char> sdp;    // Sequence Dependent Proof: h(pubkey, txid)
    std::vector<unsigned char> ctxid;  // Commitment TXID (truncated)

    Commitment() = default;

    Commitment(std::vector<unsigned char> aid_,
                      std::vector<unsigned char> sdp_,
                      std::vector<unsigned char> ctxid_)
        : aid(std::move(aid_)), sdp(std::move(sdp_)), ctxid(std::move(ctxid_)) {}

    bool IsValid() const {
        return aid.size() == COMMIT_AID_SIZE &&
               sdp.size() == COMMIT_SDP_SIZE &&
               ctxid.size() == COMMIT_CTXID_SIZE;
    }

    /** Serialize the commitment for storage or transmission */
    std::vector<unsigned char> Serialize() const;

    /** Deserialize from a byte vector */
    static std::optional<Commitment> Deserialize(const std::vector<unsigned char>& data);
};

/**
 * Create a quantum commitment for a transaction
 *
 * @param tx The transaction to commit to
 * @param pubkey The public key that will be used to sign the first input
 * @return The quantum commitment triple (AID, SDP, CTXID)
 */
Commitment CreateCommitment(const CTransaction& tx, const CPubKey& pubkey);

/**
 * Verify that a transaction matches a quantum commitment
 *
 * @param tx The transaction to verify
 * @param commitment The commitment to check against
 * @param pubkey The public key revealed in the transaction
 * @return true if the transaction matches the commitment
 */
bool VerifyCommitment(const CTransaction& tx,
                             const Commitment& commitment,
                             const CPubKey& pubkey);

/**
 * Extract quantum commitment from an OP_RETURN output script
 *
 * @param scriptPubKey The output script to parse
 * @return The commitment if valid, nullopt otherwise
 */
std::optional<Commitment> ExtractCommitmentFromScript(const CScript& scriptPubKey);

/**
 * Create an OP_RETURN script containing a quantum commitment
 *
 * @param commitment The commitment to embed
 * @return The OP_RETURN script
 */
CScript CreateCommitmentScript(const Commitment& commitment);

CTransaction CreateCommitmentTransaction(const CTransaction& tx);

#endif  // !BITCOIN_COMMIT_REVEAL_H
