// Copyright (c) 2025 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QUANTUM_VALIDATION_H
#define BITCOIN_QUANTUM_VALIDATION_H

#include <consensus/validation.h>
#include <primitives/block.h>
#include <primitives/transaction.h>

#include <optional>

class CBlockIndex;
class CCoinsViewCache;
class CPubKey;

/**
 * Process quantum commitments in a block
 *
 * This function:
 * 1. Scans for PoQC (Proof of Quantum Computer) transactions
 * 2. Extracts quantum commitments from OP_RETURN outputs
 * 3. Stores commitments in the quantum commitment database
 *
 * Should be called during ConnectBlock before transaction validation
 *
 * @param block The block being connected
 * @param pindex The block index
 * @param view The UTXO view
 * @return true if processing succeeded, false otherwise
 */
bool ProcessQuantumCommitments(const CBlock& block,
                               const CBlockIndex* pindex,
                               const CCoinsViewCache& view);

/**
 * Verify quantum commitment for a transaction
 *
 * If PoQC has been activated, this function verifies that the transaction
 * has a valid quantum commitment. Returns true if:
 * - PoQC is not activated (commitment not required), OR
 * - Transaction is coinbase, OR
 * - Transaction has a valid commitment
 *
 * @param tx The transaction to verify
 * @param state Validation state for error reporting
 * @param nHeight Block height where transaction is being validated
 * @return true if quantum commitment is valid or not required
 */
bool CheckQuantumCommitment(const CTransaction& tx,
                            TxValidationState& state,
                            int nHeight);

/**
 * Extract public key from transaction for quantum commitment verification
 *
 * Extracts the first public key from the transaction's witness/scriptSig
 * This is used to verify quantum commitments
 *
 * @param tx The transaction
 * @param view The UTXO view to get spent outputs
 * @return The public key if found, nullopt otherwise
 */
std::optional<CPubKey> ExtractPubKeyFromTx(const CTransaction& tx,
                                           const CCoinsViewCache& view);

#endif // BITCOIN_QUANTUM_VALIDATION_H
