/**
 *  Copyright (C) 2021 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 * @brief queue to store the downloading blocks
 * @file DownloadingQueue.cpp
 * @author: jimmyshi
 * @date 2021-05-24
 */
#include "DownloadingQueue.h"
#include "bcos-sync/utilities/Common.h"

using namespace std;
using namespace bcos;
using namespace bcos::protocol;
using namespace bcos::sync;
using namespace bcos::ledger;

void DownloadingQueue::push(BlocksMsgInterface::Ptr _blocksData)
{
    // push to the blockBuffer firstly
    UpgradableGuard l(x_blockBuffer);
    if (m_blockBuffer->size() >= m_config->maxDownloadingBlockQueueSize())
    {
        BLKSYNC_LOG(WARNING) << LOG_BADGE("Download") << LOG_BADGE("BlockSync")
                             << LOG_DESC("DownloadingBlockQueueBuffer is full")
                             << LOG_KV("queueSize", m_blockBuffer->size());
        return;
    }
    UpgradeGuard ul(l);
    m_blockBuffer->emplace_back(_blocksData);
}

bool DownloadingQueue::empty()
{
    ReadGuard l1(x_blockBuffer);
    ReadGuard l2(x_blocks);
    return (m_blocks.empty() && (!m_blockBuffer || m_blockBuffer->empty()));
}

size_t DownloadingQueue::size()
{
    ReadGuard l1(x_blockBuffer);
    ReadGuard l2(x_blocks);
    size_t s = (!m_blockBuffer ? 0 : m_blockBuffer->size()) + m_blocks.size();
    return s;
}

void DownloadingQueue::pop()
{
    WriteGuard l(x_blocks);
    if (!m_blocks.empty())
    {
        m_blocks.pop();
    }
}

Block::Ptr DownloadingQueue::top(bool isFlushBuffer)
{
    if (isFlushBuffer)
    {
        flushBufferToQueue();
    }
    ReadGuard l(x_blocks);
    if (!m_blocks.empty())
    {
        return m_blocks.top();
    }
    return nullptr;
}

void DownloadingQueue::clear()
{
    {
        WriteGuard l(x_blockBuffer);
        m_blockBuffer->clear();
    }
    clearQueue();
}

void DownloadingQueue::clearQueue()
{
    WriteGuard l(x_blocks);
    BlockQueue emptyQueue;
    swap(m_blocks, emptyQueue);  // Does memory leak here ?
}

void DownloadingQueue::flushBufferToQueue()
{
    WriteGuard l(x_blockBuffer);
    bool ret = true;
    while (m_blockBuffer->size() > 0 && ret)
    {
        auto blocksShard = m_blockBuffer->front();
        m_blockBuffer->pop_front();
        ret = flushOneShard(blocksShard);
    }
}

