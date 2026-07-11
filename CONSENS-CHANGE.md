# Consensus Change Considerations

This document is a map of the surfaces to review before attempting a Bitcoin
Core consensus change. It is written for codebase study, not as an endorsement
of changing consensus rules.

A consensus change is any change that can alter one of these outcomes for a
node:

- Whether a block header is valid.
- Whether a block is valid at a given height or after a given previous block.
- Whether a transaction is valid inside a block.
- The resulting UTXO set after connecting or disconnecting blocks.
- Which valid chain is selected as best.

Policy-only changes, wallet behavior, mining template behavior, and relay
behavior may be necessary for deployment, but they are not sufficient to define
consensus. The final consensus enforcement path must be reachable when a node
validates blocks from disk, during reorgs, during reindex, and when accepting
new blocks from peers.

## 1. Classify The Change

- Determine whether the change is a soft fork, hard fork, cleanup of undefined
  behavior, bug fix, policy-only change, or a non-consensus interface change.
  The classification decides whether old nodes will accept or reject blocks
  produced under the new rules.
- Define exactly which objects are affected: headers, block structure,
  coinbase, transaction serialization, script execution, signature hashing,
  UTXO accounting, difficulty, time locks, witness commitments, or chain
  selection.
- Identify whether the rule tightens validity or expands validity. Tightening a
  validity rule can be a soft fork if old nodes still see the block as valid.
  Expanding validity beyond the blocks or transactions old nodes would accept is
  not a soft fork; a soft-fork design has to use behavior old nodes already
  accept and then add stricter new-node checks.
- Decide whether the rule needs a new activation mechanism or can be buried at a
  fixed height for a network. Activation state is represented in
  `Consensus::Params` and queried through deployment helpers.
- Check whether the rule is intended for all networks or only one of mainnet,
  testnet3, testnet4, signet, or regtest.

Primary references:

- Consensus constants: `src/consensus/consensus.h:12`
- Money range and the consensus-critical `MAX_MONEY`: `src/consensus/amount.h:11`
- Consensus parameters and deployments: `src/consensus/params.h:20`
- Per-network parameter definitions: `src/kernel/chainparams.cpp:80`
- Regtest activation overrides: `src/kernel/chainparams.cpp:535`
- Command-line activation test hooks: `src/chainparams.cpp:44`

## 2. Place The Rule In The Correct Validation Layer

Bitcoin Core validation is intentionally split into layers. A consensus rule in
the wrong layer can be skipped by reindex, assumevalid, snapshot validation, or
alternative block import paths.

Review these layers separately:

- Context-free transaction checks: `CheckTransaction()` validates transaction
  shape, total output range, duplicate inputs, coinbase scriptSig length, and
  null prevouts for non-coinbase transactions. See
  `src/consensus/tx_check.cpp:11`.
- Context-free block checks: `CheckBlock()` validates header proof, merkle root,
  basic block size, coinbase placement, transaction sanity, and legacy sigops.
  See `src/validation.cpp:3950`.
- Contextual header checks: `ContextualCheckBlockHeader()` validates difficulty,
  time, version-based buried deployments, BIP94 timewarp protection, and
  future-time limits. See `src/validation.cpp:4112`.
- Contextual block checks: `ContextualCheckBlock()` validates transaction
  finality, BIP34 coinbase height, witness commitment behavior, and block
  weight. See `src/validation.cpp:4161`.
- UTXO-dependent block connection: `Chainstate::ConnectBlock()` validates input
  availability, coinbase maturity, fees, sequence locks, script flags, sigops,
  coinbase reward, and UTXO updates. See `src/validation.cpp:2292`.
- Block disconnection: `DisconnectBlock()` must be able to undo the rule's UTXO
  effects exactly during reorgs and verification. See `src/validation.cpp:2176`.

Important caveat: `ContextualCheckBlock()` is not called by `ConnectBlock()`.
The code explicitly warns that `-reindex-chainstate` skips it. See
`src/validation.cpp:4155` and the related warning inside `ConnectBlock()` at
`src/validation.cpp:2304`. A rule that must hold during chainstate
reconstruction usually belongs in or below `ConnectBlock()`, or it needs a
carefully designed redownload/reindex strategy.

## 3. Respect Historical Validation Exceptions

Bitcoin Core contains historical exceptions that are themselves consensus
critical. A new rule must not accidentally invalidate historical mainnet blocks
or change the meaning of existing block index status.

Review:

- BIP30 duplicate transaction and coinbase-overwrite handling:
  `src/validation.cpp:2389`
- BIP30/BIP34 interaction comments and exceptions: `src/validation.cpp:2412`
- Script flag exceptions for historical blocks:
  `src/validation.cpp:2247`
