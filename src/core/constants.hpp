#pragma once

// System
#define DECIMAL_SCALE_FACTOR 10000
#define TIMEOUT_MS 5000
#define TIMEOUT_BLOCK_MS 30000
#define TIMEOUT_BLOCKHEADERS_MS 60000
#define TIMEOUT_SUBMIT_MS 30000
#define BLOCKS_PER_FETCH 200
#define BLOCK_HEADERS_PER_FETCH 2000
#define BUILD_VERSION "0.6.6-beta"


// Files
#define LEDGER_FILE_PATH "./data/ledger"
#define TXDB_FILE_PATH "./data/txdb"
#define BLOCK_STORE_FILE_PATH "./data/blocks"
#define PUFFERFISH_CACHE_FILE_PATH "./data/pufferfish"

// Blocks
#define MAX_TRANSACTIONS_PER_BLOCK 25000
#define PUFFERFISH_START_BLOCK 124500

// Difficulty
#define DIFFICULTY_LOOKBACK 100
#define DESIRED_BLOCK_TIME_SEC 90
#define MIN_DIFFICULTY 6
#define MAX_DIFFICULTY 255