bool DownloadingQueue::flushOneShard(BlocksMsgInterface::Ptr _blocksData)
{
    // pop buffer into queue
    WriteGuard l(x_blocks);
    if (m_blocks.size() >= m_config->maxDownloadingBlockQueueSize())
    {
        BLKSYNC_LOG(DEBUG) << LOG_BADGE("Download") << LOG_BADGE("BlockSync")
                           << LOG_DESC("DownloadingBlockQueueBuffer is full")
                           << LOG_KV("queueSize", m_blocks.size());

        return false;
    }
    BLKSYNC_LOG(TRACE) << LOG_BADGE("Download") << LOG_BADGE("BlockSync")
                       << LOG_DESC("Decoding block buffer")
                       << LOG_KV("blocksShardSize", _blocksData->blocksSize());
    size_t blocksSize = _blocksData->blocksSize();
    for (size_t i = 0; i < blocksSize; i++)
    {
        try
        {
            auto block =
                m_config->blockFactory()->createBlock(_blocksData->blockData(i), true, true);
            if (isNewerBlock(block))
            {
                m_blocks.push(block);
                BLKSYNC_LOG(DEBUG) << LOG_BADGE("Download") << LOG_BADGE("BlockSync")
                                   << LOG_DESC("Flush block to the queue")
                                   << LOG_KV("number", block->blockHeader()->number())
                                   << LOG_KV("nodeId", m_config->nodeID()->shortHex());
            }
        }
        catch (std::exception const& e)
        {
            BLKSYNC_LOG(WARNING) << LOG_BADGE("Download") << LOG_BADGE("BlockSync")
                                 << LOG_DESC("Invalid block data")
                                 << LOG_KV("reason", boost::diagnostic_information(e))
                                 << LOG_KV("blockDataSize", _blocksData->blockData(i).size());
            continue;
        }
    }
    if (m_blocks.size() == 0)
    {
        return true;
    }
    BLKSYNC_LOG(DEBUG) << LOG_BADGE("Download") << LOG_BADGE("BlockSync")
                       << LOG_DESC("Flush buffer to block queue") << LOG_KV("rcv", blocksSize)
                       << LOG_KV("top", m_blocks.top()->blockHeader()->number())
                       << LOG_KV("downloadBlockQueue", m_blocks.size())
                       << LOG_KV("nodeId", m_config->nodeID()->shortHex());
    return true;
}

bool DownloadingQueue::isNewerBlock(Block::Ptr _block)
{
    if (_block->blockHeader()->number() <= m_config->blockNumber())
    {
        return false;
    }
    return true;
}

void DownloadingQueue::clearFullQueueIfNotHas(BlockNumber _blockNumber)
{
    bool needClear = false;
    {
        ReadGuard l(x_blocks);
        if (m_blocks.size() == m_config->maxDownloadingBlockQueueSize() &&
            m_blocks.top()->blockHeader()->number() > _blockNumber)
        {
            needClear = true;
        }
    }
    if (needClear)
    {
        clearQueue();
    }
}

bool DownloadingQueue::verifyExecutedBlock(
    bcos::protocol::Block::Ptr _block, bcos::protocol::BlockHeader::Ptr _blockHeader)
{
    // check blockHash(Note: since the ledger check the parentHash before commit, here no need to
    // check the parentHash)
    if (_block->blockHeader()->hash() != _blockHeader->hash())
    {
        BLKSYNC_LOG(WARNING) << LOG_DESC("verifyExecutedBlock failed for inconsistent hash")
                             << LOG_KV("orgHash", _block->blockHeader()->hash())
                             << LOG_KV("executedHash", _blockHeader->hash())
                             << LOG_KV("orgTxsRoot", _block->blockHeader()->txsRoot())
                             << LOG_KV("executedTxsRoot", _blockHeader->txsRoot())
                             << LOG_KV("orgReceiptsRoot", _block->blockHeader()->receiptsRoot())
                             << LOG_KV("executedReceptsRoot", _blockHeader->receiptsRoot())
                             << LOG_KV("orgDBHash", _block->blockHeader()->stateRoot())
                             << LOG_KV("executedDBHash", _blockHeader->stateRoot());
        return false;
    }
    return true;
}