- Mainnet script exceptions in chain parameters:
  `src/kernel/chainparams.cpp:85`
- Genesis block special-case in `ConnectBlock()`: `src/validation.cpp:2332`
- Merkle duplicate transaction mutation issue:
  `src/consensus/merkle.cpp:9`
- Block merkle root validation and mutation handling:
  `src/validation.cpp:3869`

Questions to answer:

- Are there historical blocks that would fail the new rule?
- If yes, is the rule activated after those blocks, or are explicit exceptions
  required?
- Are the exceptions limited, documented, and testable?
- Does the exception affect assumevalid, assumeutxo, reindex, or block import?

## 4. Define Activation Precisely

Consensus rules need deterministic activation. The codebase supports buried
deployments and versionbits deployments.

Review:

- Deployment enum and comments requiring deployment metadata updates:
  `src/consensus/params.h:20`
- `BIP9Deployment` fields, including `ALWAYS_ACTIVE` and `NEVER_ACTIVE`:
  `src/consensus/params.h:42`
- Deployment query helpers: `src/deploymentstatus.h:13`
- Versionbits constants: `src/versionbits.h:18`
- Versionbits cache API: `src/versionbits.h:74`
- BIP9 state machine overview: `src/versionbits_impl.h:12`
- Versionbit signaling check: `src/versionbits_impl.h:57`
- Versionbits state transition logic: `src/versionbits.cpp:26`
- Versionbits GBT status: `src/versionbits.cpp:228`
- Block version computation: `src/versionbits.cpp:259`
- Unknown versionbits warnings: `src/versionbits.cpp:295`
- Warnings when unknown rules activate: `src/validation.cpp:2878`

Per-network activation lives in chain parameters:

- Mainnet: `src/kernel/chainparams.cpp:80`
- Testnet3: `src/kernel/chainparams.cpp:205`
- Testnet4: `src/kernel/chainparams.cpp:308`
- Signet: `src/kernel/chainparams.cpp:415`
- Regtest: `src/kernel/chainparams.cpp:535`

Activation checklist:

- Choose buried height or versionbits. Buried heights are simple but not a live
  signaling mechanism.
- If using versionbits, choose bit, start time, timeout, threshold, period, and
  minimum activation height.
- Add deployment metadata used by RPC and mining interfaces in
  `src/deploymentinfo.cpp:11` and buried deployment names in
  `src/deploymentinfo.cpp:22`.
- Ensure regtest can force the rule active or inactive with
  `-testactivationheight` or `-vbparams`, where appropriate.
- Define behavior before activation, during `LOCKED_IN`, at the first active
  block, and after activation.
- Define how old nodes and miners detect or fail to detect the change.

## 5. Header, Work, Time, And Chain Selection

Header validity and chain selection are consensus-critical because they
determine which branch is eligible to become best chain.

Review:

- Block header fields and serialization: `src/primitives/block.h:26`
- Proof-of-work target calculation: `src/pow.cpp:14`
- Retarget calculation and clamps: `src/pow.cpp:50`
- Difficulty transition sanity checks: `src/pow.cpp:89`
- Proof-of-work validation: `src/pow.cpp:140`
- Contextual header checks for difficulty, time, and version:
  `src/validation.cpp:4112`
- Median time past calculation: `src/chain.h:231`
- Block status levels and invalidity flags: `src/chain.h:42`
- Chainwork stored in `CBlockIndex`: `src/chain.h:93`
- Work comparator for best-chain candidates:
  `src/node/blockstorage.cpp:174`
- Block index insertion and chainwork calculation:
  `src/node/blockstorage.cpp:224`
- Candidate-chain selection: `src/validation.cpp:3145`
- Best-chain activation loop: `src/validation.cpp:3354`
- Invalid block and invalid chain marking: `src/validation.cpp:1983`

Questions to answer:

- Does the rule change block hash, header serialization, target calculation, or
  chainwork?
- Does it change median-time-past semantics, future block time, or locktime
  interpretation?
- Does it interact with testnet special rules such as minimum difficulty or
  BIP94 timewarp protection?
- Can competing branches be compared deterministically by all nodes?
- Are invalid block caches and block index validity levels still sound?

## 6. Transaction Shape, Serialization, And Identifiers

Transaction serialization and identifiers are embedded in block validity,
merkle commitments, witness commitments, mempool behavior, compact blocks, and
wallet/RPC interfaces.

Review:

- Outpoints: `src/primitives/transaction.h:27`
- Transaction inputs and sequence constants: `src/primitives/transaction.h:61`
- Transaction outputs: `src/primitives/transaction.h:139`
- Transaction serialization and witness/no-witness modes:
  `src/primitives/transaction.h:176`
