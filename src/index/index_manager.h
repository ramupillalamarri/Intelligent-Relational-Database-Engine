#ifndef INDEX_MANAGER_H
#define INDEX_MANAGER_H

#include "bplus_tree.h"
#include "catalog/catalog.h"
#include <string>
#include <unordered_map>
#include <memory>
#include <sstream>
#include <sys/stat.h>

// Platform-independent utility to create directories.
#ifdef _WIN32
#include <direct.h>
#define mkdir_portable(dir) _mkdir(dir)
#else
#define mkdir_portable(dir) mkdir(dir, 0777)
#endif

/**
 * @brief IndexManager coordinates index allocations, searches, insertions, and disk writes.
 * 
 * It manages active B+ trees in memory using hash maps, separating integer indexes from string indexes:
 * - `int_indexes_` maps table_name + "_" + col_name -> BPlusTree<int>
 * - `string_indexes_` maps table_name + "_" + col_name -> BPlusTree<std::string>
 */
class IndexManager {
private:
    std::string index_dir_; // Directory path where serialized index `.idx` binary files are stored
    
    // In-memory B+ Trees for integer-based columns
    std::unordered_map<std::string, std::unique_ptr<BPlusTree<int>>> int_indexes_;
    // In-memory B+ Trees for text-based columns
    std::unordered_map<std::string, std::unique_ptr<BPlusTree<std::string>>> string_indexes_;
    
    /**
     * @brief Generates a unique key representing the index (e.g. employees_id).
     */
    std::string GetIndexKey(const std::string& table_name, const std::string& col_name) const {
        return table_name + "_" + col_name;
    }
    
    /**
     * @brief Generates the filesystem path for the binary index file.
     */
    std::string GetIndexPath(const std::string& table_name, const std::string& col_name) const {
        return index_dir_ + "/" + table_name + "_" + col_name + ".idx";
    }
    
public:
    /**
     * @brief Construct a new IndexManager object.
     * @param index_dir Directory path where index files will be stored.
     */
    IndexManager(const std::string& index_dir) : index_dir_(index_dir) {
        mkdir_portable(index_dir_.c_str());
    }
    
    /**
     * @brief Instantiates a new BPlusTree in memory if it doesn't already exist.
     * @param table_name Name of the table.
     * @param col_name Name of the column.
     * @param type Data type of the column.
     * @return true Successfully created or index already loaded.
     */
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
    
    /**
     * @brief Resets/Clears all key-value entries in a specific index.
     */
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
    
    /**
     * @brief Inserts a key and its RecordID into the active B+ Tree.
     * @param table_name Name of the table.
     * @param col_name Name of the column.
     * @param type Data type of the column.
     * @param val_str The key value (passed as string; parsed if numeric).
     * @param rid The physical RecordID.
     */
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
    
    /**
     * @brief Searches the index to retrieve RecordIDs matching a specific key.
     * Runs in O(log N) lookup time complexity.
     * @return std::vector<RecordID> List of matching physical records.
     */
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
    
    /**
     * @brief Serializes the active B+ Tree in memory to a binary `.idx` file on disk.
     * @return true Serialization succeeded.
     * @return false Write failed.
     */
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
            
            // Format: count (8 bytes), followed by elements: key (4B) | page_id (4B) | slot_id (2B)
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
                // String format: length (2B), data bytes, followed by page_id (4B) | slot_id (2B)
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
    
    /**
     * @brief Deserializes a binary `.idx` file from disk back into the B+ Tree.
     * @return true Deserialization succeeded or initialized empty index.
     * @return false Load failed.
     */
    bool LoadIndex(const std::string& table_name, const std::string& col_name, DataType type) {
        std::string key = GetIndexKey(table_name, col_name);
        std::string path = GetIndexPath(table_name, col_name);
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) {
            // Index file does not exist yet; initialize an empty index structure in memory.
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
