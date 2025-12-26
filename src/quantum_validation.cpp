// Copyright (c) 2025 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <quantum_validation.h>

#include <chain.h>
#include <coins.h>
#include <consensus/validation.h>
#include <logging.h>
#include <primitives/transaction.h>
#include <pubkey.h>
#include <quantum_commit.h>
#include <quantum_commitdb.h>
#include <script/script.h>
#include <script/solver.h>

#include <optional>

bool ProcessQuantumCommitments(const CBlock& block,
                               const CBlockIndex* pindex,
                               const CCoinsViewCache& view)
{
    if (!g_quantum_commitdb) {
        return true; // Database not initialized
    }

    // Check for PoQC in this block
    bool found_poqc = false;
    for (const auto& tx : block.vtx) {
        if (TransactionProvidesPoQC(*tx, view)) {
            found_poqc = true;
            LogInfo("QUANTUM ALERT: Proof of Quantum Computer detected in block %s at height %d\n",
                     pindex->GetBlockHash().ToString(), pindex->nHeight);
            break;
        }
    }

    // Activate PoQC if found
    if (found_poqc && !g_quantum_commitdb->IsPoQCActivated()) {
        if (!g_quantum_commitdb->SetPoQCActivated(pindex->nHeight)) {
            LogInfo("ERROR: Failed to activate PoQC\n");
            return false;
        }
    }

    // Extract and store quantum commitments from OP_RETURN outputs
    for (const auto& tx : block.vtx) {
        for (size_t i = 0; i < tx->vout.size(); i++) {
            const CTxOut& txout = tx->vout[i];

            // Try to extract quantum commitment from this output
            auto commitment_opt = ExtractQuantumCommitmentFromScript(txout.scriptPubKey);
            if (commitment_opt) {
                const auto& commitment = commitment_opt.value();

                // Store the commitment
                if (!g_quantum_commitdb->AddCommitment(commitment, pindex->nHeight)) {
                    LogInfo("Failed to add quantum commitment from tx %s output %d\n",
                            tx->GetHash().ToString(), i);
                }
            }
        }
    }

    return true;
}

std::optional<CPubKey> ExtractPubKeyFromTx(const CTransaction& tx,
                                           const CCoinsViewCache& view)
{
    if (tx.vin.empty()) {
        return std::nullopt;
    }

    // Get the first input
    const CTxIn& txin = tx.vin[0];

    // For witness transactions, check witness stack
    if (!txin.scriptWitness.IsNull() && !txin.scriptWitness.stack.empty()) {
        // For P2WPKH, the witness stack is: <signature> <pubkey>
        // For taproot, it's more complex but we look for pubkeys in the stack
        for (const auto& item : txin.scriptWitness.stack) {
            CPubKey pubkey(item.begin(), item.end());
            if (pubkey.IsFullyValid()) {
                return pubkey;
            }
        }
    }

    // For non-witness transactions, check scriptSig
    if (!txin.scriptSig.empty()) {
        CScript::const_iterator pc = txin.scriptSig.begin();
        std::vector<unsigned char> data;
        opcodetype opcode;

        // Extract push operations from scriptSig
        while (pc < txin.scriptSig.end()) {
            if (!txin.scriptSig.GetOp(pc, opcode, data)) {
                break;
            }

            // Check if this data is a valid pubkey
            if (!data.empty()) {
                CPubKey pubkey(data.begin(), data.end());
                if (pubkey.IsFullyValid()) {
                    return pubkey;
                }
            }
        }
    }

    return std::nullopt;
}

bool CheckQuantumCommitment(const CTransaction& tx,
                            TxValidationState& state,
                            int nHeight)
{
    if (!g_quantum_commitdb) {
        return true; // Database not initialized, skip check
    }

    // Check if PoQC has been activated
    int activation_height = g_quantum_commitdb->GetPoQCActivationHeight();
    if (activation_height < 0) {
        // PoQC not activated, commitments not required
        return true;
    }

    // Coinbase transactions don't need commitments
    if (tx.IsCoinBase()) {
        return true;
    }

    // PoQC is activated - we need to verify quantum commitment
    // First, we need to extract the pubkey from the transaction
    // Note: For a full implementation, we'd need access to the UTXO view here
    // For now, we return true and handle this in the main validation loop
    // where we have access to the full context

    return true; // Actual verification happens in main validation with full context
}

bool VerifyQuantumCommitmentInContext(const CTransaction& tx,
                                     TxValidationState& state,
                                     const CCoinsViewCache& view,
                                     int nHeight)
{
    if (!g_quantum_commitdb) {
        return true;
    }

    int activation_height = g_quantum_commitdb->GetPoQCActivationHeight();
    if (activation_height < 0 || nHeight < activation_height) {
        return true; // Not activated yet
    }

    if (tx.IsCoinBase()) {
        return true; // Coinbase exempt
    }

    // Extract pubkey from transaction
    auto pubkey_opt = ExtractPubKeyFromTx(tx, view);
    if (!pubkey_opt) {
        // No pubkey found - this could be a non-standard transaction
        // For safety, we reject it after PoQC activation
        return state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-quantum-no-pubkey",
                           "Transaction does not reveal a public key (required after PoQC)");
    }

    const CPubKey& pubkey = pubkey_opt.value();

    // Compute the AID for this pubkey
    uint256 aid_hash = QuantumCommitmentHashAID(pubkey);
    std::vector<unsigned char> aid = TruncateHash(aid_hash, QUANTUM_COMMITMENT_AID_SIZE);

    // Try to finalize the AID (this will find the first valid commitment)
    auto ctxid_opt = g_quantum_commitdb->FinalizeAID(aid, pubkey, tx);

    if (!ctxid_opt) {
        // No valid commitment found
        return state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-quantum-no-commitment",
                           strprintf("Transaction requires quantum commitment (PoQC activated at height %d)",
                                   activation_height));
    }

    // Verify the transaction matches the committed CTXID
    const uint256& txid = tx.GetHash().ToUint256();
    std::vector<unsigned char> tx_ctxid = TruncateHash(txid, QUANTUM_COMMITMENT_CTXID_SIZE);

    if (tx_ctxid != ctxid_opt.value()) {
        return state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-quantum-commitment-mismatch",
                           "Transaction does not match quantum commitment");
    }

    return true;
}
