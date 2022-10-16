#include <iostream>
#include <sstream>
#include <future>
#include "../core/logger.hpp"
#include "../shared/api.hpp"
#include "mempool.hpp"
#include "blockchain.hpp"
#include "virtual_chain.hpp"
using namespace std;

#define TX_BRANCH_FACTOR 10
#define MIN_FEE_TO_ENTER_MEMPOOL 1

MemPool::MemPool(HostManager& h, BlockChain &b) : hosts(h), blockchain(b) {
    this->shutdown = false;
}

MemPool::~MemPool() {
    this->shutdown = true;
}

void mempool_sync(MemPool& mempool) {
    while(true) {
        if (mempool.shutdown) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        vector<Transaction> txs;
        {
            std::unique_lock<std::mutex> ul2(mempool.sendLock);
            if (mempool.toSend.size() == 0) {
                continue;
            } else {
                txs.assign(std::make_move_iterator(mempool.toSend.begin()), std::make_move_iterator(mempool.toSend.end()));
                mempool.toSend.clear();
            }
        }

        vector<future<void>> reqs;
        set<string> neighbors;
        bool success = false;

        neighbors = mempool.hosts.sampleFreshHosts(TX_BRANCH_FACTOR);

        for(auto neighbor : neighbors) {
            reqs.push_back(std::async([neighbor, &txs, &success](){
                try {
                    sendTransactions(neighbor, txs);
                    success = true;
                } catch(...) {
                    Logger::logError("MemPool::sync", "Could not send tx to " + neighbor);
                }
            }));
        }
        for(int i =0 ; i < reqs.size(); i++) {
            reqs[i].get();
        }

        // if we did not succeed sending transactions, put them back in queue
        if (!success) {
            std::unique_lock<std::mutex> ul2(mempool.sendLock);
            for (auto tx : txs) {
                mempool.toSend.push_back(tx);
            }
        }
    }
}

void MemPool::sync() {
    this->syncThread.push_back(std::thread(mempool_sync, ref(*this)));
}

bool MemPool::hasTransaction(Transaction t) {
    bool ret = false;
    std::unique_lock<std::mutex> ul(lock);
    if (this->transactionQueue.find(t) != this->transactionQueue.end()) ret = true;
    return ret;
}

void MemPool::addProgram(ProgramID programId, std::shared_ptr<VirtualChain> program) {
    programs[programId] = program;
}

ExecutionStatus MemPool::addTransaction(Transaction t) {
    ExecutionStatus status;
    std::unique_lock<std::mutex> ul(lock);
    if (this->transactionQueue.find(t) != this->transactionQueue.end()) {
        status = ALREADY_IN_QUEUE;
        return status;
    }

    // check if this is a layer-2 transaction
    if (t.isLayer2()) {
        if (programs.find(t.getProgramId()) != programs.end()) {
            std::unique_lock<std::mutex> ul2(this->sendLock);
            this->toSend.push_back(t);
            this->programTransactions[t.getProgramId()].insert(t);
            return SUCCESS;
        } else {
            return UNSUPPORTED_CHAIN;
        }
    }
    if (t.getFee() < MIN_FEE_TO_ENTER_MEMPOOL && t.getProgramId() == NULL_SHA256_HASH) {
        status = TRANSACTION_FEE_TOO_LOW;
        return status;
    }
    
    status = this->blockchain.verifyTransaction(t);
    if (status != SUCCESS) {
        return status;
    } else {
        // check how much of the from wallets balance is outstanding in mempool
        TransactionAmount outgoing = 0;
        TransactionAmount totalTxAmount = t.getAmount() + t.getFee();
        if (!t.isFee()) outgoing = this->mempoolOutgoing[t.fromWallet()];

        if (!t.isFee() && outgoing + totalTxAmount > this->blockchain.getWalletValue(t.fromWallet())) {
            status = BALANCE_TOO_LOW;
        } else  if (this->transactionQueue.size() < (MAX_TRANSACTIONS_PER_BLOCK - 1)) {
            this->transactionQueue.insert(t);
            this->mempoolOutgoing[t.fromWallet()] += totalTxAmount;
            status = SUCCESS;
        } else {
            status = QUEUE_FULL;
        }
        if (status==SUCCESS) {
            std::unique_lock<std::mutex> ul2(this->sendLock);
            this->toSend.push_back(t);
        }
        return status;
    }
}

size_t MemPool::size() {
    return this->transactionQueue.size();
}

vector<Transaction> MemPool::getTransactions() {
    vector<Transaction> transactions;
    for(auto tx : this->transactionQueue) {
        transactions.push_back(tx);
    }
    return std::move(transactions);
}

std::pair<char*, size_t> MemPool::getRaw(ProgramID programId) {
    if (programId == NULL_SHA256_HASH) {
        int count = 0;
        std::unique_lock<std::mutex> ul(lock);
        size_t len = this->transactionQueue.size() * transactionInfoBufferSize();
        char* buf = (char*) malloc(len);
        for (auto tx : this->transactionQueue) {
            TransactionInfo t = tx.serialize();
            transactionInfoToBuffer(t, buf + count);
            count+=transactionInfoBufferSize();
        }
        return std::pair<char*, size_t>((char*)buf, len);
    } else {
        int count = 0;
        auto& q = this->programTransactions[programId];
        std::unique_lock<std::mutex> ul(lock);
        size_t len = q.size() * transactionInfoBufferSize();
        char* buf = (char*) malloc(len);
        for (auto tx : q) {
            TransactionInfo t = tx.serialize();
            transactionInfoToBuffer(t, buf + count);
            count+=transactionInfoBufferSize();
        }
        return std::pair<char*, size_t>((char*)buf, len);
    }

}

void MemPool::finishBlock(Block& block, ProgramID programId) {
    std::unique_lock<std::mutex> ul(lock);
    // erase all of this blocks included transactions from the mempool
    for(auto tx : block.getTransactions()) {
        if (programId == NULL_SHA256_HASH) {
            auto it = this->transactionQueue.find(tx);
            if (it != this->transactionQueue.end()) {
                this->transactionQueue.erase(it);
                if (!tx.isFee()) {
                    this->mempoolOutgoing[tx.fromWallet()] -= tx.getAmount() + tx.getFee();
                    if (this->mempoolOutgoing[tx.fromWallet()] == 0) {
                        // remove the key if there is no longer an outgoing amount
                        auto it = this->mempoolOutgoing.find(tx.fromWallet());
                        this->mempoolOutgoing.erase(it);
                    }
                }
            }
        } else {
            auto& q = this->programTransactions[programId];
            auto it = q.find(tx);
            if (it != q.end()) {
                q.erase(it);
            }
        }
    }
}