void DownloadingQueue::applyBlock(Block::Ptr _block, size_t _retryTime)
{
    // at most retry one time
    if (_retryTime >= 2)
    {
        BLKSYNC_LOG(WARNING) << LOG_DESC(
                                    "applyBlock over the max retry time, reset the executedBlock")
                             << LOG_KV("reset executedBlock", m_config->blockNumber())
                             << LOG_KV("number", _block->blockHeader()->number())
                             << LOG_KV("hash", _block->blockHeader()->hash().abridged());
        m_config->setExecutedBlock(m_config->blockNumber());
        return;
    }
    auto self = std::weak_ptr<DownloadingQueue>(shared_from_this());
    m_config->dispatcher()->asyncExecuteBlock(_block, true,
        [self, _block, _retryTime](Error::Ptr _error, protocol::BlockHeader::Ptr _blockHeader) {
            try
            {
                auto downloadQueue = self.lock();
                if (!downloadQueue)
                {
                    return;
                }
                auto config = downloadQueue->m_config;
                // execute/verify exception
                if (_error != nullptr)
                {
                    // reset the executed number
                    BLKSYNC_LOG(WARNING)
                        << LOG_DESC("applyBlock: executing the downloaded block failed and retry")
                        << LOG_KV("number", _block->blockHeader()->number())
                        << LOG_KV("hash", _block->blockHeader()->hash().abridged())
                        << LOG_KV("errorCode", _error->errorCode())
                        << LOG_KV("errorMessage", _error->errorMessage());
                    downloadQueue->applyBlock(_block, (_retryTime + 1));
                    return;
                }
                if (!downloadQueue->verifyExecutedBlock(_block, _blockHeader))
                {
                    downloadQueue->m_config->setExecutedBlock(config->blockNumber());
                    return;
                }
                BLKSYNC_LOG(INFO) << LOG_BADGE("Download")
                                  << LOG_DESC("BlockSync: applyBlock success")
                                  << LOG_KV("number", _block->blockHeader()->number())
                                  << LOG_KV("hash", _block->blockHeader()->hash().abridged())
                                  << LOG_KV("nextBlock", downloadQueue->m_config->nextBlock())
                                  << LOG_KV("node", downloadQueue->m_config->nodeID()->shortHex());
                // verify and comit the block
                downloadQueue->updateCommitQueue(_block);
            }
            catch (std::exception const& e)
            {
                BLKSYNC_LOG(WARNING) << LOG_DESC("applyBlock exception")
                                     << LOG_KV("number", _block->blockHeader()->number())
                                     << LOG_KV("hash", _block->blockHeader()->hash().abridged())
                                     << LOG_KV("error", boost::diagnostic_information(e));
            }
        });
}

bool DownloadingQueue::checkAndCommitBlock(bcos::protocol::Block::Ptr _block)
{
    // check the block number
    if (_block->blockHeader()->number() != m_config->nextBlock())
    {
        BLKSYNC_LOG(WARNING) << LOG_BADGE("Download") << LOG_BADGE("BlockSync: checkBlock")
                             << LOG_DESC("Ignore illegal block")
                             << LOG_KV("reason", "number illegal")
                             << LOG_KV("thisNumber", _block->blockHeader()->number())
                             << LOG_KV("currentNumber", m_config->blockNumber());
        m_config->setExecutedBlock(m_config->blockNumber());
        return false;
    }
    auto self = std::weak_ptr<DownloadingQueue>(shared_from_this());
    m_config->consensus()->asyncCheckBlock(_block, [self, _block](Error::Ptr _error, bool _ret) {
        try
        {
            auto downloadQueue = self.lock();
            if (!downloadQueue)
            {
                return;
            }
            if (_error)
            {
                BLKSYNC_LOG(WARNING)
                    << LOG_DESC("asyncCheckBlock error")
                    << LOG_KV("blockNumber", _block->blockHeader()->number())
                    << LOG_KV("hash", _block->blockHeader()->hash().abridged())
                    << LOG_KV("code", _error->errorCode()) << LOG_KV("msg", _error->errorMessage());
                downloadQueue->m_config->setExecutedBlock(_block->blockHeader()->number() - 1);
                return;
            }
            if (_ret)
            {
                downloadQueue->commitBlock(_block);
                return;
            }
            downloadQueue->m_config->setExecutedBlock(_block->blockHeader()->number() - 1);
            BLKSYNC_LOG(WARNING) << LOG_DESC("asyncCheckBlock failed")
                                 << LOG_KV("blockNumber", _block->blockHeader()->number())
                                 << LOG_KV("hash", _block->blockHeader()->hash().abridged());
        }
        catch (std::exception const& e)
        {
            BLKSYNC_LOG(WARNING) << LOG_DESC("asyncCheckBlock exception")
                                 << LOG_KV("blockNumber", _block->blockHeader()->number())
                                 << LOG_KV("hash", _block->blockHeader()->hash().abridged())
                                 << LOG_KV("error", boost::diagnostic_information(e));
        }
    });
    return true;
}

