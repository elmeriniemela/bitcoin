# Post-Quantum commit / reveal Fawkescoin variant as a soft fork
https://groups.google.com/g/bitcoindev/c/LpWOcXMcvk8?pli=1

One of the tricky things about securing Bitcoin against quantum computers is: do you even need to?  Maybe quantum computers that can break secp256k1 keys will never exist, in which case we shouldn't waste our time.  Or maybe they will exist, in not too many years, and we should spend the effort to secure the system against QCs.

Since people disagree on how likely QCs are to arrive, and what the timing would be if they do, it's hard to get consensus on changes to bitcoin that disrupt the properties we use today.  For example, a soft fork introducing a post-quantum (PQ) signature scheme and at the same time disallowing new secp256k1 based outputs would be great for strengthening Bitcoin against an oncoming QC.  But it would be awful if a QC never appears, or takes decades to do so, since secp256k1 is really nice.

So it would be nice to have a way to not deal with this issue until *after* the QC shows up.  With commit / reveal schemes Bitcoin can keep working after a QC shows up, even if we haven't defined a PQ signature scheme and everyone's still got P2WPKH outputs.

Most of this is similar to Tim Ruffing's proposal from a few years ago here:
https://gnusha.org/pi/bitcoindev/1518710367.3...@mmci.uni-saarland.de/

The main difference is that this scheme doesn't use encryption, but a smaller hash-based commitment, and describes activation as a soft fork.  I'll define the two types of attacks, a commitment scheme, and then say how it can be implemented in bitcoin nodes as a soft fork.

This scheme only works for keys that are pubkey hashes (or script hashes) with pubkeys that are unknown to the network.  It works with taproot as well, but there must be some script-path in the taproot key, as keypath spends would no longer be secure.

