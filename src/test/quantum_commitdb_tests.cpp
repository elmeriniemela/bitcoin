// Copyright (c) 2025 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <quantum_commitdb.h>

#include <key.h>
#include <primitives/transaction.h>
#include <quantum_commit.h>
#include <test/util/setup_common.h>

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(quantum_commitdb_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(db_initialization)
{
    // Create in-memory database
    CQuantumCommitmentDB db(1 << 20, true, false);

    // Check initial state
    BOOST_CHECK(!db.IsPoQCActivated());
    BOOST_CHECK_EQUAL(db.GetPoQCActivationHeight(), -1);
}

BOOST_AUTO_TEST_CASE(poqc_activation)
{
    CQuantumCommitmentDB db(1 << 20, true, false);

    // Activate PoQC at height 100
    BOOST_CHECK(db.SetPoQCActivated(100));
    BOOST_CHECK(db.IsPoQCActivated());

    int height;
    BOOST_CHECK(db.IsPoQCActivated(&height));
    BOOST_CHECK_EQUAL(height, 100);
    BOOST_CHECK_EQUAL(db.GetPoQCActivationHeight(), 100);

    // Try to activate again at different height (should keep original)
    BOOST_CHECK(db.SetPoQCActivated(200));
    BOOST_CHECK_EQUAL(db.GetPoQCActivationHeight(), 100);
}

BOOST_AUTO_TEST_CASE(add_commitment)
{
    CQuantumCommitmentDB db(1 << 20, true, false);

    // Create a commitment
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

    // Add commitment
    BOOST_CHECK(db.AddCommitment(commitment, 100));

    // Retrieve it
    auto commits_opt = db.GetCommitments(commitment.aid);
    BOOST_CHECK(commits_opt.has_value());
    BOOST_CHECK_EQUAL(commits_opt->entries.size(), 1);
    BOOST_CHECK(commits_opt->entries[0].sdp == commitment.sdp);
    BOOST_CHECK(commits_opt->entries[0].ctxid == commitment.ctxid);
    BOOST_CHECK_EQUAL(commits_opt->entries[0].nHeight, 100);
}

BOOST_AUTO_TEST_CASE(add_duplicate_commitment)
{
    CQuantumCommitmentDB db(1 << 20, true, false);

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

    // Add same commitment twice
    BOOST_CHECK(db.AddCommitment(commitment, 100));
    BOOST_CHECK(db.AddCommitment(commitment, 101));

    // Should only have one entry
    auto commits_opt = db.GetCommitments(commitment.aid);
    BOOST_CHECK(commits_opt.has_value());
    BOOST_CHECK_EQUAL(commits_opt->entries.size(), 1);
}

BOOST_AUTO_TEST_CASE(add_multiple_commitments_same_aid)
{
    CQuantumCommitmentDB db(1 << 20, true, false);

    // Create two different transactions
    CMutableTransaction mtx1, mtx2;
    mtx1.version = 2;
    mtx1.vin.resize(1);
    mtx1.vout.resize(1);
    mtx1.vout[0].nValue = 50 * COIN;

    mtx2.version = 2;
    mtx2.vin.resize(1);
    mtx2.vout.resize(1);
    mtx2.vout[0].nValue = 25 * COIN;

    CTransaction tx1(mtx1);
    CTransaction tx2(mtx2);

    CKey key;
    key.MakeNewKey(true);
    CPubKey pubkey = key.GetPubKey();

    // Both commitments will have same AID (same pubkey)
    QuantumCommitment commit1 = CreateQuantumCommitment(tx1, pubkey);
    QuantumCommitment commit2 = CreateQuantumCommitment(tx2, pubkey);

    // Add both
    BOOST_CHECK(db.AddCommitment(commit1, 100));
    BOOST_CHECK(db.AddCommitment(commit2, 101));

    // Should have two entries
    auto commits_opt = db.GetCommitments(commit1.aid);
    BOOST_CHECK(commits_opt.has_value());
    BOOST_CHECK_EQUAL(commits_opt->entries.size(), 2);
}

BOOST_AUTO_TEST_CASE(finalize_aid_single_valid)
{
    CQuantumCommitmentDB db(1 << 20, true, false);

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
    BOOST_CHECK(db.AddCommitment(commitment, 100));

    // Finalize
    auto ctxid_opt = db.FinalizeAID(commitment.aid, pubkey, tx);
    BOOST_CHECK(ctxid_opt.has_value());
    BOOST_CHECK(ctxid_opt.value() == commitment.ctxid);

    // Check it's marked as finalized
    auto commits_opt = db.GetCommitments(commitment.aid);
    BOOST_CHECK(commits_opt.has_value());
    BOOST_CHECK(commits_opt->is_finalized);
    BOOST_CHECK(commits_opt->finalized_ctxid == commitment.ctxid);
}

BOOST_AUTO_TEST_CASE(finalize_aid_first_valid_wins)
{
    CQuantumCommitmentDB db(1 << 20, true, false);

    // Create two different transactions
    CMutableTransaction mtx1, mtx2;
    mtx1.version = 2;
    mtx1.vin.resize(1);
    mtx1.vout.resize(1);
    mtx1.vout[0].nValue = 50 * COIN;

    mtx2.version = 2;
    mtx2.vin.resize(1);
    mtx2.vout.resize(1);
    mtx2.vout[0].nValue = 25 * COIN;

    CTransaction tx1(mtx1);
    CTransaction tx2(mtx2);

    CKey key;
    key.MakeNewKey(true);
    CPubKey pubkey = key.GetPubKey();

    QuantumCommitment commit1 = CreateQuantumCommitment(tx1, pubkey);
    QuantumCommitment commit2 = CreateQuantumCommitment(tx2, pubkey);

    // Add commit2 first (at height 100)
    BOOST_CHECK(db.AddCommitment(commit2, 100));
    // Add commit1 later (at height 101)
    BOOST_CHECK(db.AddCommitment(commit1, 101));

    // Finalize with tx1 - should pick commit1 since it's the valid one for tx1
    // But first need to ensure we're testing the right tx
    auto ctxid_opt = db.FinalizeAID(commit1.aid, pubkey, tx1);
    BOOST_CHECK(ctxid_opt.has_value());
    BOOST_CHECK(ctxid_opt.value() == commit1.ctxid);
}

BOOST_AUTO_TEST_CASE(finalize_aid_invalid_commitment)
{
    CQuantumCommitmentDB db(1 << 20, true, false);

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

    // Create an invalid commitment (wrong SDP)
    QuantumCommitment invalid_commit;
    invalid_commit.aid = commitment.aid;
    invalid_commit.sdp = std::vector<unsigned char>(16, 0xFF);  // Wrong SDP
    invalid_commit.ctxid = commitment.ctxid;

    BOOST_CHECK(db.AddCommitment(invalid_commit, 100));

    // Try to finalize - should fail
    auto ctxid_opt = db.FinalizeAID(commitment.aid, pubkey, tx);
    BOOST_CHECK(!ctxid_opt.has_value());
}

BOOST_AUTO_TEST_CASE(remove_aid)
{
    CQuantumCommitmentDB db(1 << 20, true, false);

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
    BOOST_CHECK(db.AddCommitment(commitment, 100));

    // Verify it exists
    BOOST_CHECK(db.GetCommitments(commitment.aid).has_value());

    // Remove it
    BOOST_CHECK(db.RemoveAID(commitment.aid));

    // Verify it's gone
    BOOST_CHECK(!db.GetCommitments(commitment.aid).has_value());
}

BOOST_AUTO_TEST_CASE(cannot_add_after_finalize)
{
    CQuantumCommitmentDB db(1 << 20, true, false);

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
    BOOST_CHECK(db.AddCommitment(commitment, 100));

    // Finalize
    BOOST_CHECK(db.FinalizeAID(commitment.aid, pubkey, tx).has_value());

    // Try to add another commitment for same AID
    CMutableTransaction mtx2;
    mtx2.version = 2;
    mtx2.vin.resize(1);
    mtx2.vout.resize(1);
    mtx2.vout[0].nValue = 25 * COIN;
    CTransaction tx2(mtx2);

    QuantumCommitment commit2 = CreateQuantumCommitment(tx2, pubkey);

    // Should fail or be ignored
    BOOST_CHECK(!db.AddCommitment(commit2, 101));
}

BOOST_AUTO_TEST_CASE(flush_and_reload)
{
    // Create temporary directory for database
    fs::path temp_dir = m_args.GetDataDirNet() / "quantum_test";
    fs::create_directories(temp_dir);

    {
        // Create database and add commitment
        CQuantumCommitmentDB db(1 << 20, false, true);  // Disk-backed, wipe first

        db.SetPoQCActivated(100);

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
        db.AddCommitment(commitment, 100);

        // Flush to disk
        BOOST_CHECK(db.Flush());
    }

    {
        // Reload database
        CQuantumCommitmentDB db(1 << 20, false, false);  // Don't wipe

        // Check PoQC state persisted
        BOOST_CHECK(db.IsPoQCActivated());
        BOOST_CHECK_EQUAL(db.GetPoQCActivationHeight(), 100);
    }

    // Cleanup
    fs::remove_all(temp_dir);
}

BOOST_AUTO_TEST_SUITE_END()