- Immutable transaction fields and cached hashes:
  `src/primitives/transaction.h:280`
- Txid vs wtxid calculation: `src/primitives/transaction.cpp:69`
- `GetValueOut()` money range checks: `src/primitives/transaction.cpp:98`
- Context-free transaction validation: `src/consensus/tx_check.cpp:11`
- Locktime finality: `src/consensus/tx_verify.cpp:17`
- Sequence lock calculation and evaluation: `src/consensus/tx_verify.cpp:39`
- Sigop accounting helpers: `src/consensus/tx_verify.cpp:112`

Questions to answer:

- Does the rule change what bytes are committed to by txid, wtxid, merkle root,
  or witness merkle root?
- Does it introduce new optional transaction data? If so, how do old nodes
  parse or reject it?
- Does it affect transaction weight, stripped size, or minimum serializable
  size?
- Does it preserve `MoneyRange()` checks and avoid integer overflow?
- Does it affect coinbase transaction structure or coinbase maturity?
- Does it require wallet or RPC changes to create, decode, or sign the new
  transaction form?

## 7. Monetary Invariants

Money creation and destruction are consensus-critical and are checked across
transaction validation, block connection, and UTXO accounting.

Review:

- `CAmount`, `COIN`, `MAX_MONEY`, and `MoneyRange()`:
  `src/consensus/amount.h:11`
- Coinbase maturity: `src/consensus/consensus.h:17`
- Input value and fee checks: `src/consensus/tx_verify.cpp:164`
- Subsidy schedule: `src/validation.cpp:1836`
- Per-transaction input checks in `ConnectBlock()`:
  `src/validation.cpp:2521`
- Coinbase reward limit: `src/validation.cpp:2607`
- UTXO creation and spending: `src/validation.cpp:1996`
- UTXO set statistics and total amount reporting:
  `src/kernel/coinstats.cpp:150`
- Fuzz target for total supply: `src/test/fuzz/utxo_total_supply.cpp:23`

Questions to answer:

- Can the rule create or destroy satoshis?
- Are fees still computed as total inputs minus total outputs?
- Are negative values, overflow, and values over `MAX_MONEY` impossible?
- Are coinbase rewards bounded by subsidy plus fees?
- Does the rule affect unspendable outputs and UTXO statistics?

## 8. Script, Sighash, And Signature Verification

Most soft forks are script changes. The script interpreter has consensus flags,
policy-only flags, signature hashing code, witness handling, Taproot logic, and
resource limits that must remain internally consistent.

Review:

- Script verify flags and the comment that all flags are intended soft forks:
  `src/script/interpreter.h:41`
- Full list of script verify flags: `src/script/interpreter.h:47`
- Precomputed transaction data for BIP143/BIP341:
  `src/script/interpreter.h:163`
- Signature versions: `src/script/interpreter.h:200`
- Signature checker APIs: `src/script/interpreter.h:272`
- Signature and pubkey encoding checks: `src/script/interpreter.cpp:201`
- Signature opcode behavior: `src/script/interpreter.cpp:321`
- Main opcode evaluation: `src/script/interpreter.cpp:407`
- CLTV/CSV script checks: `src/script/interpreter.cpp:524`
- Minimal-if handling: `src/script/interpreter.cpp:613`
- Tapscript-specific signature opcodes: `src/script/interpreter.cpp:1086`
- Precomputed tx data initialization: `src/script/interpreter.cpp:1407`
- Taproot/Schnorr sighash: `src/script/interpreter.cpp:1487`
- Legacy and witness v0 sighash: `src/script/interpreter.cpp:1604`
- ECDSA/Schnorr signature verification: `src/script/interpreter.cpp:1696`
- `CheckLockTime()` and `CheckSequence()`: `src/script/interpreter.cpp:1749`
- Tapscript execution and OP_SUCCESS handling:
  `src/script/interpreter.cpp:1836`
- Witness program validation: `src/script/interpreter.cpp:1921`
- Top-level `VerifyScript()`: `src/script/interpreter.cpp:2006`
- Witness sigop counting: `src/script/interpreter.cpp:2143`
- Script size, stack size, locktime threshold, and tapscript constants:
  `src/script/script.h:40`
- Unspendable script detection: `src/script/script.h:560`
- OP_SUCCESS and minimal push helpers: `src/script/script.cpp:365`
- Block script flag selection: `src/validation.cpp:2247`
- Script cache and consensus script checks: `src/validation.cpp:2058`

Script-change checklist:

- Decide whether the rule is a new flag, a modification to an existing flag, or
  a new witness/script version.
- Ensure inactive paths preserve old behavior exactly.
- Preserve forward-compatibility hooks such as unknown witness versions,
  upgradeable public key types, discouraged NOPs, and OP_SUCCESS behavior.
