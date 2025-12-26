// Copyright (c) 2025 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <quantum_commitdb.h>

#include <common/args.h>
#include <logging.h>
#include <quantum_commit.h>
#include <uint256.h>

#include <algorithm>

// Database prefixes
static constexpr uint8_t DB_QUANTUM_COMMIT{'q'};
static constexpr uint8_t DB_POQC_ACTIVATED{'Q'};

std::unique_ptr<CQuantumCommitmentDB> g_quantum_commitdb;

CQuantumCommitmentDB::CQuantumCommitmentDB(size_t nCacheSize, bool fMemory, bool fWipe)
    : m_db(std::make_unique<CDBWrapper>(DBParams{
        .path = gArgs.GetDataDirNet() / "quantum_commits",
        .cache_bytes = nCacheSize,
        .memory_only = fMemory,
        .wipe_data = fWipe
    }))
{
    // Load PoQC activation status from database
    LOCK(cs_commitments);
    m_db->Read(DB_POQC_ACTIVATED, m_poqc_activation_height);
}

bool CQuantumCommitmentDB::AddCommitment(const QuantumCommitment& commitment, int nHeight)
{
    if (!commitment.IsValid()) {
        return false;
    }

    LOCK(cs_commitments);

    // Get existing commitments for this AID (or create new entry)
    AIDCommitments aid_commits;
    auto it = m_cache.find(commitment.aid);
    if (it != m_cache.end()) {
        aid_commits = it->second;
    } else {
        // Try to read from database
        if (m_db->Read(std::make_pair(DB_QUANTUM_COMMIT, commitment.aid), aid_commits)) {
            // Found in database
        }
        // else: new AID, aid_commits is empty
    }

    // If already finalized, don't add new commitments
    if (aid_commits.is_finalized) {
        LogInfo("QuantumCommitDB: Attempt to add commitment for finalized AID\n");
        return false;
    }

    // Check if this exact commitment already exists
    for (const auto& entry : aid_commits.entries) {
        if (entry.sdp == commitment.sdp && entry.ctxid == commitment.ctxid) {
            // Duplicate commitment, no-op
            return true;
        }
    }

    // Add the new commitment entry
    CommitmentEntry entry(commitment.sdp, commitment.ctxid, nHeight);
    aid_commits.AddEntry(entry);

    // Update cache
    m_cache[commitment.aid] = aid_commits;
    m_cache_dirty = true;

    LogInfo("QuantumCommitDB: Added commitment for AID (height=%d, total_entries=%d)\n",
             nHeight, aid_commits.entries.size());

    return true;
}

std::optional<AIDCommitments> CQuantumCommitmentDB::GetCommitments(const std::vector<unsigned char>& aid) const
{
    LOCK(cs_commitments);

    // Check cache first
    auto it = m_cache.find(aid);
    if (it != m_cache.end()) {
        return it->second;
    }

    // Read from database
    AIDCommitments aid_commits;
    if (m_db->Read(std::make_pair(DB_QUANTUM_COMMIT, aid), aid_commits)) {
        // Cache it
        m_cache[aid] = aid_commits;
        return aid_commits;
    }

    return std::nullopt;
}

std::optional<std::vector<unsigned char>> CQuantumCommitmentDB::FinalizeAID(
    const std::vector<unsigned char>& aid,
    const CPubKey& pubkey,
    const CTransaction& tx)
{
    if (!pubkey.IsFullyValid()) {
        return std::nullopt;
    }

    LOCK(cs_commitments);

    // Get all commitments for this AID
    auto commits_opt = GetCommitments(aid);
    if (!commits_opt) {
        LogInfo("QuantumCommitDB: No commitments found for AID during finalization\n");
        return std::nullopt;
    }

    AIDCommitments& aid_commits = m_cache[aid];

    // Already finalized?
    if (aid_commits.is_finalized) {
        return aid_commits.finalized_ctxid;
    }

    // Find the first valid commitment
    // Valid means: SDP = h(pubkey, txid) where txid matches CTXID
    std::vector<unsigned char> first_valid_ctxid;
    int earliest_height = std::numeric_limits<int>::max();

    for (const auto& entry : aid_commits.entries) {
        // Reconstruct the txid from the truncated CTXID
        // We need to check if this commitment is valid for this pubkey and tx
        const uint256& txid = tx.GetHash().ToUint256();
        std::vector<unsigned char> expected_ctxid = TruncateHash(txid, QUANTUM_COMMITMENT_CTXID_SIZE);

        // Check if CTXID matches
        if (entry.ctxid != expected_ctxid) {
            continue;
        }

        // Verify SDP = h(pubkey, txid)
        uint256 expected_sdp_hash = QuantumCommitmentHashSDP(pubkey, txid);
        std::vector<unsigned char> expected_sdp = TruncateHash(expected_sdp_hash, QUANTUM_COMMITMENT_SDP_SIZE);

        if (entry.sdp == expected_sdp) {
            // Valid commitment! Check if it's the earliest
            if (entry.nHeight < earliest_height) {
                earliest_height = entry.nHeight;
                first_valid_ctxid = entry.ctxid;
            }
        }
    }

    if (first_valid_ctxid.empty()) {
        LogInfo("QuantumCommitDB: No valid commitments found for AID\n");
        return std::nullopt;
    }

    // Finalize this AID
    aid_commits.is_finalized = true;
    aid_commits.finalized_ctxid = first_valid_ctxid;
    m_cache[aid] = aid_commits;
    m_cache_dirty = true;

    LogInfo("QuantumCommitDB: Finalized AID (height=%d)\n", earliest_height);

    return first_valid_ctxid;
}

