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
 * @file DownloadingQueue.h
 * @author: jimmyshi
 * @date 2021-05-24
 */
#pragma once
#include "BlockSyncConfig.h"
#include "interfaces/BlocksMsgInterface.h"
#include <bcos-framework/interfaces/protocol/Block.h>
#include <queue>
namespace bcos
{
namespace sync
{
// increase order
struct BlockCmp
{
    bool operator()(
        bcos::protocol::Block::Ptr const& _first, bcos::protocol::Block::Ptr const& _second)
    {
        // increase order
        return _first->blockHeader()->number() > _second->blockHeader()->number();
    }
};
class DownloadingQueue
{
public:
    using BlocksMessageQueue = std::list<BlocksMsgInterface::Ptr>;
    using BlocksMessageQueuePtr = std::shared_ptr<BlocksMessageQueue>;

    using Ptr = std::shared_ptr<DownloadingQueue>;
    explicit DownloadingQueue(BlockSyncConfig::Ptr _config)
      : m_config(_config), m_blockBuffer(std::make_shared<BlocksMessageQueue>())
    {}
    virtual ~DownloadingQueue() {}

    virtual void push(BlocksMsgInterface::Ptr _blocksData);
    // Is the queue empty?
    virtual bool empty();

    // get the total size of th block queue
    virtual size_t size();

    // pop the top unit of the block queue
    virtual void pop();

    // get the top unit of the block queue
    bcos::protocol::Block::Ptr top(bool isFlushBuffer = false);

    virtual void clearFullQueueIfNotHas(bcos::protocol::BlockNumber _blockNumber);

protected:
    // clear queue and buffer
    virtual void clear();

    // clear queue
    virtual void clearQueue();

    // flush m_buffer into queue
    virtual void flushBufferToQueue();
    virtual bool flushOneShard(BlocksMsgInterface::Ptr _blocksData);
    virtual bool isNewerBlock(bcos::protocol::Block::Ptr _block);

private:
    BlockSyncConfig::Ptr m_config;


    using BlockQueue =
        std::priority_queue<bcos::protocol::Block::Ptr, bcos::protocol::Blocks, BlockCmp>;
    BlockQueue m_blocks;
    mutable SharedMutex x_blocks;

    BlocksMessageQueuePtr m_blockBuffer;
    mutable SharedMutex x_blockBuffer;
};
}  // namespace sync
}  // namespace bcos