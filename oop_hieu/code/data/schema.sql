-- =====================================================================
-- NHÓM 10 - Khung kiểm thử & thực thi hợp đồng thông minh xuyên chuỗi
-- Database schema (SQLite 3)
--
-- Ghi chú:
--  * Số tiền / số dư lưu dạng TEXT (chuỗi số nguyên) vì EVM dùng uint256,
--    vượt quá giới hạn INTEGER 64-bit của SQL.
--  * Hỗ trợ cả 2 mô hình sổ cái: Account-based (EVM) và UTXO-based.
--  * Bật khoá ngoại mỗi khi mở kết nối: PRAGMA foreign_keys = ON;
-- =====================================================================

PRAGMA foreign_keys = ON;

-- ---------------------------------------------------------------------
-- 1. CẤU HÌNH MẠNG (ứng với IBlockchainAdapter)
-- ---------------------------------------------------------------------
CREATE TABLE chains (
    chain_id        INTEGER PRIMARY KEY AUTOINCREMENT,
    name            TEXT    NOT NULL UNIQUE,              -- 'ChainA-EVM'
    chain_type      TEXT    NOT NULL
                    CHECK (chain_type IN ('EVM','UTXO','SOLANA','MOCK')),
    ledger_model    TEXT    NOT NULL
                    CHECK (ledger_model IN ('ACCOUNT','UTXO')),
    adapter_class   TEXT    NOT NULL,                     -- 'EVMAdapter'
    rpc_url         TEXT,                                 -- NULL = in-memory
    native_symbol   TEXT    NOT NULL,
    decimals        INTEGER NOT NULL DEFAULT 18,
    block_time_ms   INTEGER NOT NULL DEFAULT 1000,
    created_at      TEXT    NOT NULL DEFAULT (datetime('now'))
);

-- ---------------------------------------------------------------------
-- 2. SỔ CÁI CỤC BỘ (LocalLedgerSimulator)
-- ---------------------------------------------------------------------
CREATE TABLE blocks (
    block_id        INTEGER PRIMARY KEY AUTOINCREMENT,
    chain_id        INTEGER NOT NULL REFERENCES chains(chain_id) ON DELETE CASCADE,
    height          INTEGER NOT NULL,
    block_hash      TEXT    NOT NULL,
    parent_hash     TEXT,
    timestamp       TEXT    NOT NULL DEFAULT (datetime('now')),
    UNIQUE (chain_id, height),
    UNIQUE (chain_id, block_hash)
);

-- Account-based (EVM): EOA và Contract account
CREATE TABLE accounts (
    account_id      INTEGER PRIMARY KEY AUTOINCREMENT,
    chain_id        INTEGER NOT NULL REFERENCES chains(chain_id) ON DELETE CASCADE,
    address         TEXT    NOT NULL,
    label           TEXT,                                 -- 'Alice', 'Bridge'
    balance         TEXT    NOT NULL DEFAULT '0',
    nonce           INTEGER NOT NULL DEFAULT 0 CHECK (nonce >= 0),
    is_contract     INTEGER NOT NULL DEFAULT 0 CHECK (is_contract IN (0,1)),
    UNIQUE (chain_id, address)
);

CREATE TABLE contracts (
    contract_id     INTEGER PRIMARY KEY AUTOINCREMENT,
    account_id      INTEGER NOT NULL UNIQUE REFERENCES accounts(account_id) ON DELETE CASCADE,
    name            TEXT    NOT NULL,                     -- 'WrappedToken'
    contract_type   TEXT    NOT NULL
                    CHECK (contract_type IN ('TOKEN','BRIDGE','LENDING','OTHER')),
    bytecode_hash   TEXT,
    abi_json        TEXT,
    deployed_at     TEXT    NOT NULL DEFAULT (datetime('now'))
);

-- SmartContractState: storage slot dạng Key-Value
CREATE TABLE contract_storage (
    contract_id     INTEGER NOT NULL REFERENCES contracts(contract_id) ON DELETE CASCADE,
    slot_key        TEXT    NOT NULL,                     -- 'balances[0xAlice]'
    slot_value      TEXT    NOT NULL,
    PRIMARY KEY (contract_id, slot_key)
);

-- ---------------------------------------------------------------------
-- 3. GIAO DỊCH (TransactionFactory sinh ra, ExecutionEngine thực thi)
-- ---------------------------------------------------------------------
CREATE TABLE transactions (
    tx_id           INTEGER PRIMARY KEY AUTOINCREMENT,
    chain_id        INTEGER NOT NULL REFERENCES chains(chain_id) ON DELETE CASCADE,
    block_id        INTEGER REFERENCES blocks(block_id) ON DELETE SET NULL,
    tx_hash         TEXT    NOT NULL,
    tx_format       TEXT    NOT NULL
                    CHECK (tx_format IN ('EVM','UTXO','SOLANA_INSTRUCTION')),
    -- Trường chung / EVM (NULL với UTXO)
    from_address    TEXT,
    to_address      TEXT,
    value           TEXT    DEFAULT '0',
    nonce           INTEGER,
    gas_price       TEXT,
    gas_limit       INTEGER,
    gas_used        INTEGER,
    data            TEXT,                                 -- calldata hex / JSON
    -- Kết quả thực thi (TransactionResult)
    status          TEXT    NOT NULL DEFAULT 'PENDING'
                    CHECK (status IN ('PENDING','SUCCESS','REVERTED','FAILED')),
    revert_reason   TEXT,
    created_at      TEXT    NOT NULL DEFAULT (datetime('now')),
    executed_at     TEXT,
    UNIQUE (chain_id, tx_hash)
);

