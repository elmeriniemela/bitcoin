// Copyright (c) 2025 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QUANTUM_COMMITDB_H
#define BITCOIN_QUANTUM_COMMITDB_H

#include <dbwrapper.h>
#include <quantum_commit.h>
#include <sync.h>
#include <uint256.h>

#include <map>
#include <optional>
#include <vector>

/**
 * A single commitment entry: (SDP, CTXID) pair
 */
struct CommitmentEntry {
    std::vector<unsigned char> sdp;
    std::vector<unsigned char> ctxid;
    int nHeight;  // Block height where this commitment was seen

    CommitmentEntry() : nHeight(0) {}
    CommitmentEntry(std::vector<unsigned char> sdp_,
                   std::vector<unsigned char> ctxid_,
                   int height)
        : sdp(std::move(sdp_)), ctxid(std::move(ctxid_)), nHeight(height) {}

    SERIALIZE_METHODS(CommitmentEntry, obj)
    {
        READWRITE(obj.sdp, obj.ctxid, obj.nHeight);
    }
};

/**
 * All commitments for a single AID
 * Stores multiple (SDP, CTXID) pairs in case of conflicts
 */
struct AIDCommitments {
    std::vector<CommitmentEntry> entries;
    bool is_finalized{false};  // Set to true once pubkey is revealed
    std::vector<unsigned char> finalized_ctxid;  // The winning CTXID after finalization (empty if not finalized)

    AIDCommitments() = default;

    void AddEntry(const CommitmentEntry& entry) {
        entries.push_back(entry);
    }

    SERIALIZE_METHODS(AIDCommitments, obj)
    {
        READWRITE(obj.entries, obj.is_finalized, obj.finalized_ctxid);
    }
};

/**
 * Database for storing quantum commitments
 *
 * Key: AID (16 bytes)
 * Value: AIDCommitments (vector of CommitmentEntry)
 *
 * This database stores all commitments indexed by Address ID (AID).
 * When a pubkey is revealed, the database can determine which commitment
 * is valid and finalize the entry.
 */
class CQuantumCommitmentDB
{
private:
    std::unique_ptr<CDBWrapper> m_db;
    mutable RecursiveMutex cs_commitments;

    // In-memory cache for quick lookups
    mutable std::map<std::vector<unsigned char>, AIDCommitments> m_cache GUARDED_BY(cs_commitments);
    mutable bool m_cache_dirty GUARDED_BY(cs_commitments){false};

public:
    explicit CQuantumCommitmentDB(size_t nCacheSize, bool fMemory = false, bool fWipe = false);

    CQuantumCommitmentDB(const CQuantumCommitmentDB&) = delete;
    CQuantumCommitmentDB& operator=(const CQuantumCommitmentDB&) = delete;

    /**
     * Add a commitment to the database
     *
     * @param commitment The quantum commitment to store
     * @param nHeight Block height where commitment was seen
     * @return true if successfully added
     */
    bool AddCommitment(const QuantumCommitment& commitment, int nHeight);

    /**
     * Get all commitments for a given AID
     *
     * @param aid The Address ID to look up
     * @return The commitments if found, nullopt otherwise
     */
    std::optional<AIDCommitments> GetCommitments(const std::vector<unsigned char>& aid) const LOCKS_EXCLUDED(cs_commitments);

    /**
     * Finalize an AID entry after pubkey revelation
     *
     * This filters all commitments to find the first valid one and marks the AID as finalized
     *
     * @param aid The Address ID to finalize
     * @param pubkey The revealed public key
     * @param tx The transaction revealing the pubkey
     * @return The finalized CTXID if successful, nullopt if no valid commitment found
     */
    std::optional<std::vector<unsigned char>> FinalizeAID(const std::vector<unsigned char>& aid,
                                                          const CPubKey& pubkey,
                                                          const CTransaction& tx);

    /**
     * Check if a transaction matches the finalized commitment for its pubkey
     *
     * @param tx The transaction to verify
     * @param pubkey The public key used in the transaction
     * @return true if the transaction matches a valid commitment
     */
    bool VerifyTransaction(const CTransaction& tx, const CPubKey& pubkey) const LOCKS_EXCLUDED(cs_commitments);

    /**
     * Remove an AID entry from the database
     * Called after transaction is confirmed to free up space
     *
     * @param aid The Address ID to remove
     * @return true if successfully removed
     */
    bool RemoveAID(const std::vector<unsigned char>& aid);

    /**
     * Flush the cache to disk
     */
    bool Flush();

    /**
     * Get database size estimate
     */
    size_t DynamicMemoryUsage() const;

    /**
     * Mark that a Proof of Quantum Computer has been observed
     *
     * @param nHeight The block height where PoQC was confirmed
     * @return true if successfully recorded
     */
    bool SetPoQCActivated(int nHeight);

    /**
     * Check if PoQC has been activated
     *
     * @param nHeight Optional output parameter for the activation height
     * @return true if PoQC has been seen and commitments are required
     */
    bool IsPoQCActivated(int* nHeight = nullptr) const LOCKS_EXCLUDED(cs_commitments);

    /**
     * Get the activation height for PoQC
     *
     * @return The height where PoQC was activated, or -1 if not activated
     */
    int GetPoQCActivationHeight() const LOCKS_EXCLUDED(cs_commitments);

private:
    int m_poqc_activation_height GUARDED_BY(cs_commitments){-1};
};

/** Global quantum commitment database instance */
extern std::unique_ptr<CQuantumCommitmentDB> g_quantum_commitdb;

#endif // BITCOIN_QUANTUM_COMMITDB_H
