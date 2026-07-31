#ifndef BPLUS_TREE_H
#define BPLUS_TREE_H

#include "storage/page.h"
#include <vector>
#include <algorithm>
#include <memory>
#include <fstream>
#include <iostream>

template <typename KeyType>
struct BPlusNode {
    bool is_leaf;
    std::vector<KeyType> keys;
    
    // Leaf node specific
    std::vector<std::vector<RecordID>> values; // multi-value support
    BPlusNode* next = nullptr;
    BPlusNode* prev = nullptr;
    
    // Internal node specific
    std::vector<BPlusNode*> children;
    BPlusNode* parent = nullptr;
    
    BPlusNode(bool leaf) : is_leaf(leaf) {}
    ~BPlusNode() {
        if (!is_leaf) {
            for (auto* child : children) {
                delete child;
            }
        }
    }
};

template <typename KeyType>
class BPlusTree {
private:
    BPlusNode<KeyType>* root_;
    size_t max_keys_; // Order of B+ tree (M)
    
    BPlusNode<KeyType>* FindLeafNode(const KeyType& key) {
        BPlusNode<KeyType>* curr = root_;
        while (!curr->is_leaf) {
            auto it = std::upper_bound(curr->keys.begin(), curr->keys.end(), key);
            int idx = std::distance(curr->keys.begin(), it);
            curr = curr->children[idx];
        }
        return curr;
    }
    
    void InsertIntoParent(BPlusNode<KeyType>* left, const KeyType& key, BPlusNode<KeyType>* right) {
        if (left == root_) {
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
        
        if (parent->keys.size() > max_keys_) {
            // Split parent (internal node)
            BPlusNode<KeyType>* sibling = new BPlusNode<KeyType>(false);
            size_t split_idx = parent->keys.size() / 2;
            KeyType parent_key = parent->keys[split_idx];
            
            sibling->keys.assign(parent->keys.begin() + split_idx + 1, parent->keys.end());
            sibling->children.assign(parent->children.begin() + split_idx + 1, parent->children.end());
            
            for (auto* child : sibling->children) {
                child->parent = sibling;
            }
            
            parent->keys.erase(parent->keys.begin() + split_idx, parent->keys.end());
            parent->children.erase(parent->children.begin() + split_idx + 1, parent->children.end());
            
            InsertIntoParent(parent, parent_key, sibling);
        }
    }
    
public:
    BPlusTree(size_t max_keys = 3) : max_keys_(max_keys) {
        root_ = new BPlusNode<KeyType>(true);
    }
    
    ~BPlusTree() {
        delete root_;
    }
    
    std::vector<RecordID> Search(const KeyType& key) {
        BPlusNode<KeyType>* leaf = FindLeafNode(key);
        auto it = std::lower_bound(leaf->keys.begin(), leaf->keys.end(), key);
        if (it != leaf->keys.end() && *it == key) {
            int idx = std::distance(leaf->keys.begin(), it);
            return leaf->values[idx];
        }
        return {};
    }
    
    void Insert(const KeyType& key, RecordID rid) {
        BPlusNode<KeyType>* leaf = FindLeafNode(key);
        auto it = std::lower_bound(leaf->keys.begin(), leaf->keys.end(), key);
        int idx = std::distance(leaf->keys.begin(), it);
        
        if (it != leaf->keys.end() && *it == key) {
            // Key already exists, add duplicate RID
            leaf->values[idx].push_back(rid);
            return;
        }
        
        leaf->keys.insert(leaf->keys.begin() + idx, key);
        leaf->values.insert(leaf->values.begin() + idx, std::vector<RecordID>{rid});
        
        if (leaf->keys.size() > max_keys_) {
            // Split leaf
            BPlusNode<KeyType>* sibling = new BPlusNode<KeyType>(true);
            size_t split_idx = leaf->keys.size() / 2;
            
            sibling->keys.assign(leaf->keys.begin() + split_idx, leaf->keys.end());
            sibling->values.assign(leaf->values.begin() + split_idx, leaf->values.end());
            
            leaf->keys.erase(leaf->keys.begin() + split_idx, leaf->keys.end());
            leaf->values.erase(leaf->values.begin() + split_idx, leaf->values.end());
            
            sibling->next = leaf->next;
            if (leaf->next) leaf->next->prev = sibling;
            leaf->next = sibling;
            sibling->prev = leaf;
            
            InsertIntoParent(leaf, sibling->keys[0], sibling);
        }
    }
    
    // Clear and return all elements for serialization
    void GetAllEntries(std::vector<std::pair<KeyType, RecordID>>& entries) {
        BPlusNode<KeyType>* curr = root_;
        while (!curr->is_leaf) {
            curr = curr->children[0];
        }
        
        while (curr != nullptr) {
            for (size_t i = 0; i < curr->keys.size(); ++i) {
                for (const auto& rid : curr->values[i]) {
                    entries.push_back({curr->keys[i], rid});
                }
            }
            curr = curr->next;
        }
    }
    
    void Clear() {
        delete root_;
        root_ = new BPlusNode<KeyType>(true);
    }
};

#endif // BPLUS_TREE_H