- Define resource limits: script size, stack size, element size, sigops, weight,
  and validation cost.
- Check sighash commitments: inputs, outputs, amounts, scriptPubKeys,
  sequences, annex, code separator position, leaf hash, and hash type behavior.
- Ensure the script cache key includes everything relevant. Current script cache
  behavior uses the transaction witness hash and script flags in
  `CheckInputScripts()`.
- Keep policy-only discouragement flags separate from mandatory consensus flags
  unless the rule is intentionally activated as consensus.

## 9. Block Structure, Merkle Roots, Weight, Sigops, And Commitments

Rules that alter block structure must maintain deterministic commitments and
resource accounting.

Review:

- Block size, weight, sigops, witness scale constants:
  `src/consensus/consensus.h:12`
- Block weight calculation: `src/consensus/validation.h:128`
- Witness commitment locator: `src/consensus/validation.h:146`
- Block serialization and cached validation flags:
  `src/primitives/block.h:73`
- Merkle root construction: `src/consensus/merkle.cpp:66`
- Witness merkle root construction: `src/consensus/merkle.cpp:76`
- Merkle root validation: `src/validation.cpp:3869`
- Witness malleation and commitment checks: `src/validation.cpp:3902`
- Context-free block validation: `src/validation.cpp:3950`
- Contextual block weight and witness checks: `src/validation.cpp:4161`
- Coinbase witness commitment generation:
  `src/validation.cpp:4017`
- Legacy/P2SH/witness sigop cost calculation:
  `src/consensus/tx_verify.cpp:112`
- Sigop limit enforcement in `ConnectBlock()`:
  `src/validation.cpp:2521`

Questions to answer:

- Does the rule change the merkle tree, witness tree, or coinbase commitment?
- Does it change block weight, serialized size, stripped size, or sigop cost?
- Can mutated blocks, duplicate txids, or witness malleation bypass early relay
  checks or final validation?
- Is the coinbase first and unique rule preserved?
- Does the rule require miners to add new commitments or reserve coinbase
  fields?

## 10. UTXO Set, Undo Data, Reorgs, And Snapshots

Any rule that changes spendability or created outputs changes the UTXO set.
It must also be reversible during reorgs and compatible with snapshot
validation.

Review:

- `Coin` serialization and UTXO metadata: `src/coins.h:26`
- Dirty/fresh cache flags and consensus failure warning:
  `src/coins.h:94`
- Coins view abstraction: `src/coins.h:305`
- Coins cache APIs: `src/coins.h:365`
- Adding coins and BIP30 overwrite handling: `src/coins.cpp:89`
- Spending coins: `src/coins.cpp:153`
- Cache batch write behavior: `src/coins.cpp:208`
- Input availability checks: `src/coins.cpp:329`
- Undo data structures: `src/undo.h:15`
- Updating coins while connecting a block: `src/validation.cpp:1996`
- Disconnecting a block: `src/validation.cpp:2176`
- Connect-block UTXO and undo writes: `src/validation.cpp:2292`
- Database verification by disconnecting/reconnecting blocks:
  `src/validation.cpp:4644`
- Roll-forward and replay after inconsistent state:
  `src/validation.cpp:4783`
- Assumeutxo metadata: `src/kernel/chainparams.h:29`
- UTXO hash warning for assumeutxo compatibility:
  `src/kernel/coinstats.cpp:75`
- Snapshot activation: `src/validation.cpp:5605`
- Snapshot population and hash validation: `src/validation.cpp:5771`
- Background snapshot validation: `src/validation.cpp:5985`

Questions to answer:

- Does the rule alter which outputs are added to the UTXO set?
- Does it alter which outputs are spendable, unspendable, mature, or
  overwritten?
- Can undo data reconstruct the exact prior `Coin` state?
- Does a reorg across the activation boundary behave deterministically?
- Are assumeutxo snapshot hashes invalidated? If yes, chainparams snapshot data
  and documentation need to be updated.
- Are coinstats and indexes still consistent with the consensus UTXO set?

## 11. Mempool, Policy, And Package Acceptance

Mempool rules do not define consensus, but consensus changes usually need
mempool and policy changes so nodes do not relay or mine transactions that will
fail in blocks, and so valid post-activation transactions can propagate.

Review:

- Policy constants and standardness limits: `src/policy/policy.h:23`
- Mandatory vs standard script flags:
  `src/policy/policy.h:96`
- Policy-only file comment: `src/policy/policy.cpp:6`
- Output and transaction standardness: `src/policy/policy.cpp:79`
- Policy sigops checks: `src/policy/policy.cpp:166`
- Input standardness: `src/policy/policy.cpp:213`
- Witness standardness: `src/policy/policy.cpp:251`
- Mempool prechecks: `src/validation.cpp:782`
- Policy script checks: `src/validation.cpp:1132`
- Consensus script checks before mempool acceptance:
  `src/validation.cpp:1155`
