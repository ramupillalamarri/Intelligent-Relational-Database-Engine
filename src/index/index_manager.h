#ifndef INDEX_MANAGER_H
#define INDEX_MANAGER_H

#include "bplus_tree.h"
#include "catalog/catalog.h"
#include <string>
#include <unordered_map>
#include <memory>
#include <sstream>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#define mkdir_portable(dir) _mkdir(dir)
#else
#define mkdir_portable(dir) mkdir(dir, 0777)
#endif

class IndexManager {
private:
    std::string index_dir_;
    
    // Maps table_name + "_" + col_name -> BPlusTree
    std::unordered_map<std::string, std::unique_ptr<BPlusTree<int>>> int_indexes_;
    std::unordered_map<std::string, std::unique_ptr<BPlusTree<std::string>>> string_indexes_;
    
    std::string GetIndexKey(const std::string& table_name, const std::string& col_name) const {
        return table_name + "_" + col_name;
    }
    
    std::string GetIndexPath(const std::string& table_name, const std::string& col_name) const {
        return index_dir_ + "/" + table_name + "_" + col_name + ".idx";
    }
    
public:
    IndexManager(const std::string& index_dir) : index_dir_(index_dir) {
        mkdir_portable(index_dir_.c_str());
    }
    
    bool CreateIndex(const std::string& table_name, const std::string& col_name, DataType type) {
        std::string key = GetIndexKey(table_name, col_name);
        if (type == DataType::INT) {
            if (int_indexes_.find(key) != int_indexes_.end()) return true;
            int_indexes_[key] = std::make_unique<BPlusTree<int>>();
        } else {
            if (string_indexes_.find(key) != string_indexes_.end()) return true;
            string_indexes_[key] = std::make_unique<BPlusTree<std::string>>();
        }
        return true;
    }
    
    void ClearIndex(const std::string& table_name, const std::string& col_name, DataType type) {
        std::string key = GetIndexKey(table_name, col_name);
        if (type == DataType::INT) {
            auto it = int_indexes_.find(key);
            if (it != int_indexes_.end()) {
                it->second->Clear();
            }
        } else {
            auto it = string_indexes_.find(key);
            if (it != string_indexes_.end()) {
                it->second->Clear();
            }
        }
    }
    
    void Insert(const std::string& table_name, const std::string& col_name, DataType type, const std::string& val_str, RecordID rid) {
        std::string key = GetIndexKey(table_name, col_name);
        CreateIndex(table_name, col_name, type);
        
        if (type == DataType::INT) {
            int val = 0;
            try {
                val = std::stoi(val_str);
            } catch (...) {}
            int_indexes_[key]->Insert(val, rid);
        } else {
            string_indexes_[key]->Insert(val_str, rid);
        }
    }
    
    std::vector<RecordID> Search(const std::string& table_name, const std::string& col_name, DataType type, const std::string& val_str) {
        std::string key = GetIndexKey(table_name, col_name);
        CreateIndex(table_name, col_name, type);
        
        if (type == DataType::INT) {
            int val = 0;
            try {
                val = std::stoi(val_str);
            } catch (...) {}
            auto it = int_indexes_.find(key);
            if (it != int_indexes_.end()) {
                return it->second->Search(val);
            }
        } else {
            auto it = string_indexes_.find(key);
            if (it != string_indexes_.end()) {
                return it->second->Search(val_str);
            }
        }
        return {};
    }
    
    bool SaveIndex(const std::string& table_name, const std::string& col_name, DataType type) {
        std::string key = GetIndexKey(table_name, col_name);
        std::string path = GetIndexPath(table_name, col_name);
        std::ofstream file(path, std::ios::binary);
        if (!file.is_open()) return false;
        
        if (type == DataType::INT) {
            auto it = int_indexes_.find(key);
            if (it == int_indexes_.end()) return false;
            std::vector<std::pair<int, RecordID>> entries;
            it->second->GetAllEntries(entries);
            
            // Format: count, followed by pairs of (key, page_id, slot_id)
            uint64_t count = entries.size();
            file.write(reinterpret_cast<const char*>(&count), sizeof(count));
            for (const auto& entry : entries) {
                file.write(reinterpret_cast<const char*>(&entry.first), sizeof(int));
                file.write(reinterpret_cast<const char*>(&entry.second.page_id), sizeof(uint32_t));
                file.write(reinterpret_cast<const char*>(&entry.second.slot_id), sizeof(uint16_t));
            }
        } else {
            auto it = string_indexes_.find(key);
            if (it == string_indexes_.end()) return false;
            std::vector<std::pair<std::string, RecordID>> entries;
            it->second->GetAllEntries(entries);
            
            uint64_t count = entries.size();
            file.write(reinterpret_cast<const char*>(&count), sizeof(count));
            for (const auto& entry : entries) {
                // String format: length (uint16_t), data, rid details
                uint16_t len = entry.first.size();
                file.write(reinterpret_cast<const char*>(&len), sizeof(len));
                file.write(entry.first.data(), len);
                file.write(reinterpret_cast<const char*>(&entry.second.page_id), sizeof(uint32_t));
                file.write(reinterpret_cast<const char*>(&entry.second.slot_id), sizeof(uint16_t));
            }
        }
        file.close();
        return true;
    }
    
    bool LoadIndex(const std::string& table_name, const std::string& col_name, DataType type) {
        std::string key = GetIndexKey(table_name, col_name);
        std::string path = GetIndexPath(table_name, col_name);
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) {
            // Index file does not exist, initialize empty
            CreateIndex(table_name, col_name, type);
            return true;
        }
        
        CreateIndex(table_name, col_name, type);
        
        uint64_t count = 0;
        file.read(reinterpret_cast<char*>(&count), sizeof(count));
        if (file.gcount() != sizeof(count)) return false;
        
        if (type == DataType::INT) {
            auto it = int_indexes_.find(key);
            it->second->Clear();
            for (uint64_t i = 0; i < count; ++i) {
                int key_val = 0;
                uint32_t page_id = 0;
                uint16_t slot_id = 0;
                
                file.read(reinterpret_cast<char*>(&key_val), sizeof(int));
                file.read(reinterpret_cast<char*>(&page_id), sizeof(uint32_t));
                file.read(reinterpret_cast<char*>(&slot_id), sizeof(uint16_t));
                
                it->second->Insert(key_val, RecordID{page_id, slot_id});
            }
        } else {
            auto it = string_indexes_.find(key);
            it->second->Clear();
            for (uint64_t i = 0; i < count; ++i) {
                uint16_t len = 0;
                file.read(reinterpret_cast<char*>(&len), sizeof(len));
                std::vector<char> str_buf(len);
                file.read(str_buf.data(), len);
                std::string key_val(str_buf.begin(), str_buf.end());
                
                uint32_t page_id = 0;
                uint16_t slot_id = 0;
                file.read(reinterpret_cast<char*>(&page_id), sizeof(uint32_t));
                file.read(reinterpret_cast<char*>(&slot_id), sizeof(uint16_t));
                
                it->second->Insert(key_val, RecordID{page_id, slot_id});
            }
        }
        file.close();
        return true;
    }
};

#endif // INDEX_MANAGER_H