-- UTXO model
CREATE TABLE utxos (
    utxo_id         INTEGER PRIMARY KEY AUTOINCREMENT,
    chain_id        INTEGER NOT NULL REFERENCES chains(chain_id) ON DELETE CASCADE,
    created_by_tx   INTEGER NOT NULL REFERENCES transactions(tx_id) ON DELETE CASCADE,
    output_index    INTEGER NOT NULL CHECK (output_index >= 0),
    owner_address   TEXT    NOT NULL,
    amount          TEXT    NOT NULL,
    locking_script  TEXT,
    spent_by_tx     INTEGER REFERENCES transactions(tx_id) ON DELETE SET NULL,
    UNIQUE (created_by_tx, output_index)
);

CREATE TABLE tx_inputs (
    tx_id           INTEGER NOT NULL REFERENCES transactions(tx_id) ON DELETE CASCADE,
    input_index     INTEGER NOT NULL,
    utxo_id         INTEGER NOT NULL UNIQUE REFERENCES utxos(utxo_id), -- chống double-spend
    signature       TEXT    NOT NULL,
    PRIMARY KEY (tx_id, input_index)
);

-- Event / log phát ra khi thực thi (Relayer lắng nghe bảng này)
CREATE TABLE events (
    event_id        INTEGER PRIMARY KEY AUTOINCREMENT,
    tx_id           INTEGER NOT NULL REFERENCES transactions(tx_id) ON DELETE CASCADE,
    contract_id     INTEGER REFERENCES contracts(contract_id) ON DELETE SET NULL,
    log_index       INTEGER NOT NULL,
    event_name      TEXT    NOT NULL,                     -- 'Locked', 'Minted'
    payload_json    TEXT    NOT NULL,
    processed       INTEGER NOT NULL DEFAULT 0 CHECK (processed IN (0,1)),
    UNIQUE (tx_id, log_index)
);

-- ---------------------------------------------------------------------
-- 4. XUYÊN CHUỖI (CrossChainRelayer / CrossChainMessage)
-- ---------------------------------------------------------------------
CREATE TABLE cross_chain_messages (
    message_id      INTEGER PRIMARY KEY AUTOINCREMENT,
    message_hash    TEXT    NOT NULL UNIQUE,
    source_chain_id INTEGER NOT NULL REFERENCES chains(chain_id),
    target_chain_id INTEGER NOT NULL REFERENCES chains(chain_id),
    source_event_id INTEGER REFERENCES events(event_id) ON DELETE SET NULL,
    source_tx_id    INTEGER REFERENCES transactions(tx_id) ON DELETE SET NULL,
    target_tx_id    INTEGER REFERENCES transactions(tx_id) ON DELETE SET NULL,
    sender          TEXT    NOT NULL,
    receiver        TEXT    NOT NULL,
    nonce           INTEGER NOT NULL,                     -- chống replay
    payload_json    TEXT    NOT NULL,
    proof           TEXT,                                 -- proof giả lập
    status          TEXT    NOT NULL DEFAULT 'SENT'
                    CHECK (status IN ('SENT','RELAYED','VERIFIED','EXECUTED','FAILED')),
    failure_reason  TEXT,
    created_at      TEXT    NOT NULL DEFAULT (datetime('now')),
    executed_at     TEXT,
    CHECK (source_chain_id <> target_chain_id),
    UNIQUE (source_chain_id, target_chain_id, nonce)
);

-- ---------------------------------------------------------------------
-- 5. KIỂM THỬ (Testing Suite / Test Runner / Snapshot)
-- ---------------------------------------------------------------------
CREATE TABLE test_cases (
    test_case_id    INTEGER PRIMARY KEY AUTOINCREMENT,
    name            TEXT    NOT NULL UNIQUE,
    description     TEXT,
    scenario_type   TEXT    NOT NULL
                    CHECK (scenario_type IN ('TOKEN_BRIDGE','LENDING','CUSTOM')),
    source_chain_id INTEGER NOT NULL REFERENCES chains(chain_id),
    target_chain_id INTEGER NOT NULL REFERENCES chains(chain_id),
    arrange_json    TEXT    NOT NULL,                     -- trạng thái đầu vào
    act_json        TEXT    NOT NULL,                     -- các bước thực thi
    expected_json   TEXT    NOT NULL,                     -- điều kiện Assert
    is_active       INTEGER NOT NULL DEFAULT 1 CHECK (is_active IN (0,1)),
    created_at      TEXT    NOT NULL DEFAULT (datetime('now'))
);