What to do with all the keys that are known is another issue and independent of the scheme in this post (it's compatible with both burning them and leaving them to be stolen)

For these schemes, we assume there is an attacker with a QC that can compute a quickly compute a private key from any secp256k1 public key.  We also assume the attacker has some mining power or influence over miners for their attacks; maybe not reliably, but they can sometimes get a few blocks in a row with the transactions they want.

"Pubkey" can also be substituted with "script" for P2SH and P2WSH output types and should work about the same way (with caveats about multisig).  The equivalent for taproot outputs would be an inner key proving a script path.

## A simple scheme to show an attack

The simplest commit/reveal scheme would be one where after activation, for any transaction with an EC signature in it, that transaction's txid must appear in a earlier transaction's OP_RETURN output.

When a user wants to spend their coins, they first sign a transaction as they would normally, compute the txid, get that txid into an OP_RETURN output somehow (paying a miner out of band, etc), then after waiting a while, broadcast the transaction.  Nodes would check that the txid matches a previously seen commitment, and allow the transaction.

One problem with this scheme is that upon seeing the full transaction, the attacker can compute the user's private key, and create a new commitment with a different txid for a transaction where the attacker gets all the coins.  If the attacker can get their commitment and spending transaction in before the user's transaction, they can steal the coins.

In order to mitigate this problem, a minimum delay can be enforced by consensus.  A minimum delay of 100 blocks would mean that the attacker would have to prevent the user's transaction from being confirmed for 100 blocks after it showed up in the attacker's mempool.  The tradeoff is that longer periods give better safety at the cost of more delay in spending.

This scheme, while problematic, is better than nothing!  But it's possible to remove this timing tradeoff.


## A slightly more complex scheme with (worse) problems

If instead of just the txid, the commitment were both the outpoint being spent, and the txid that was going to spend it, we could add a "first seen" consensus rule.  Only the first commitment pointing to an outpoint works.

So if nodes see two OP_RETURN commitments in their sequence of confirmed transactions:

C1 = outpoint1, txid1
C2 = outpoint1, txid2

They can ignore C2; C1 has already laid claim to outpoint1, and the transaction identified by txid1 is the only transaction that can spend outpoint1.

If the user manages to get C1 confirmed first, this is great, and eliminates the timing problem in the txid only scheme.  But this introduces a different problem, where an attacker -- in this case any attacker, even one without a QC -- who can observe C1 before it is confirmed can flip some bits in the txid field, freezing the outpoint forever.

We want to retain the "first seen" rule, but we want to also be able to discard invalid commitments.  In a bit flipping attack, we could say an invalid commitment is one where there is no transaction described by the txid.  A more general way to classify a commitment as invalid is a commitment made without knowledge of the (secret) pubkey.  Knowledge of the pubkey is what security of coins is now hinging on.


The actual commitment scheme


We define some hash function h().  We'll use SHA256 for the hashing, but it needs to be keyed with some tag, for example "Alas poor Koblitz curve, we knew it well".

Thus h(pubkey) is not equal to the pubkey hash already used in the bitcoin output script, which instead is RIPEMD160(SHA256(pubkey)), or in bitcoin terms, HASH160(pubkey).  Due to the hash functions being different, A = HASH160(pubkey) and B = h(pubkey) will be completely different, and nobody should be able to determine if A and B are hashes of the same pubkey without knowing pubkey itself.

An efficient commitment is:

C =  h(pubkey), h(pubkey, txid), txid
(to label things: C = AID, SDP, CTXID)

This commitment includes 3 elements: a different hash of the pubkey which will be signed for, a proof of knowledge of the pubkey which commits to a transaction, and an the txid of the spending transaction.  We'll call these "address ID" (AID), sequence dependent proof (SDP), and the commitment txid (CTXID).

For those familiar with the proposal by Ruffing, the SDP has a similar function to the authenticated encryption part of the encrypted commitment.  Instead of using authenticated encryption, we can instead just use an HMAC-style authentication alone, since the other data, the CTXID, is provided.

When the user's wallet creates a transaction, they can feed that transaction into a commitment generator function which takes in a transaction, extracts the pubkey from the tx, computes the 3 hashes, and returns the 3-hash commitment.  Once this commitment is confirmed, the user broadcasts the transaction.

Nodes verify the commitment by using the same commitment generator function and checking if it matches the first valid commitment for that AID, in which case the tx is confirmed.

If a node sees multiple commitments all claiming the same AID, it must store all of them.  Once the AID's pubkey is known, the node can distinguish which commitments are valid, which are invalid, and which is the first seen valid commitment.  Given the pubkey, nodes can determine commitments to be invalid by checking if SDP = h(pubkey, CTXID).

As an example, consider a sequence of 3 commitments:

C1 = h(pubkey), h(pubkey', txid1), txid1
C2 = h(pubkey), h(pubkey, txid2), txid2
C3 = h(pubkey), h(pubkey, txid3), txid3

The user first creates tx2 and tries to commit C2.  But an attacker creates C1, committing to a different txid where they control the outputs, and confirms it first.  This attacker may know the outpoint being spent, and may be able to create a transaction and txid that could work.  But they don't know the pubkey, so while they can copy the AID hash, they have to make something up for the SDP.

The user gets C2 confirmed after C1.  They then reveal tx2 in the mempool, but before it can be confirmed, the attacker gets C3 confirmed.  C3 is a valid commitment made with knowledge of the pubkey.

Nodes can reject transactions tx1 and tx3.  For tx1, they will see that the SDP doesn't match the data in the transaction, so it's an invalid commitment.  For tx3, they will see that it is valid, but by seeing tx3 they will also be able to determine that C2 is a valid commitment (since pubkey is revealed in tx3) which came prior to C3, making C2 the only valid commitment for that AID.


## Implementation

Nodes would keep a new key/value store, similar to the existing UTXO set.  The indexing key would be the AID, and the value would be the set of all (SDP, CTXID) pairs seen alongside that AID.  Every time an commitment is seen in an OP_RETURN, nodes store the commitment.

When a transaction is seen, nodes observe the pubkey used in the transaction, and look up if it matches an AID they have stored.  If not, the transaction is dropped.  If the AID does match, the node can now "clean out" an AID entry, eliminating all but the first valid commitment, and marking that AID as final.  If the txid seen matches the remaining commitment, the transaction is valid; if not, the transaction is dropped.

After the transaction is confirmed the AID entry can be deleted.  Deleting the entries frees up space, and would allow another round to happen with the same pubkey, which would lead to theft.  Retaining the entries takes up more space on nodes that can't be pruned, and causes pubkey reuse to destroy coins rather than allow them to be stolen.  That's a tradeoff, and I personally guess it's probably not worth retaining that data but don't have a strong opinion either way.

Short commitments:

Since we're not trying to defend against collision attacks, I think all 3 hashes can be truncated to 16 bytes.  The whole commitment could be 48 bytes long.  Without truncation the commitments would be 96 bytes.


## Activation

The activation for the commit/reveal requirement can be triggered by a proof of quantum computer (PoQC).

A transaction which successfully spends an output using tapscript:

OP_SHA256 OP_CHECKSIG

is a PoQC in the form of a valid bitcoin transaction.  In order to satisfy this script, the spending transaction needs to provide 2 data elements: a signature, and some data that when hashed results in a pubkey for which that signature is valid.  If such a pair of data elements exists, it means that either SHA256 preimage resistance is broken (which we're assuming isn't the case) or someone can create valid signatures for arbitrary elliptic curve points, ie a cryptographically relevant quantum computer (or any other process which breaks the security of secp256k1 signatures)

Once such a PoQC has been observed in a confirmed transaction, the requirements for the 3-hash commitment scheme can be enforced.  This is a soft fork since the transactions themselves look the same, the only requirement is that some OP_RETURN outputs show up earlier.  Nodes which are not aware of the commitment requirement will still accept all transactions with the new rules.

Wallets not aware of the new rules, however, are very dangerous, as they may try to broadcast signed transactions without any commitment.  Nodes that see such a transaction should drop the tx, and if possible tell the wallet that they are doing something which is now very dangerous!  On the open p2p network this is not really enforceable, but people submitting transactions to their own node (eg via RPC) can at least get a scary error message.


## Issues

My hope is that this scheme would give some peace of mind to people holding bitcoin, that in the face of a sudden QC, even with minimal preparation their coins can be safe at rest and safely moved.  It also suggests some best practices for users and wallets to adopt, before any software changes: Don't reuse addresses, and if you have taproot outputs, include some kind of script path in the outer key.

There are still a number of problems, though!

- Reorgs can steal coins.  An attacker that observes a pubkey and can reorg back to before the commitment can compute the private key, sign a new transaction and get their commitment in first on the new chain.  This seems unavoidable with commit/reveal schemes, and it's up to the user how long they wait between confirming the commitment and revealing the transaction.

- How to get op_returns in
If there are no PQ signature schemes activated in bitcoin when this activates, there's only one type of transaction that can reliably get the OP_RETURN outputs confirmed: coinbase transactions.  Getting commitments to the miners and paying them out of band is not great, but is possible and we see this kind of activity today.  Users wouldn't need to directly contact miners: anyone could aggregate commitments, create a large transaction with many OP_RETURN outputs, and then get a miner to commit to that parent transaction.  Users don't need to worry about committing twice as identical commitments would be a no op.

- Spam
Anyone can make lots of OP_RETURN commitments which are just random numbers, forcing nodes to store these commitments in a database.  That's not great, but isn't much different from how bitcoin works today.  If it's really a problem, nodes could requiring the commitment outputs to have a non-0 amount of bitcoin, imposing a higher cost for the commitments than other OP_RETURN outputs.

- Multiple inputs
If users have received more than one UTXO to the same address, they will need to spend all the UTXOs at once.  The commitment scheme can deal with only the first pubkey seen in the serialized transaction.

- Multisig and Lightning Network
If your multisig counterparties have a QC, multisig outputs become 1 of N.  Possibly a more complex commit / reveal scheme could deal with multiple keys, but the keys would all have to be hashed with counterparties not knowing each others' unhashed pubkeys.  This isn't how existing multisig outputs work, and in fact the current trend is the opposite with things like Musig2, FROST and ROAST.  If we're going to need to make new signing software and new output types it might make more sense to go for a PQ signature scheme.

- Making more p2wpkhs
You don't have to send to a PQ address type with these transactions -- you can send to p2wpkh and do the whole commit/reveal process again when you want to spend.  This could be helpful if PQ signature schemes are still being worked on, or if the PQ schemes are more costly to verify and have high fees in comparison to the old p2wpkh output types.  It's possible that in such a scenario a few high-cost PQ transactions commit to many smaller EC transactions.  If this actually gets adoption though, we might as well drop the EC signatures and just make output scripts into raw hash / preimage pairs.  It could make sense to cover some non-EC script types with the same 3-hash commitment requirement to enable this.

## Conclusion

This PQ commit / reveal scheme has similar properties to Tim Ruffing's, with a smaller commitment that can be done as a soft fork.  I hope something like this could be soft forked with a PoQC activation trigger, so that if a QC never shows up, none of this code gets executed.  And people who take a couple easy steps like not reusing addresses (which they should anyway for privacy reasons) don't have to worry about their coins.

Some of these ideas may have been posted before; I know of the Fawkscoin paper (https://jbonneau.com/doc/BM14-SPW-fawkescoin.pdf) and the recent discussion which linked to Ruffing's proposal.  Here I've tried to show how it could be done in a soft fork which doesn't look too bad to implement.

I've also heard of some more complex schemes involving zero knowledge proofs, proving things like BIP32 derivations, but I think this gives some pretty good properties without needing anything other than good old SHA256.

Hope this is useful & wonder if people think something like this would be a good idea.

-Tadge



## Implementation Plan (Minimal, Regtest-First)

### Summary
- Implement a minimal, testable soft-fork prototype that enforces commit/reveal after activation.
- Scope is intentionally narrow: regtest consensus only, first-input `P2WPKH` reveals only, delete commitment state after successful spend.
- Reuse existing commit/reveal scaffolding and add only the missing consensus path.

### Implementation Changes
1. Add a small lifeboat state machine in validation (`src/validation.cpp`, `src/validation.h`):
- Track activation state (`inactive` -> `active`) via:
- first confirmed PoQC spend (`P2TR` script-path leaf exactly `OP_SHA256 OP_CHECKSIG`), or
- regtest override height.
- Maintain in-memory commitment index keyed by 16-byte `AID`:
- collect commitments from confirmed `OP_RETURN` outputs,
- store commit position `(height, tx_index, vout_index)` for deterministic earliest-valid selection,
- support connect/disconnect updates for reorg correctness.
- Enforce post-activation rule for applicable reveals:
- applicable tx = non-coinbase tx whose first input spends `P2WPKH`,
- extract pubkey from first input witness stack,
- require earliest valid commitment for `AID` to match tx (`SDP` and `CTXID`),
- reject otherwise,
- on successful block connection, delete that `AID` entry.

2. Finish and normalize commit helper module (`src/commit_reveal.cpp`, `src/commit_reveal.h`):
- Fix incomplete/stubbed pieces (non-void return path).
- Keep one canonical tag/name set (`SDP`, not mixed `SPD/SDP`).
- Add deterministic commitment parsing/validation helpers reused by validation.

3. Add regtest-only activation override plumbing (`src/chainparams.cpp`, `src/chainparamsbase.cpp`, `src/kernel/chainparams.h`):
- New debug arg: `-lifeboatactivationheight=<n>` (regtest-only).
- Default remains PoQC-triggered; override exists only for deterministic tests.

### Test Plan
1. Unit tests:
- Commitment serialization/deserialization and malformed `OP_RETURN` parsing.
- `AID/SDP/CTXID` derivation and truncation determinism.
- Earliest-valid commitment selection with mixed valid/invalid commitments.

2. Functional regtest test:
- Pre-activation `P2WPKH` spend without commitment is accepted.
- Activation via override height (plus PoQC-detection smoke path if feasible).
- Post-activation `P2WPKH` spend without commitment is rejected (mempool/block).
- Valid prior commitment in earlier block allows reveal spend.
- Earlier invalid attacker commitment does not block later valid user commitment.
- After successful reveal, entry is deleted; new spend requires new commitment.
- Reorg: disconnecting commitment block invalidates dependent reveal until recommitted.

### Assumptions and Defaults
- Scope is regtest consensus for this iteration; no main/test/signet deployment wiring.
- Coverage is first-input `P2WPKH` only (no P2SH/P2WSH/multisig/taproot key-path yet).
- Commitment must be in an earlier confirmed block (not same-block ordering in v1).
- State is in-memory and rebuilt from active-chain blocks on startup.
- `commitrawtransaction` RPC stub is not required for consensus MVP.


## Recommended Learning-First Implementation Order (RPC-First)

Starting with RPC makes sense for learning velocity and quick feedback loops, with one caveat: RPC-first does not validate consensus/reorg correctness.

### Why RPC-First Is Useful

- Fast manual iteration with bitcoin-cli.
- Easy to inspect raw hex and decoded transactions.
- Lets you validate commitment data format and parsing before touching consensus paths.

### Caveat

- RPC and policy behavior are not the same as consensus behavior.
- The actual soft-fork logic still needs to be implemented in validation/block-connection paths.

### Suggested Order

1. Finish commit/reveal library first

- File: src/commit_reveal.cpp
- File: src/commit_reveal.h
- Goal: make commitment creation/parsing/verification complete and deterministic.
- Note: there is already scaffolding and an incomplete function to finish.

2. Wire a minimal RPC wrapper next

- File: src/rpc/rawtransaction.cpp
- Goal: make commitrawtransaction usable as a manual test harness.
- Note: there is already a stubbed commitrawtransaction() and it currently needs proper wiring/registration.

3. Add consensus enforcement last

- File: src/validation.cpp (and related state plumbing)
- Goal: enforce commit/reveal rules under activation conditions and handle reorg-safe behavior.
- This is the real feature; RPC is only developer tooling around it.

### Practical Outcome

- You get quick wins and high learning density early (library + RPC).
- You reduce risk before entering the hardest part (consensus integration).
- You keep manual testing available throughout development.