bool CQuantumCommitmentDB::VerifyTransaction(const CTransaction& tx, const CPubKey& pubkey) const
{
    if (!pubkey.IsFullyValid()) {
        return false;
    }

    // Compute the AID for this pubkey
    uint256 aid_hash = QuantumCommitmentHashAID(pubkey);
    std::vector<unsigned char> aid = TruncateHash(aid_hash, QUANTUM_COMMITMENT_AID_SIZE);

    LOCK(cs_commitments);

    // Get commitments for this AID
    auto commits_opt = GetCommitments(aid);
    if (!commits_opt) {
        // No commitments found
        return false;
    }

    const AIDCommitments& aid_commits = commits_opt.value();

    // If already finalized, just check against the finalized CTXID
    if (aid_commits.is_finalized && !aid_commits.finalized_ctxid.empty()) {
        const uint256& txid = tx.GetHash().ToUint256();
        std::vector<unsigned char> tx_ctxid = TruncateHash(txid, QUANTUM_COMMITMENT_CTXID_SIZE);
        return tx_ctxid == aid_commits.finalized_ctxid;
    }

    // Not yet finalized - we need to check if this tx matches any valid commitment
    // This will also finalize the AID as a side effect
    // Note: We can't modify in a const function, so we return false here
    // The actual finalization will happen during validation
    return false;
}

bool CQuantumCommitmentDB::RemoveAID(const std::vector<unsigned char>& aid)
{
    LOCK(cs_commitments);

    // Remove from cache
    m_cache.erase(aid);

    // Remove from database
    CDBBatch batch(*m_db);
    batch.Erase(std::make_pair(DB_QUANTUM_COMMIT, aid));
    m_cache_dirty = true;

    m_db->WriteBatch(batch);
    return true;
}

bool CQuantumCommitmentDB::Flush()
{
    LOCK(cs_commitments);

    if (!m_cache_dirty) {
        return true;
    }

    // Write all cached entries to database
    CDBBatch batch(*m_db);
    for (const auto& [aid, commits] : m_cache) {
        batch.Write(std::make_pair(DB_QUANTUM_COMMIT, aid), commits);
    }

    m_db->WriteBatch(batch);
    m_cache_dirty = false;

    return true;
}

size_t CQuantumCommitmentDB::DynamicMemoryUsage() const
{
    LOCK(cs_commitments);

    size_t size = 0;
    for (const auto& [aid, commits] : m_cache) {
        size += aid.size();
        size += sizeof(AIDCommitments);
        size += commits.entries.size() * sizeof(CommitmentEntry);
    }
    return size;
}

bool CQuantumCommitmentDB::SetPoQCActivated(int nHeight)
{
    LOCK(cs_commitments);

    if (m_poqc_activation_height >= 0) {
        // Already activated, don't allow changing
        LogInfo("QuantumCommitDB: PoQC already activated at height %d\n",
                 m_poqc_activation_height);
        return true;
    }

    m_poqc_activation_height = nHeight;

    // Persist to database
    m_db->Write(DB_POQC_ACTIVATED, m_poqc_activation_height);

    LogInfo("QuantumCommitDB: PoQC ACTIVATED at height %d - Quantum commitment scheme now enforced\n", nHeight);
    return true;
}

bool CQuantumCommitmentDB::IsPoQCActivated(int* nHeight) const
{
    LOCK(cs_commitments);

    if (nHeight != nullptr) {
        *nHeight = m_poqc_activation_height;
    }

    return m_poqc_activation_height >= 0;
}

int CQuantumCommitmentDB::GetPoQCActivationHeight() const
{
    LOCK(cs_commitments);
    return m_poqc_activation_height;
}