void DownloadingQueue::updateCommitQueue(Block::Ptr _block)
{
    {
        WriteGuard l(x_commitQueue);
        m_commitQueue.push(_block);
    }
    tryToCommitBlockToLedger();
}

void DownloadingQueue::tryToCommitBlockToLedger()
{
    WriteGuard l(x_commitQueue);
    // remove expired block
    while (!m_commitQueue.empty() &&
           m_commitQueue.top()->blockHeader()->number() <= m_config->blockNumber())
    {
        m_commitQueue.pop();
    }
    // try to commit the block
    if (!m_commitQueue.empty() &&
        m_commitQueue.top()->blockHeader()->number() == m_config->nextBlock())
    {
        auto block = m_commitQueue.top();
        m_commitQueue.pop();
        checkAndCommitBlock(block);
    }
}


void DownloadingQueue::commitBlock(bcos::protocol::Block::Ptr _block)
{
    BLKSYNC_LOG(INFO) << LOG_DESC("commitBlock")
                      << LOG_KV("number", _block->blockHeader()->number())
                      << LOG_KV("txsNum", _block->transactionsSize())
                      << LOG_KV("hash", _block->blockHeader()->hash().abridged());
    // empty block
    if (_block->transactionsSize() == 0)
    {
        commitBlockState(_block);
    }
    // commit transaction firstly
    auto txsData = std::make_shared<std::vector<bytesPointer>>();
    auto txsSize = _block->transactionsSize();
    auto txsHashList = std::make_shared<HashList>();

    txsData->resize(txsSize);
    txsHashList->resize(txsSize);
    tbb::parallel_for(
        tbb::blocked_range<size_t>(0, txsSize), [&](const tbb::blocked_range<size_t>& range) {
            for (size_t i = range.begin(); i < range.end(); ++i)
            {
                auto encodedData = _block->transaction(i)->encode(false);
                (*txsData)[i] = std::make_shared<bytes>(encodedData.begin(), encodedData.end());
                (*txsHashList)[i] = _block->transaction(i)->hash();
            }
        });

    auto self = std::weak_ptr<DownloadingQueue>(shared_from_this());
    m_config->ledger()->asyncStoreTransactions(
        txsData, txsHashList, [self, _block](Error::Ptr _error) {
            try
            {
                auto downloadingQueue = self.lock();
                if (!downloadingQueue)
                {
                    return;
                }
                // store transaction failed
                if (_error)
                {
                    downloadingQueue->m_config->setExecutedBlock(
                        _block->blockHeader()->number() - 1);
                    BLKSYNC_LOG(WARNING) << LOG_DESC("commitBlock: store transactions failed")
                                         << LOG_KV("number", _block->blockHeader()->number())
                                         << LOG_KV("hash", _block->blockHeader()->hash().abridged())
                                         << LOG_KV("txsSize", _block->transactionsSize());
                    return;
                }
                BLKSYNC_LOG(INFO) << LOG_DESC("commitBlock: store transactions success")
                                  << LOG_KV("number", _block->blockHeader()->number())
                                  << LOG_KV("hash", _block->blockHeader()->hash().abridged())
                                  << LOG_KV("txsSize", _block->transactionsSize());
                downloadingQueue->commitBlockState(_block);
            }
            catch (std::exception const& e)
            {
                BLKSYNC_LOG(WARNING) << LOG_DESC("commitBlock exception")
                                     << LOG_KV("error", boost::diagnostic_information(e));
            }
        });
}

