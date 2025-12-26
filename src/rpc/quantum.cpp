// Copyright (c) 2025 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <core_io.h>
#include <key_io.h>
#include <primitives/transaction.h>
#include <pubkey.h>
#include <quantum_commit.h>
#include <quantum_commitdb.h>
#include <rpc/server.h>
#include <rpc/util.h>
#include <univalue.h>
#include <util/strencodings.h>

#include <optional>

static RPCHelpMan createquantumcommitment()
{
    return RPCHelpMan{"createquantumcommitment",
        "\nCreate a quantum commitment for a transaction.\n"
        "This generates the (AID, SDP, CTXID) triple that should be committed in an OP_RETURN output before broadcasting the transaction.\n"
        "The commitment protects against quantum computer attacks by proving knowledge of the public key in advance.\n",
        {
            {"hexstring", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "The transaction hex string"},
            {"pubkey", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "The public key (hex) that will sign the first input"},
        },
        RPCResult{
            RPCResult::Type::OBJ, "", "",
            {
                {RPCResult::Type::STR_HEX, "aid", "Address ID: h(pubkey) - 16 bytes"},
                {RPCResult::Type::STR_HEX, "sdp", "Sequence Dependent Proof: h(pubkey, txid) - 16 bytes"},
                {RPCResult::Type::STR_HEX, "ctxid", "Commitment TXID (truncated) - 16 bytes"},
                {RPCResult::Type::STR_HEX, "commitment", "Full commitment (48 bytes) for OP_RETURN"},
                {RPCResult::Type::STR_HEX, "commitment_script", "Complete OP_RETURN scriptPubKey"},
            }
        },
        RPCExamples{
            HelpExampleCli("createquantumcommitment", "\"0200000001...\" \"02abc123...\"")
            + HelpExampleRpc("createquantumcommitment", "\"0200000001...\", \"02abc123...\"")
        },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
        {
            // Parse transaction
            CMutableTransaction mtx;
            if (!DecodeHexTx(mtx, request.params[0].get_str())) {
                throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "TX decode failed");
            }
            CTransaction tx(mtx);

            // Parse public key
            std::vector<unsigned char> pubkey_data = ParseHex(request.params[1].get_str());
            CPubKey pubkey(pubkey_data.begin(), pubkey_data.end());
            if (!pubkey.IsFullyValid()) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid public key");
            }

            // Create commitment
            QuantumCommitment commitment = CreateQuantumCommitment(tx, pubkey);

            // Create OP_RETURN script
            CScript commitment_script = CreateQuantumCommitmentScript(commitment);

            // Build result
            UniValue result(UniValue::VOBJ);
            result.pushKV("aid", HexStr(commitment.aid));
            result.pushKV("sdp", HexStr(commitment.sdp));
            result.pushKV("ctxid", HexStr(commitment.ctxid));
            result.pushKV("commitment", HexStr(commitment.Serialize()));
            result.pushKV("commitment_script", HexStr(commitment_script));

            return result;
        },
    };
}

static RPCHelpMan verifyquantumcommitment()
{
    return RPCHelpMan{"verifyquantumcommitment",
        "\nVerify that a transaction matches a quantum commitment.\n",
        {
            {"hexstring", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "The transaction hex string"},
            {"commitment", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "The commitment hex (48 bytes)"},
            {"pubkey", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "The public key (hex)"},
        },
        RPCResult{
            RPCResult::Type::BOOL, "", "Whether the transaction matches the commitment"
        },
        RPCExamples{
            HelpExampleCli("verifyquantumcommitment", "\"0200000001...\" \"abc123...\" \"02def456...\"")
            + HelpExampleRpc("verifyquantumcommitment", "\"0200000001...\", \"abc123...\", \"02def456...\"")
        },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
        {
            // Parse transaction
            CMutableTransaction mtx;
            if (!DecodeHexTx(mtx, request.params[0].get_str())) {
                throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "TX decode failed");
            }
            CTransaction tx(mtx);

            // Parse commitment
            std::vector<unsigned char> commitment_data = ParseHex(request.params[1].get_str());
            auto commitment_opt = QuantumCommitment::Deserialize(commitment_data);
            if (!commitment_opt) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid commitment");
            }

            // Parse public key
            std::vector<unsigned char> pubkey_data = ParseHex(request.params[2].get_str());
            CPubKey pubkey(pubkey_data.begin(), pubkey_data.end());
            if (!pubkey.IsFullyValid()) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid public key");
            }

            // Verify
            bool valid = VerifyQuantumCommitment(tx, commitment_opt.value(), pubkey);

            return UniValue(valid);
        },
    };
}

