#ifndef BPLUS_TREE_H
#define BPLUS_TREE_H

#include "storage/page.h"
#include <vector>
#include <algorithm>
#include <memory>
#include <fstream>
#include <iostream>

/**
 * @brief BPlusNode represents a single block/node inside the B+ Tree.
 * It can be either an internal routing node or a leaf data node.
 * 
 * @tparam KeyType The data type of the search keys (INT or std::string).
 */
template <typename KeyType>
struct BPlusNode {
    bool is_leaf;               // True if this is a leaf node, false if internal routing node
    std::vector<KeyType> keys;  // Sorted keys stored in this node
    
    // Leaf node specific attributes:
    // values[i] holds all the RecordIDs matching keys[i] (multi-value index support)
    std::vector<std::vector<RecordID>> values; 
    BPlusNode* next = nullptr;   // Pointer to next sibling leaf node (facilitates range scans)
    BPlusNode* prev = nullptr;   // Pointer to previous sibling leaf node
    
    // Internal node specific attributes:
    std::vector<BPlusNode*> children; // Pointers to child nodes (size is always keys.size() + 1)
    BPlusNode* parent = nullptr;      // Pointer to parent node
    
    /**
     * @brief Construct a new BPlusNode.
     * @param leaf True if creating a leaf node.
     */
    BPlusNode(bool leaf) : is_leaf(leaf) {}
    
    /**
     * @brief Destroy the BPlusNode object. Recursively cleans up child nodes if internal.
     */
    ~BPlusNode() {
        if (!is_leaf) {
            for (auto* child : children) {
                delete child;
            }
        }
    }
};

/**
 * @brief BPlusTree implements a self-balancing B+ Tree indexing structure.
 * 
 * Characteristics:
 * - Internal nodes store search keys to guide lookups (high fan-out).
 * - Leaf nodes contain the keys mapped to their actual values (vector of RecordIDs).
 * - Leaves form a doubly-linked list (`next`/`prev` pointers) for O(1) range iterations.
 * 
 * @tparam KeyType Data type of keys indexed (usually int or std::string).
 */
template <typename KeyType>
class BPlusTree {
private:
    BPlusNode<KeyType>* root_; // Root pointer of the tree
    size_t max_keys_;          // Order of the tree (M) - max keys per node before split triggers
    
    /**
     * @brief Traverses the tree from root to leaf to locate the node containing the target key.
     * Runs in logarithmic time O(log N).
     * @param key The search key.
     * @return BPlusNode<KeyType>* Pointer to the leaf node.
     */
    BPlusNode<KeyType>* FindLeafNode(const KeyType& key) {
        BPlusNode<KeyType>* curr = root_;
        while (!curr->is_leaf) {
            // Binary search to find the upper bound key index
            auto it = std::upper_bound(curr->keys.begin(), curr->keys.end(), key);
            int idx = std::distance(curr->keys.begin(), it);
            curr = curr->children[idx]; // Move down to target child node
        }
        return curr;
    }
    
    /**
     * @brief Inserts a split key and right sibling pointer back up into the parent internal node.
     * Recursively triggers splits up the tree if the parent exceeds max keys capacity.
     */
    void InsertIntoParent(BPlusNode<KeyType>* left, const KeyType& key, BPlusNode<KeyType>* right) {
        if (left == root_) {
            // Root split: create a new root routing node pointing to left and right nodes
            BPlusNode<KeyType>* new_root = new BPlusNode<KeyType>(false);
            new_root->keys.push_back(key);
            new_root->children.push_back(left);
            new_root->children.push_back(right);
            left->parent = new_root;
            right->parent = new_root;
            root_ = new_root;
            return;
        }
        
        BPlusNode<KeyType>* parent = left->parent;
        auto it = std::upper_bound(parent->keys.begin(), parent->keys.end(), key);
        int idx = std::distance(parent->keys.begin(), it);
        
        parent->keys.insert(parent->keys.begin() + idx, key);
        parent->children.insert(parent->children.begin() + idx + 1, right);
        right->parent = parent;
        
        // Split parent node if it exceeds max capacity
        if (parent->keys.size() > max_keys_) {
            BPlusNode<KeyType>* sibling = new BPlusNode<KeyType>(false);
            size_t split_idx = parent->keys.size() / 2;
            KeyType parent_key = parent->keys[split_idx];
            
            // Distribute children and keys to right sibling
            sibling->keys.assign(parent->keys.begin() + split_idx + 1, parent->keys.end());
            sibling->children.assign(parent->children.begin() + split_idx + 1, parent->children.end());
            
            for (auto* child : sibling->children) {
                child->parent = sibling;
            }
            
            parent->keys.erase(parent->keys.begin() + split_idx, parent->keys.end());
            parent->children.erase(parent->children.begin() + split_idx + 1, parent->children.end());
            
            // Recurse upwards
            InsertIntoParent(parent, parent_key, sibling);
        }
    }
    
public:
    /**
     * @brief Construct a new BPlusTree object.
     * @param max_keys Maximum keys per node. Defaults to 3 for testing split behavior easily.
     */
    BPlusTree(size_t max_keys = 3) : max_keys_(max_keys) {
        root_ = new BPlusNode<KeyType>(true); // Initialize tree with an empty leaf root
    }
    