- Package acceptance and script-cache note:
  `src/validation.cpp:1249`

Questions to answer:

- Can the mempool accept a transaction that will fail under current or next
  block consensus flags?
- Are pre-activation and post-activation mempool behaviors defined?
- Should policy become stricter before activation to reduce miner risk?
- Does the rule interact with package acceptance, ancestor/descendant limits,
  replacement, fee calculation, or standardness?
- Are standardness changes intentionally non-consensus?

## 12. Mining, Block Templates, And RPC Exposure

Miners need templates that satisfy the new rule, and RPC clients need accurate
deployment and rule information.

Review:

- Block assembler options: `src/node/miner.h:60`
- Block assembler weight and sigop clamps: `src/node/miner.cpp:79`
- `CreateNewBlock()` versionbits, coinbase, witness commitment, and final
  `TestBlockValidity()` call: `src/node/miner.cpp:122`
- Transaction selection limits and finality checks:
  `src/node/miner.cpp:239`
- Mempool package selection for templates: `src/node/miner.cpp:279`
- Recomputing merkle root and cached block checks:
  `src/node/miner.cpp:336`
- Template refresh/wait behavior: `src/node/miner.cpp:361`
- `getblocktemplate` rule fields and docs: `src/rpc/mining.cpp:620`
- GBT proposal validation through `TestBlockValidity()`:
  `src/rpc/mining.cpp:729`
- Required signet/segwit rules: `src/rpc/mining.cpp:848`
- GBT active rules and versionbits status: `src/rpc/mining.cpp:946`
- GBT limits: `src/rpc/mining.cpp:997`
- Signet and witness commitment fields in GBT:
  `src/rpc/mining.cpp:1014`
- Deployment info RPC metadata: `src/deploymentinfo.cpp:11`

Questions to answer:

- Will miners produce valid blocks immediately after activation?
- Does the coinbase need a new commitment, witness reserved value, or height
  encoding change?
- Does `getblocktemplate` expose the new rule in `rules`, `vbavailable`, or
  `vbrequired`?
- Does proposal mode reject invalid candidate blocks with useful errors?
- Are mining tests able to exercise pre-activation, locked-in, and active
  states?

## 13. P2P Relay, Compact Blocks, And Block Download

P2P code is not the source of final consensus truth, but it affects safe relay,
DoS exposure, peer punishment, and whether nodes can fetch and reconstruct
blocks around activation.

Review:

- Witness service bit: `src/protocol.h:318`
- Service bit reservation guidance: `src/protocol.h:332`
- Witness inventory types: `src/protocol.h:470`
- Protocol string handling for witness flags: `src/protocol.cpp:61`
- Compact block witness/wtxid handling: `src/blockencodings.h:19`
- Compact block short IDs from witness hash:
  `src/blockencodings.cpp:20`
- Compact block reconstruction and weight assumptions:
  `src/blockencodings.cpp:59`
- Compact block mutation check during fill:
  `src/blockencodings.cpp:191`
- WTXID relay handshake: `src/net_processing.cpp:3715`
- GETDATA transaction relay with witness: `src/net_processing.cpp:4216`
- Header processing and minimum chain work: `src/net_processing.cpp:4394`
- Incoming transaction relay and witness/wtxid behavior:
  `src/net_processing.cpp:4473`
- Compact block header handling: `src/net_processing.cpp:4554`
- Compact block fill using segwit activation state:
  `src/net_processing.cpp:4736`
- Normal block message deserialization with witness and mutation check:
  `src/net_processing.cpp:4866`

Questions to answer:

- Does the rule need a new service bit, inventory type, relay message, or peer
  negotiation?
- Can old peers relay enough data for new nodes to validate?
- Are compact blocks still reconstructable and mutation-resistant?
- Does early peer punishment match final validation behavior?
- Does the rule change minimum-chain-work assumptions, header sync, or block
  download anti-DoS thresholds?

## 14. Block Import, Reindex, Assumevalid, And Validation Caches

Consensus rules must be enforced consistently when blocks arrive from peers,
disk, snapshots, and reindex. Caches must not cause stale validation results
across activation boundaries or flag changes.

Review:

- New-block processing: `src/validation.cpp:4430`
- Block acceptance and contextual checks: `src/validation.cpp:4330`
- Header acceptance: `src/validation.cpp:4218`
- Testing a block against the current tip: `src/validation.cpp:4495`
- Assumevalid script-skipping decision: `src/validation.cpp:2342`
- Script cache use in input checks: `src/validation.cpp:2058`
- Block validation levels in `BlockStatus`: `src/chain.h:42`
- Raising block validity level: `src/chain.h:260`
- Verification by disconnect/reconnect: `src/validation.cpp:4644`
- External block load/reindex fuzz target:
  `src/test/fuzz/load_external_block_file.cpp:28`