CREATE TABLE test_runs (
    run_id          INTEGER PRIMARY KEY AUTOINCREMENT,
    test_case_id    INTEGER NOT NULL REFERENCES test_cases(test_case_id) ON DELETE CASCADE,
    started_at      TEXT    NOT NULL DEFAULT (datetime('now')),
    finished_at     TEXT,
    result          TEXT    NOT NULL DEFAULT 'RUNNING'
                    CHECK (result IN ('RUNNING','PASSED','FAILED','ERROR')),
    error_message   TEXT,
    rolled_back     INTEGER NOT NULL DEFAULT 0 CHECK (rolled_back IN (0,1))
);

-- Snapshot (Memento) chụp toàn bộ trạng thái 1 chain để RevertState
CREATE TABLE snapshots (
    snapshot_id     INTEGER PRIMARY KEY AUTOINCREMENT,
    run_id          INTEGER REFERENCES test_runs(run_id) ON DELETE CASCADE,
    chain_id        INTEGER NOT NULL REFERENCES chains(chain_id) ON DELETE CASCADE,
    label           TEXT,                                 -- 'before-act'
    block_height    INTEGER,
    state_json      TEXT    NOT NULL,                     -- accounts + storage + utxos
    created_at      TEXT    NOT NULL DEFAULT (datetime('now'))
);

-- Bảng State Changes hiển thị trên Dashboard
CREATE TABLE state_changes (
    change_id       INTEGER PRIMARY KEY AUTOINCREMENT,
    run_id          INTEGER REFERENCES test_runs(run_id) ON DELETE CASCADE,
    chain_id        INTEGER NOT NULL REFERENCES chains(chain_id) ON DELETE CASCADE,
    tx_id           INTEGER REFERENCES transactions(tx_id) ON DELETE SET NULL,
    entity_type     TEXT    NOT NULL
                    CHECK (entity_type IN ('BALANCE','NONCE','STORAGE','UTXO')),
    entity_key      TEXT    NOT NULL,                     -- address hoặc slot
    old_value       TEXT,
    new_value       TEXT,
    changed_at      TEXT    NOT NULL DEFAULT (datetime('now'))
);

CREATE TABLE assertion_results (
    assertion_id    INTEGER PRIMARY KEY AUTOINCREMENT,
    run_id          INTEGER NOT NULL REFERENCES test_runs(run_id) ON DELETE CASCADE,
    description     TEXT    NOT NULL,
    expected_value  TEXT,
    actual_value    TEXT,
    passed          INTEGER NOT NULL CHECK (passed IN (0,1))
);

-- ---------------------------------------------------------------------
-- 6. INDEX
-- ---------------------------------------------------------------------
CREATE INDEX idx_tx_chain_status   ON transactions(chain_id, status);
CREATE INDEX idx_tx_from           ON transactions(chain_id, from_address);
CREATE INDEX idx_utxo_owner_unspent ON utxos(chain_id, owner_address) WHERE spent_by_tx IS NULL;
CREATE INDEX idx_events_unprocessed ON events(processed, event_name);
CREATE INDEX idx_ccm_status        ON cross_chain_messages(status);
CREATE INDEX idx_state_changes_run ON state_changes(run_id);

-- ---------------------------------------------------------------------
-- 7. VIEW tiện cho Dashboard
-- ---------------------------------------------------------------------
CREATE VIEW v_utxo_balances AS
SELECT chain_id, owner_address,
       SUM(CAST(amount AS INTEGER)) AS balance,   -- đủ dùng cho UTXO (≤ 2^63)
       COUNT(*) AS utxo_count
FROM utxos
WHERE spent_by_tx IS NULL
GROUP BY chain_id, owner_address;

CREATE VIEW v_message_flow AS
SELECT m.message_id, m.message_hash,
       cs.name AS source_chain, ct.name AS target_chain,
       m.sender, m.receiver, m.status,
       ts.tx_hash AS source_tx, tt.tx_hash AS target_tx,
       m.created_at, m.executed_at
FROM cross_chain_messages m
JOIN chains cs ON cs.chain_id = m.source_chain_id
JOIN chains ct ON ct.chain_id = m.target_chain_id
LEFT JOIN transactions ts ON ts.tx_id = m.source_tx_id
LEFT JOIN transactions tt ON tt.tx_id = m.target_tx_id;

CREATE VIEW v_test_summary AS
SELECT tc.name, tc.scenario_type,
       COUNT(r.run_id)                                  AS total_runs,
       SUM(CASE WHEN r.result = 'PASSED' THEN 1 ELSE 0 END) AS passed,
       SUM(CASE WHEN r.result IN ('FAILED','ERROR') THEN 1 ELSE 0 END) AS failed,
       MAX(r.started_at)                                AS last_run
FROM test_cases tc
LEFT JOIN test_runs r ON r.test_case_id = tc.test_case_id
GROUP BY tc.test_case_id;