static RPCHelpMan getquantumcommitmentstatus()
{
    return RPCHelpMan{"getquantumcommitmentstatus",
        "\nGet the status of the quantum commitment system.\n",
        {},
        RPCResult{
            RPCResult::Type::OBJ, "", "",
            {
                {RPCResult::Type::BOOL, "poqc_activated", "Whether a Proof of Quantum Computer has been detected"},
                {RPCResult::Type::NUM, "activation_height", "Block height where PoQC was activated (-1 if not activated)"},
                {RPCResult::Type::BOOL, "commitments_required", "Whether quantum commitments are currently required for transactions"},
                {RPCResult::Type::NUM, "database_size", "Estimated size of commitment database in bytes"},
            }
        },
        RPCExamples{
            HelpExampleCli("getquantumcommitmentstatus", "")
            + HelpExampleRpc("getquantumcommitmentstatus", "")
        },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
        {
            UniValue result(UniValue::VOBJ);

            if (!g_quantum_commitdb) {
                result.pushKV("poqc_activated", false);
                result.pushKV("activation_height", -1);
                result.pushKV("commitments_required", false);
                result.pushKV("database_size", 0);
                return result;
            }

            int activation_height = g_quantum_commitdb->GetPoQCActivationHeight();
            bool activated = (activation_height >= 0);

            result.pushKV("poqc_activated", activated);
            result.pushKV("activation_height", activation_height);
            result.pushKV("commitments_required", activated);
            result.pushKV("database_size", (int64_t)g_quantum_commitdb->DynamicMemoryUsage());

            return result;
        },
    };
}

static RPCHelpMan createcommitmentscript()
{
    return RPCHelpMan{"createcommitmentscript",
        "\nCreate an OP_RETURN script containing a quantum commitment.\n"
        "This is a convenience function to create a commitment output that can be added to any transaction.\n",
        {
            {"commitment", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "The commitment hex (48 bytes)"},
        },
        RPCResult{
            RPCResult::Type::OBJ, "", "",
            {
                {RPCResult::Type::STR_HEX, "scriptPubKey", "The OP_RETURN scriptPubKey hex"},
                {RPCResult::Type::STR, "asm", "Human-readable script assembly"},
            }
        },
        RPCExamples{
            HelpExampleCli("createcommitmentscript", "\"abc123...\"")
            + HelpExampleRpc("createcommitmentscript", "\"abc123...\"")
        },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
        {
            // Parse commitment
            std::vector<unsigned char> commitment_data = ParseHex(request.params[0].get_str());
            auto commitment_opt = QuantumCommitment::Deserialize(commitment_data);
            if (!commitment_opt) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid commitment (must be 48 bytes)");
            }

            // Create script
            CScript script = CreateQuantumCommitmentScript(commitment_opt.value());

            UniValue result(UniValue::VOBJ);
            result.pushKV("scriptPubKey", HexStr(script));
            result.pushKV("asm", ScriptToAsmStr(script));

            return result;
        },
    };
}

void RegisterQuantumRPCCommands(CRPCTable& t)
{
    static const CRPCCommand commands[]{
        {"quantum", &createquantumcommitment},
        {"quantum", &verifyquantumcommitment},
        {"quantum", &getquantumcommitmentstatus},
        {"quantum", &createcommitmentscript},
    };
    for (const auto& c : commands) {
        t.appendCommand(c.name, &c);
    }
}