- Kernel reindex tests: `src/test/kernel/test_kernel.cpp:791`

Questions to answer:

- Is the rule enforced during `-reindex` and `-reindex-chainstate`?
- Does assumevalid skip any validation the new rule requires?
- Does the script cache key include activation-relevant flags?
- Does cached `CBlock::fChecked` remain safe if the rule is contextual?
- Are blocks imported from disk validated the same way as blocks from peers?
- Are invalidity flags and failure caches invalidated or versioned if needed?

## 15. Indexes, RPC, Wallet, And External Interfaces

These are usually not consensus-defining, but consensus changes often break
assumptions in interfaces that inspect blocks, transactions, and UTXOs.

Review index surfaces:

- Block filter index construction from block and undo data:
  `src/index/blockfilterindex.cpp:251`
- Block filter reorg removal behavior:
  `src/index/blockfilterindex.cpp:277`
- Coinstats index append/remove behavior:
  `src/index/coinstatsindex.cpp:109`
- Coinstats index lookup: `src/index/coinstatsindex.cpp:237`
- Block filter RPC/REST endpoints: `src/rpc/blockchain.cpp:2934` and
  `src/rest.cpp:621`
- `gettxoutsetinfo` and UTXO hash selection:
  `src/rpc/blockchain.cpp:1015`
- Snapshot dump/load RPCs: `src/rpc/blockchain.cpp:3048` and
  `src/rpc/blockchain.cpp:3347`

Review wallet and signing surfaces when the rule affects spend construction:

- Transaction signing examples and RPCs are outside the validation core, but
  changes to script versions, sighash, address encoding, or standardness usually
  require wallet, descriptor, PSBT, and raw transaction RPC review.
- Functional test framework address/script helpers may need updates for new
  script types: `test/functional/test_framework/address.py:8`,
  `test/functional/test_framework/script.py:703`, and
  `test/functional/test_framework/script.py:916`.

Questions to answer:

- Can RPCs decode and report the new transaction or script form?
- Can wallets avoid creating invalid spends across activation boundaries?
- Do block filters still include all data clients expect?
- Do indexes handle reorgs involving the new rule?
- Are external clients told about activation status accurately?

## 16. Kernel API, Notifications, And Operational Parameters

Bitcoin Core also exposes validation behavior through interfaces used by
external applications and by internal components. These interfaces should not
define consensus independently, but they may need updates when validation
semantics, warnings, or hardcoded chain data change.

Review:

- Kernel validation callback types: `src/kernel/bitcoinkernel.h:345`
- Kernel validation interface implementation:
  `src/kernel/bitcoinkernel.cpp:327`
- Kernel script verification entry point:
  `src/kernel/bitcoinkernel.cpp:650`
- C++ kernel wrapper validation interface:
  `src/kernel/bitcoinkernel_wrapper.h:945`
- Node interface validation notifications:
  `src/interfaces/chain.h:311`
- Node-side notification proxy: `src/node/interfaces.cpp:453`
- Validation signals sent after connect/disconnect/tip update:
  `src/validation.cpp:3413`
- Example kernel chainstate executable:
  `src/bitcoin-chainstate.cpp:53`

Hardcoded operational parameters to revisit after a deployment is finalized:

- `AssumeutxoData`: `src/kernel/chainparams.h:34`
- `ChainTxData`: `src/kernel/chainparams.h:57`
- Headers sync parameters: `src/kernel/chainparams.h:64`
- Mainnet minimum chain work and default assumevalid:
  `src/kernel/chainparams.cpp:117`
- Mainnet assumeutxo data: `src/kernel/chainparams.cpp:166`
- Mainnet chain transaction data: `src/kernel/chainparams.cpp:187`
- Mainnet headers sync data: `src/kernel/chainparams.cpp:194`
- Minimum-chain-work defaulting: `src/validation.cpp:6148`
- Minimum-chain-work block acceptance behavior: `src/validation.cpp:4312`
- Verification progress from chain transaction data:
  `src/validation.cpp:5523`
- Low-work headers sync using chainparams headers sync data:
  `src/net_processing.cpp:2793`

Questions to answer:

- Do kernel consumers observe the new warning, activation, script verification,
  or block connection behavior correctly?
- Are validation callbacks still emitted in the same order and with enough data?
- Does a changed script rule require a public script verification flag or API
  behavior change?
- Do minimum chain work, default assumevalid, chain transaction data, headers
  sync data, or assumeutxo data need post-deployment updates?
