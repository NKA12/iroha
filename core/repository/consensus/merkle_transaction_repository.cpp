/*
Copyright Soramitsu Co., Ltd. 2016 All Rights Reserved.
http://soramitsu.co.jp

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

     http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/

#include "merkle_transaction_repository.hpp"
#include "../world_state_repository.hpp"

#include <memory>
#include <iostream>

#include "../../util/logger.hpp"
#include "../../crypto/hash.hpp"

#include "../../../flatbuf/api_generated.h"

namespace merkle_transaction_repository {

    //TODO: change bool to throw an exception instead
    bool commit(const iroha::ConsensusEvent& event) {
        std::vector<std::tuple<std::string, std::string>> batchCommit
          = {
                std::make_tuple("last_insertion", pevent->transaction().hash()),
                std::make_tuple(pevent->transaction().hash(), event->tx->getAsText())
        };

        calculateNewRootHash(event, batchCommit);

        return repository::world_state_repository::addBatch<std::string>(batchCommit);
    }

    bool leafExists(const std::string& hash) {
        return !repository::world_state_repository::find(hash).empty();
    }

    std::string getLeaf(const std::string& hash) {
        return repository::world_state_repository::find(hash);
    }


    std::string calculateNewRootHash(const iroha::ConsensusEvent& event,
                                     std::vector<std::tuple<std::string, std::string>> &batchCommit) {

        std::string lastInsertion = repository::world_state_repository::find("last_insertion");

        if (lastInsertion.empty()) {
            // Note: there is no need to update the tree's DB here, because there is only one transaction--the current!
            return pevent->transaction().hash();
        }

        std::string parent     = repository::world_state_repository::find(lastInsertion + "_parent");
        std::string leftChild  = repository::world_state_repository::find(parent + "_leftChild");
        std::string rightChild = repository::world_state_repository::find(parent + "_rightChild");

        if (rightChild.empty()) {
            // insert the event's transaction as the right child
            rightChild = pevent->transaction().hash();
            std::string newParentHash = hash::sha3_256_hex(leftChild + rightChild);

            if (!batchCommit.empty()) { // TODO: this may not be the best comparison to use
                batchCommit.emplace_back(lastInsertion + "_rightChild", rightChild);
                batchCommit.emplace_back(lastInsertion + "_parent", rightChild);
            }

            // Propagate up the tree to the root
            while (!parent.empty()) {
                std::string newParentHash = hash::sha3_256_hex(leftChild + rightChild);

                if (!batchCommit.empty()) { // TODO: this may not be the best comparison to use
                    batchCommit.emplace_back(newParentHash + "_parent", rightChild);
                    // TODO: delete old, unused nodes
                }

                // increment the right child and the parent, to move up the tree
                rightChild = newParentHash;
                parent = repository::world_state_repository::find(parent + "_parent");
            }

            if (!batchCommit.empty()) { // TODO: this may not be the best comparison to use
                batchCommit.emplace_back("merkle_root", rightChild);
                // TODO: delete old, unused nodes
            }
            return rightChild;

        } else {
            std::string newLeftChild  = pevent->transaction().hash();
            std::string newParentHash = hash::sha3_256_hex(currHash);

            std::string oldParent = parent;

            // Propagate up the tree to the root
            while (!parent.empty()) {

                if (!batchCommit.empty()) { // TODO: this may not be the best comparison to use
                    batchCommit.emplace_back(pevent->transaction().hash() + "_parent", newParentHash);
                    batchCommit.emplace_back(pevent->transaction().hash() + "_leftChild", newLeftChild);
                    // TODO: delete old, unused nodes
                }

                // increment the right child and the parent, to move up the tree
                newLeftChild = newParentHash;
                newParentHash = hash::sha3_256_hex(newLeftChild);

                oldParent = parent;
                parent = repository::world_state_repository::find(parent + "_parent");
            }

            newParentHash = hash::sha3_256_hex(oldParent + newLeftChild);

            // save new root
            if (!batchCommit.empty()) { // TODO: this may not be the best comparison to use
                batchCommit.emplace_back(event->tx->getHash() + "merkle_root", newParentHash);
                // TODO: delete old, unused nodes
            }

            return newParentHash;
        }
    }
};  // namespace merkle_transaction_repository