    /**
     * @brief Destroy the BPlusTree object and free all allocated nodes recursively.
     */
    ~BPlusTree() {
        delete root_;
    }
    
    /**
     * @brief Searches for RIDs associated with a specific key.
     * Runs in O(log N) lookup complexity.
     * @param key The target search key.
     * @return std::vector<RecordID> List of matching physical record pointers.
     */
    std::vector<RecordID> Search(const KeyType& key) {
        BPlusNode<KeyType>* leaf = FindLeafNode(key);
        auto it = std::lower_bound(leaf->keys.begin(), leaf->keys.end(), key);
        if (it != leaf->keys.end() && *it == key) {
            int idx = std::distance(leaf->keys.begin(), it);
            return leaf->values[idx];
        }
        return {};
    }
    
    /**
     * @brief Inserts a key and its associated RecordID.
     * Triggers leaf node splitting and parent propagation if node capacity overflows.
     * @param key The key to insert.
     * @param rid The physical RecordID pointer.
     */
    void Insert(const KeyType& key, RecordID rid) {
        BPlusNode<KeyType>* leaf = FindLeafNode(key);
        auto it = std::lower_bound(leaf->keys.begin(), leaf->keys.end(), key);
        int idx = std::distance(leaf->keys.begin(), it);
        
        if (it != leaf->keys.end() && *it == key) {
            // Duplicate key handling: Append RecordID to existing values vector
            leaf->values[idx].push_back(rid);
            return;
        }
        
        // Insert key and record in sorted position
        leaf->keys.insert(leaf->keys.begin() + idx, key);
        leaf->values.insert(leaf->values.begin() + idx, std::vector<RecordID>{rid});
        
        // Trigger split if leaf size exceeds capacity limit
        if (leaf->keys.size() > max_keys_) {
            BPlusNode<KeyType>* sibling = new BPlusNode<KeyType>(true);
            size_t split_idx = leaf->keys.size() / 2;
            
            // Distribute split records to the right sibling node
            sibling->keys.assign(leaf->keys.begin() + split_idx, leaf->keys.end());
            sibling->values.assign(leaf->values.begin() + split_idx, leaf->values.end());
            
            leaf->keys.erase(leaf->keys.begin() + split_idx, leaf->keys.end());
            leaf->values.erase(leaf->values.begin() + split_idx, leaf->values.end());
            
            // Adjust doubly linked list pointers for sibling scanning
            sibling->next = leaf->next;
            if (leaf->next) leaf->next->prev = sibling;
            leaf->next = sibling;
            sibling->prev = leaf;
            
            // Insert split key to parent routing node
            InsertIntoParent(leaf, sibling->keys[0], sibling);
        }
    }
    
    /**
     * @brief Traverses the entire doubly linked list of leaf nodes to return all indexed elements.
     * Useful for index scans, index serialization, and indexing re-builds.
     * @param entries Vector to populate with sorted key-value pairs.
     */
    void GetAllEntries(std::vector<std::pair<KeyType, RecordID>>& entries) {
        BPlusNode<KeyType>* curr = root_;
        // Navigate all the way down to the first left-most leaf node
        while (!curr->is_leaf) {
            curr = curr->children[0];
        }
        
        // Walk sibling next pointers to gather sorted entries
        while (curr != nullptr) {
            for (size_t i = 0; i < curr->keys.size(); ++i) {
                for (const auto& rid : curr->values[i]) {
                    entries.push_back({curr->keys[i], rid});
                }
            }
            curr = curr->next;
        }
    }
    
    /**
     * @brief Clears the entire tree and resets to an empty root.
     */
    void Clear() {
        delete root_;
        root_ = new BPlusNode<KeyType>(true);
    }
};

#endif // BPLUS_TREE_H