- Are these operational parameters clearly separated from the consensus rule
  itself?

## 17. Tests To Plan Before Touching Consensus

A consensus change needs tests at multiple levels because each validation layer
can hide a different bug class.

Unit and validation tests to study:

- Block validation tests: `src/test/validation_block_tests.cpp:35`
- Transaction validation tests: `src/test/txvalidation_tests.cpp:21`
- Script tests: `src/test/script_tests.cpp:429`
- P2SH script tests: `src/test/script_p2sh_tests.cpp:58`
- Segwit script tests: `src/test/script_segwit_tests.cpp:10`
- Sighash tests: `src/test/sighash_tests.cpp:119`
- Script number tests: `src/test/scriptnum_tests.cpp:13`
- Proof-of-work tests: `src/test/pow_tests.cpp:14`
- Versionbits tests: `src/test/versionbits_tests.cpp:185`
- Merkle tests: `src/test/merkle_tests.cpp:11`
- Transaction primitive tests: `src/test/transaction_tests.cpp:159`
- Coins/UTXO cache tests: `src/test/coins_tests.cpp:304`
- Chainstate tests: `src/test/validation_chainstate_tests.cpp:25`
- Chainstate manager tests:
  `src/test/validation_chainstatemanager_tests.cpp:35`
- Tx validation cache tests: `src/test/txvalidationcache_tests.cpp:29`
- Block filter index tests: `src/test/blockfilter_index_tests.cpp:25`
- Testnet4 miner tests: `src/test/testnet4_miner_tests.cpp:30`

Functional tests to study:

- Taproot activation and spending: `test/functional/feature_taproot.py:1407`
- Segwit behavior: `test/functional/feature_segwit.py:73`
- P2P segwit relay: `test/functional/p2p_segwit.py:211`
- CSV/BIP68/BIP112/BIP113 activation:
  `test/functional/feature_csv_activation.py:94`
- BIP68 sequence locks: `test/functional/feature_bip68_sequence.py:47`
- CLTV/BIP65: `test/functional/feature_cltv.py:82`
- NULLDUMMY: `test/functional/feature_nulldummy.py:50`
- Versionbits warnings: `test/functional/feature_versionbits_warning.py:27`
- Pre-segwit node upgrade behavior:
  `test/functional/feature_presegwit_node_upgrade.py:15`
- Minimum chain work: `test/functional/feature_minchainwork.py:31`
- Assumevalid: `test/functional/feature_assumevalid.py:69`
- Assumeutxo: `test/functional/feature_assumeutxo.py:62`
- Reindex: `test/functional/feature_reindex.py:21`
- Mining template verification:
  `test/functional/mining_template_verification.py:55`
- Basic mining: `test/functional/mining_basic.py:60`
- Invalid blocks: `test/functional/p2p_invalid_block.py:34`
- Invalid transactions: `test/functional/p2p_invalid_tx.py:24`
- Mutated blocks: `test/functional/p2p_mutated_blocks.py:30`
- Compact blocks: `test/functional/p2p_compactblocks.py:144`

Fuzz targets to study:

- Block header: `src/test/fuzz/block_header.cpp:17`
- Block: `src/test/fuzz/block.cpp:25`
- Transaction: `src/test/fuzz/transaction.cpp:30`
- Primitive transaction serialization:
  `src/test/fuzz/primitives_transaction.cpp:15`
- Proof of work: `src/test/fuzz/pow.cpp:26`
- Difficulty transition: `src/test/fuzz/pow.cpp:90`
- Script: `src/test/fuzz/script.cpp:39`
- Script flags: `src/test/fuzz/script_flags.cpp:27`
- Script interpreter: `src/test/fuzz/script_interpreter.cpp:19`
- Sighash cache: `src/test/fuzz/script_interpreter.cpp:51`
- Eval script: `src/test/fuzz/eval_script.cpp:12`
- Signature checker: `src/test/fuzz/signature_checker.cpp:51`
- Versionbits: `src/test/fuzz/versionbits.cpp:90`
- Merkle: `src/test/fuzz/merkle.cpp:36`
- Coins view: `src/test/fuzz/coins_view.cpp:373`
- Coins cache simulation: `src/test/fuzz/coinscache_sim.cpp:192`
- UTXO snapshot: `src/test/fuzz/utxo_snapshot.cpp:228`
- UTXO total supply: `src/test/fuzz/utxo_total_supply.cpp:23`
- Process message: `src/test/fuzz/process_message.cpp:68`
- Process messages: `src/test/fuzz/process_messages.cpp:58`
- Partially downloaded block:
  `src/test/fuzz/partially_downloaded_block.cpp:46`
- External block file loading:
  `src/test/fuzz/load_external_block_file.cpp:28`

