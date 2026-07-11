
### Rules for AI agents:
* Do not write code. This is a learning exercise and you're job is to help the developer to understand the codebase.

## Local setup
* `ln -s "$PWD/build/bin/*" "$HOME/.local/bin/"`

### Config
```~/.bitcoin/bitcoin.conf
regtest=1

[regtest]
rpcport=8332
rpcbind=0.0.0.0
whitelist=127.0.0.1
rpcpassword=elmeri
rpcuser=elmeri
```

### Build
* `rm -rf build`
* `cmake -B build -DWITH_ZMQ=ON -DCMAKE_BUILD_TYPE=Debug`
* `cmake --build build 2> build/error.log`

### d commands:
* `bitcoind`

### cli commands:
* https://developer.bitcoin.org/examples/transactions.html
* `bitcoin-cli createwallet "testwallet"` or `bitcoin-cli loadwallet "testwallet"`
* `bitcoin-cli generatetoaddress 101 $(bitcoin-cli getnewaddress)`
* `bitcoin-cli getbalance`
* `bitcoin-cli sendtoaddress bcrt1q7vjvrf5zuux4r50e49k4vlelys6zvnsrlqsh89 10.0 "comment" "comment to" false true null "unset" false 1 true`
* `bitcoin-cli listunspent 0`
* Manually great TX based on listunspent
```bash
UTXO_TXID=5cafe8ca33f250cb14476d9d5887e7609d04406912883399672c4d9763c04394
UTXO_VOUT=0
NEW_ADDRESS=$(bitcoin-cli getnewaddress)
RAW_TX=$(bitcoin-cli -regtest createrawtransaction '''
    [
      {
        "txid": "'$UTXO_TXID'",
        "vout": '$UTXO_VOUT'
      }
    ]
    ''' '''
    {
      "'$NEW_ADDRESS'": 49.9999
    }''')
```

* `bitcoin-cli decoderawtransaction $RAW_TX`
* `bitcoin-cli signrawtransactionwithwallet $RAW_TX`
* Save signed TX hex to `SIGNED_RAW_TX`
* `bitcoin-cli sendrawtransaction $SIGNED_RAW_TX`
* `bitcoin-cli -generate 1`
