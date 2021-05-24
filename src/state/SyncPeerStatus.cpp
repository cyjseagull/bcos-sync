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
 * @brief matain the sync status
 * @file SyncPeerStatus.cpp
 * @author: jimmyshi
 * @date 2021-05-24
 */
#include "SyncPeerStatus.h"

using namespace bcos;
using namespace bcos::sync;
using namespace bcos::crypto;
using namespace bcos::protocol;

PeerStatus::PeerStatus(BlockSyncConfig::Ptr _config, PublicPtr _nodeId, BlockNumber _number,
    HashType const& _hash, HashType const& _gensisHash)
  : m_nodeId(_nodeId),
    m_number(_number),
    m_hash(_hash),
    m_genesisHash(_gensisHash),
    m_downloadRequests(std::make_shared<DownloadRequestQueue>(_config))
{}

PeerStatus::PeerStatus(
    BlockSyncConfig::Ptr _config, PublicPtr _nodeId, BlockSyncStatusInterface::ConstPtr _status)
  : PeerStatus(_config, _nodeId, _status->number(), _status->hash(), _status->genesisHash())
{}

void PeerStatus::update(BlockSyncStatusInterface::ConstPtr _status)
{
    UpgradableGuard l(x_mutex);
    if (_status->number() <= m_number)
    {
        return;
    }
    if (_status->genesisHash() != m_genesisHash)
    {
        BLOCK_SYNC_LOG(WARNING) << LOG_BADGE("Status")
                                << LOG_DESC(
                                       "Receive invalid status packet with different genesis hash")
                                << LOG_KV("peer", m_nodeId->shortHex())
                                << LOG_KV("genesisHash", _status->genesisHash().abridged())
                                << LOG_KV("storedGenesisHash", m_genesisHash.abridged());
        return;
    }
    UpgradeGuard ul(l);
    m_number = _status->number();
    m_hash = _status->hash();

    BLOCK_SYNC_LOG(DEBUG) << LOG_DESC("updatePeerStatus") << LOG_KV("peer", m_nodeId->shortHex())
                          << LOG_KV("number", _status->number())
                          << LOG_KV("hash", _status->hash().abridged())
                          << LOG_KV("genesisHash", _status->genesisHash().abridged());
}

bool SyncPeerStatus::hashPeer(PublicPtr _peer)
{
    ReadGuard l(x_peersStatus);
    return m_peersStatus.count(_peer);
}

bool SyncPeerStatus::updatePeerStatus(
    PublicPtr _peer, BlockSyncStatusInterface::ConstPtr _peerStatus)
{
    WriteGuard l(x_peersStatus);
    // update the existed peer status
    if (m_peersStatus.count(_peer))
    {
        auto status = m_peersStatus[_peer];
        status->update(_peerStatus);
        return true;
    }
    // create and insert the new peer status
    auto peerStatus = std::make_shared<PeerStatus>(m_config, _peer, _peerStatus);
    m_peersStatus.insert(std::make_pair(_peer, peerStatus));
    BLOCK_SYNC_LOG(DEBUG) << LOG_DESC("updatePeerStatus: new peer")
                          << LOG_KV("peer", _peer->shortHex())
                          << LOG_KV("number", _peerStatus->number())
                          << LOG_KV("hash", _peerStatus->hash().abridged())
                          << LOG_KV("genesisHash", _peerStatus->genesisHash().abridged());
    return true;
}

void SyncPeerStatus::deletePeer(PublicPtr _peer)
{
    WriteGuard l(x_peersStatus);
    auto peer = m_peersStatus.find(_peer);
    if (peer != m_peersStatus.end())
    {
        m_peersStatus.erase(peer);
    }
}