Test cases to enumerate:

- Invalid before activation, valid after activation, and valid before but
  invalid after activation where applicable.
- Boundary block: one block before activation, activation block, one block
  after activation.
- Reorg from active to inactive branch and from inactive to active branch.
- Reindex and reindex-chainstate across the activation height.
- Assumevalid on and off.
- Assumeutxo snapshot load and background validation.
- Kernel API script verification and validation callbacks when public behavior
  changes.
- Mempool acceptance before and after activation.
- Miner template creation before and after activation.
- GBT proposal mode with valid and invalid candidate blocks.
- Compact block relay and reconstruction.
- Old peer or old-node compatibility scenario when relevant.
- Serialization round trips and malformed encodings.
- Resource-limit edge cases: max block weight, sigops, script size, stack size,
  element size, witness size, and maximum money.

## 18. Review Questions For Any Proposed Rule

Before implementation, write down exact answers to these questions:

- What precise condition makes a block or transaction invalid?
- Is the condition context-free, height-dependent, previous-block-dependent, or
  UTXO-dependent?
- Which function is the final enforcement point?
- Which existing checks must remain unchanged?
- Which historical blocks, if any, are exceptions?
- How is activation computed on each network?
- How does regtest force the rule active or inactive?
- What happens to mempool transactions across activation?
- What changes must miners make to produce valid blocks?
- What data must old peers still be able to relay?
- What block, transaction, witness, or UTXO commitments change?
- Does the rule alter txid, wtxid, merkle root, witness merkle root, block hash,
  or UTXO snapshot hash?
- Does the rule alter chainwork, target calculation, or best-chain selection?
- Does the rule alter script cache keys or validation flags?
- Does the rule alter undo data or reorg behavior?
- Does assumevalid skip anything that must now be checked?
- Does assumeutxo need new hardcoded snapshot metadata?
- Do kernel APIs, warnings, and validation callbacks need updates?
- Which unit, functional, fuzz, and interoperability tests prove the rule?

## 19. Common Failure Modes

- Adding a consensus rule only to mempool policy. Blocks can still include
  transactions that bypass mempool.
- Adding a contextual rule only to `ContextualCheckBlock()` and missing
  `ConnectBlock()` or reindex-chainstate behavior.
- Forgetting historical exceptions and invalidating old mainnet blocks.
- Making miners enforce a rule that full validation does not enforce, or the
  opposite.
- Treating standardness flags as consensus flags.
- Changing transaction serialization without tracking txid/wtxid, compact block,
  witness commitment, RPC, and wallet effects.
- Changing UTXO semantics without updating undo, reorg, coinstats, assumeutxo,
  and indexes.
- Reusing a script cache result across different consensus flags.
- Forgetting that old nodes may accept soft-fork blocks but will not enforce the
  new restrictions.
- Forgetting regtest/testnet/signet parameter behavior and test hooks.
- Treating hardcoded operational data such as assumevalid or minimum chain work
  as if it were the activation mechanism.

## 20. Minimal Code Reading Path

For a first pass through the codebase, read in this order:

1. `src/consensus/params.h:20` and `src/kernel/chainparams.cpp:80` for the
   consensus parameter model.
2. `src/validation.cpp:3950`, `src/validation.cpp:4161`, and
   `src/validation.cpp:2292` for block validation layers.
3. `src/consensus/tx_check.cpp:11` and `src/consensus/tx_verify.cpp:164` for
   transaction validation.
4. `src/validation.cpp:2247` and `src/script/interpreter.cpp:2006` for script
   flags and script execution.
5. `src/coins.h:94`, `src/coins.cpp:89`, `src/validation.cpp:2176`, and
   `src/undo.h:15` for UTXO and undo behavior.
6. `src/versionbits.cpp:26`, `src/deploymentstatus.h:13`, and
   `src/deploymentinfo.cpp:11` for activation.
7. `src/node/miner.cpp:122`, `src/rpc/mining.cpp:946`, and
   `src/validation.cpp:4495` for mining and proposal validation.
8. `src/net_processing.cpp:4866` and `src/blockencodings.cpp:191` for relay and
   compact block interactions.
9. `src/validation.cpp:4644`, `src/validation.cpp:5605`, and
   `src/kernel/coinstats.cpp:75` for reindex, verification, and assumeutxo.
10. `src/kernel/bitcoinkernel.cpp:650` and
    `src/kernel/chainparams.cpp:117` for public kernel behavior and hardcoded
    operational chain data.

The central design rule is: final consensus enforcement must live on the path
that connects blocks into a chainstate, while surrounding systems must be made
consistent enough to relay, mine, test, index, and recover from those blocks
without creating a second interpretation of validity.
