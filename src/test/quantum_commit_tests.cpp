// Copyright (c) 2025 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <quantum_commit.h>

#include <key.h>
#include <primitives/transaction.h>
#include <pubkey.h>
#include <script/script.h>
#include <test/util/setup_common.h>
#include <uint256.h>

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(quantum_commit_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(hash_aid_consistency)
{
    // Test that AID hash is deterministic and consistent
    CKey key;
    key.MakeNewKey(true);
    CPubKey pubkey = key.GetPubKey();

    uint256 aid1 = QuantumCommitmentHashAID(pubkey);
    uint256 aid2 = QuantumCommitmentHashAID(pubkey);

    BOOST_CHECK_EQUAL(aid1, aid2);
}

BOOST_AUTO_TEST_CASE(hash_aid_different_pubkeys)
{
    // Test that different pubkeys produce different AIDs
    CKey key1, key2;
    key1.MakeNewKey(true);
    key2.MakeNewKey(true);

    CPubKey pubkey1 = key1.GetPubKey();
    CPubKey pubkey2 = key2.GetPubKey();

    uint256 aid1 = QuantumCommitmentHashAID(pubkey1);
    uint256 aid2 = QuantumCommitmentHashAID(pubkey2);

    BOOST_CHECK_NE(aid1, aid2);
}

BOOST_AUTO_TEST_CASE(hash_sdp_consistency)
{
    // Test that SDP hash is deterministic
    CKey key;
    key.MakeNewKey(true);
    CPubKey pubkey = key.GetPubKey();
    uint256 txid = *uint256::FromHex("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");

    uint256 sdp1 = QuantumCommitmentHashSDP(pubkey, txid);
    uint256 sdp2 = QuantumCommitmentHashSDP(pubkey, txid);

    BOOST_CHECK_EQUAL(sdp1, sdp2);
}

BOOST_AUTO_TEST_CASE(hash_sdp_binds_to_txid)
{
    // Test that different txids produce different SDPs
    CKey key;
    key.MakeNewKey(true);
    CPubKey pubkey = key.GetPubKey();

    uint256 txid1 = *uint256::FromHex("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");
    uint256 txid2 = *uint256::FromHex("fedcba9876543210fedcba9876543210fedcba9876543210fedcba9876543210");

    uint256 sdp1 = QuantumCommitmentHashSDP(pubkey, txid1);
    uint256 sdp2 = QuantumCommitmentHashSDP(pubkey, txid2);

    BOOST_CHECK_NE(sdp1, sdp2);
}

BOOST_AUTO_TEST_CASE(commitment_creation)
{
    // Create a simple transaction
    CMutableTransaction mtx;
    mtx.version = 2;
    mtx.vin.resize(1);
    mtx.vin[0].prevout = COutPoint(Txid::FromUint256(*uint256::FromHex("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef")), 0);
    mtx.vout.resize(1);
    mtx.vout[0].nValue = 50 * COIN;

    CTransaction tx(mtx);

    CKey key;
    key.MakeNewKey(true);
    CPubKey pubkey = key.GetPubKey();

    // Create commitment
    QuantumCommitment commitment = CreateQuantumCommitment(tx, pubkey);

    // Check validity
    BOOST_CHECK(commitment.IsValid());
    BOOST_CHECK_EQUAL(commitment.aid.size(), QUANTUM_COMMITMENT_AID_SIZE);
    BOOST_CHECK_EQUAL(commitment.sdp.size(), QUANTUM_COMMITMENT_SDP_SIZE);
    BOOST_CHECK_EQUAL(commitment.ctxid.size(), QUANTUM_COMMITMENT_CTXID_SIZE);
}

BOOST_AUTO_TEST_CASE(commitment_verification_valid)
{
    // Create transaction and commitment
    CMutableTransaction mtx;
    mtx.version = 2;
    mtx.vin.resize(1);
    mtx.vout.resize(1);
    mtx.vout[0].nValue = 50 * COIN;
    CTransaction tx(mtx);

    CKey key;
    key.MakeNewKey(true);
    CPubKey pubkey = key.GetPubKey();

    QuantumCommitment commitment = CreateQuantumCommitment(tx, pubkey);

    // Verify commitment
    BOOST_CHECK(VerifyQuantumCommitment(tx, commitment, pubkey));
}

BOOST_AUTO_TEST_CASE(commitment_verification_wrong_pubkey)
{
    // Create transaction and commitment with one pubkey
    CMutableTransaction mtx;
    mtx.version = 2;
    mtx.vin.resize(1);
    mtx.vout.resize(1);
    mtx.vout[0].nValue = 50 * COIN;
    CTransaction tx(mtx);

    CKey key1, key2;
    key1.MakeNewKey(true);
    key2.MakeNewKey(true);
    CPubKey pubkey1 = key1.GetPubKey();
    CPubKey pubkey2 = key2.GetPubKey();

    QuantumCommitment commitment = CreateQuantumCommitment(tx, pubkey1);

    // Try to verify with wrong pubkey
    BOOST_CHECK(!VerifyQuantumCommitment(tx, commitment, pubkey2));
}

BOOST_AUTO_TEST_CASE(commitment_verification_wrong_tx)
{
    // Create two different transactions
    CMutableTransaction mtx1, mtx2;
    mtx1.version = 2;
    mtx1.vin.resize(1);
    mtx1.vout.resize(1);
    mtx1.vout[0].nValue = 50 * COIN;

    mtx2.version = 2;
    mtx2.vin.resize(1);
    mtx2.vout.resize(1);
    mtx2.vout[0].nValue = 25 * COIN;  // Different amount

    CTransaction tx1(mtx1);
    CTransaction tx2(mtx2);

    CKey key;
    key.MakeNewKey(true);
    CPubKey pubkey = key.GetPubKey();

    // Create commitment for tx1
    QuantumCommitment commitment = CreateQuantumCommitment(tx1, pubkey);

    // Try to verify tx2 with tx1's commitment
    BOOST_CHECK(!VerifyQuantumCommitment(tx2, commitment, pubkey));
}

BOOST_AUTO_TEST_CASE(commitment_serialization)
{
    // Create commitment
    CMutableTransaction mtx;
    mtx.version = 2;
    mtx.vin.resize(1);
    mtx.vout.resize(1);
    mtx.vout[0].nValue = 50 * COIN;
    CTransaction tx(mtx);

    CKey key;
    key.MakeNewKey(true);
    CPubKey pubkey = key.GetPubKey();

    QuantumCommitment commitment = CreateQuantumCommitment(tx, pubkey);

    // Serialize
    std::vector<unsigned char> serialized = commitment.Serialize();
    BOOST_CHECK_EQUAL(serialized.size(), QUANTUM_COMMITMENT_FULL_SIZE);

    // Deserialize
    auto deserialized = QuantumCommitment::Deserialize(serialized);
    BOOST_CHECK(deserialized.has_value());
    BOOST_CHECK(deserialized->IsValid());

    // Check equality
    BOOST_CHECK(deserialized->aid == commitment.aid);
    BOOST_CHECK(deserialized->sdp == commitment.sdp);
    BOOST_CHECK(deserialized->ctxid == commitment.ctxid);
}

BOOST_AUTO_TEST_CASE(commitment_deserialization_invalid_size)
{
    // Try to deserialize wrong size
    std::vector<unsigned char> invalid_data(32, 0);  // Wrong size
    auto result = QuantumCommitment::Deserialize(invalid_data);
    BOOST_CHECK(!result.has_value());
}

BOOST_AUTO_TEST_CASE(commitment_script_creation)
{
    // Create commitment
    CMutableTransaction mtx;
    mtx.version = 2;
    mtx.vin.resize(1);
    mtx.vout.resize(1);
    mtx.vout[0].nValue = 50 * COIN;
    CTransaction tx(mtx);

    CKey key;
    key.MakeNewKey(true);
    CPubKey pubkey = key.GetPubKey();

    QuantumCommitment commitment = CreateQuantumCommitment(tx, pubkey);

    // Create OP_RETURN script
    CScript script = CreateQuantumCommitmentScript(commitment);

    // Check it starts with OP_RETURN
    BOOST_CHECK(!script.empty());
    BOOST_CHECK_EQUAL(script[0], OP_RETURN);

    // Extract commitment from script
    auto extracted = ExtractQuantumCommitmentFromScript(script);
    BOOST_CHECK(extracted.has_value());
    BOOST_CHECK(extracted->aid == commitment.aid);
    BOOST_CHECK(extracted->sdp == commitment.sdp);
    BOOST_CHECK(extracted->ctxid == commitment.ctxid);
}

BOOST_AUTO_TEST_CASE(commitment_script_extraction_invalid)
{
    // Non-OP_RETURN script
    CScript script = CScript() << OP_DUP << OP_HASH160;
    auto result = ExtractQuantumCommitmentFromScript(script);
    BOOST_CHECK(!result.has_value());

    // OP_RETURN with wrong data size
    CScript wrong_size = CScript() << OP_RETURN << std::vector<unsigned char>(32, 0);
    result = ExtractQuantumCommitmentFromScript(wrong_size);
    BOOST_CHECK(!result.has_value());
}

BOOST_AUTO_TEST_CASE(poqc_script_detection)
{
    // OP_SHA256 OP_CHECKSIG
    CScript poqc_script;
    poqc_script << OP_SHA256 << OP_CHECKSIG;

    BOOST_CHECK(IsProofOfQuantumComputer(poqc_script));

    // Not a PoQC script
    CScript normal_script;
    normal_script << OP_DUP << OP_HASH160;
    BOOST_CHECK(!IsProofOfQuantumComputer(normal_script));

    // Wrong size
    CScript wrong_size;
    wrong_size << OP_SHA256;
    BOOST_CHECK(!IsProofOfQuantumComputer(wrong_size));
}

BOOST_AUTO_TEST_CASE(truncate_hash)
{
    uint256 hash = *uint256::FromHex("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");

    auto truncated = TruncateHash(hash, 16);
    BOOST_CHECK_EQUAL(truncated.size(), 16);

    // Check it's the first 16 bytes
    for (size_t i = 0; i < 16; i++) {
        BOOST_CHECK_EQUAL(truncated[i], hash.begin()[i]);
    }
}

BOOST_AUTO_TEST_SUITE_END()