void DownloadingQueue::commitBlockState(bcos::protocol::Block::Ptr _block)
{
    BLKSYNC_LOG(INFO) << LOG_DESC("commitBlockState")
                      << LOG_KV("number", _block->blockHeader()->number())
                      << LOG_KV("hash", _block->blockHeader()->hash().abridged());
    auto self = std::weak_ptr<DownloadingQueue>(shared_from_this());
    m_config->ledger()->asyncCommitBlock(
        _block->blockHeader(), [self, _block](Error::Ptr _error, LedgerConfig::Ptr _ledgerConfig) {
            try
            {
                auto downloadingQueue = self.lock();
                if (!downloadingQueue)
                {
                    return;
                }
                if (_error != nullptr)
                {
                    downloadingQueue->m_config->setExecutedBlock(
                        _block->blockHeader()->number() - 1);
                    BLKSYNC_LOG(WARNING) << LOG_DESC("commitBlockState failed")
                                         << LOG_KV("number", _block->blockHeader()->number())
                                         << LOG_KV("hash", _block->blockHeader()->hash().abridged())
                                         << LOG_KV("code", _error->errorCode())
                                         << LOG_KV("message", _error->errorMessage());
                    return;
                }


                // reset the config for the consensus and the blockSync module
                // broadcast the status to all the peers
                // clear the expired cache
                _ledgerConfig->setSealerId(_block->blockHeader()->sealer());
                downloadingQueue->m_newBlockHandler(_ledgerConfig);
                // notify the txpool the transaction result
                downloadingQueue->notifyTransactionsResult(_block);
                // try to commit the next block
                downloadingQueue->tryToCommitBlockToLedger();
                BLKSYNC_LOG(INFO) << LOG_DESC("commitBlockState success")
                                  << LOG_KV("number", _block->blockHeader()->number())
                                  << LOG_KV("hash", _block->blockHeader()->hash().abridged())
                                  << LOG_KV(
                                         "node", downloadingQueue->m_config->nodeID()->shortHex());
            }
            catch (std::exception const& e)
            {
                BLKSYNC_LOG(WARNING) << LOG_DESC("commitBlock exception")
                                     << LOG_KV("number", _block->blockHeader()->number())
                                     << LOG_KV("hash", _block->blockHeader()->hash().abridged())
                                     << LOG_KV("error", boost::diagnostic_information(e));
            }
        });
}


void DownloadingQueue::notifyTransactionsResult(bcos::protocol::Block::Ptr _block)
{
    auto results = std::make_shared<bcos::protocol::TransactionSubmitResults>();
    for (size_t i = 0; i < _block->transactionsSize(); i++)
    {
        auto const& hash = _block->transaction(i)->hash();
        auto txResult =
            m_config->txResultFactory()->createTxSubmitResult(_block->blockHeader(), hash);
        txResult->setNonce(_block->transaction(i)->nonce());
        results->push_back(txResult);
    }
    m_config->txpool()->asyncNotifyBlockResult(
        _block->blockHeader()->number(), results, [_block](Error::Ptr _error) {
            if (_error == nullptr)
            {
                BLKSYNC_LOG(INFO) << LOG_DESC("notify block result success")
                                  << LOG_KV("number", _block->blockHeader()->number())
                                  << LOG_KV("hash", _block->blockHeader()->hash().abridged())
                                  << LOG_KV("txsSize", _block->transactionsSize());
                return;
            }
            BLKSYNC_LOG(INFO) << LOG_DESC("notify block result failed")
                              << LOG_KV("code", _error->errorCode())
                              << LOG_KV("msg", _error->errorMessage());
        });
}

void DownloadingQueue::clearExpiredQueueCache()
{
    clearExpiredCache(m_blocks, x_blocks);
    clearExpiredCache(m_commitQueue, x_commitQueue);
}

void DownloadingQueue::clearExpiredCache(BlockQueue& _queue, SharedMutex& _lock)
{
    WriteGuard l(_lock);
    while (!_queue.empty() && _queue.top()->blockHeader()->number() <= m_config->blockNumber())
    {
        _queue.pop();
    }
}