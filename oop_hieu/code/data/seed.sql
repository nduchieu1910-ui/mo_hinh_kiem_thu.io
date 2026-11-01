-- =====================================================================
-- Dữ liệu mẫu: kịch bản Cross-Chain Token Bridge (Lock -> Mint)
-- ChainA (EVM, account model)  ->  ChainB (UTXO model)
-- =====================================================================
PRAGMA foreign_keys = ON;

INSERT INTO chains (name, chain_type, ledger_model, adapter_class, rpc_url, native_symbol, decimals, block_time_ms) VALUES
 ('ChainA-EVM',  'EVM',  'ACCOUNT', 'EVMAdapter',       'http://127.0.0.1:8545', 'ETH', 18, 1000),
 ('ChainB-UTXO', 'UTXO', 'UTXO',    'MockChainAdapter', NULL,                    'BTC', 8,  2000);

INSERT INTO blocks (chain_id, height, block_hash, parent_hash) VALUES
 (1, 0, '0xA_genesis', NULL),
 (2, 0, 'B_genesis',   NULL);

-- Tài khoản trên ChainA
INSERT INTO accounts (chain_id, address, label, balance, nonce, is_contract) VALUES
 (1, '0xA11CE', 'Alice',          '10000000000000000000', 0, 0),  -- 10 ETH
 (1, '0xB0B',   'Bob',            '5000000000000000000',  0, 0),
 (1, '0xBR1D6E','BridgeContract', '0',                    0, 1),
 (1, '0x70KEN', 'TokenA',         '0',                    0, 1);

INSERT INTO contracts (account_id, name, contract_type, bytecode_hash) VALUES
 (3, 'LockBridge', 'BRIDGE', '0xhash_bridge'),
 (4, 'TokenA',     'TOKEN',  '0xhash_token');

INSERT INTO contract_storage (contract_id, slot_key, slot_value) VALUES
 (2, 'totalSupply',         '1000'),
 (2, 'balances[0xA11CE]',   '1000'),
 (1, 'lockedTotal',         '0'),
 (1, 'outboundNonce',       '0');

-- Coinbase tx tạo UTXO ban đầu cho bridge vault trên ChainB
INSERT INTO transactions (chain_id, block_id, tx_hash, tx_format, status, executed_at) VALUES
 (2, 2, 'B_coinbase_0', 'UTXO', 'SUCCESS', datetime('now'));

INSERT INTO utxos (chain_id, created_by_tx, output_index, owner_address, amount) VALUES
 (2, 1, 0, 'bridge_vault', '100000');

-- Test case
INSERT INTO test_cases (name, description, scenario_type, source_chain_id, target_chain_id, arrange_json, act_json, expected_json) VALUES
 ('TC01_lock_and_mint',
  'Alice khoá 100 TokenA trên ChainA, relayer phát hành 100 đơn vị cho Alice trên ChainB',
  'TOKEN_BRIDGE', 1, 2,
  '{"ChainA":{"TokenA.balances[0xA11CE]":"1000"},"ChainB":{"bridge_vault":"100000"}}',
  '[{"chain":"ChainA","call":"LockBridge.lock","args":{"token":"0x70KEN","amount":"100","receiver":"alice_b"}}]',
  '{"ChainA":{"TokenA.balances[0xA11CE]":"900","LockBridge.lockedTotal":"100"},"ChainB":{"alice_b":"100"},"message.status":"EXECUTED"}'),
 ('TC02_replay_rejected',
  'Gửi lại cùng message (cùng nonce) phải bị từ chối, trạng thái ChainB không đổi',
  'TOKEN_BRIDGE', 1, 2,
  '{"ChainB":{"alice_b":"100"}}',
  '[{"relay":"resend","nonce":0}]',
  '{"ChainB":{"alice_b":"100"},"message.status":"FAILED"}');
