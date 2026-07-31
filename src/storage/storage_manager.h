#ifndef STORAGE_MANAGER_H
#define STORAGE_MANAGER_H

#include "page.h"
#include <string>
#include <vector>

class StorageManager {
private:
    std::string data_dir_;
    
    std::string GetTablePath(const std::string& table_name) const;
    
public:
    StorageManager(const std::string& data_dir);
    
    // Creates table file
    bool CreateTableFile(const std::string& table_name);
    
    // Inserts a record. If the last page has no space, appends a new page.
    RecordID InsertRecord(const std::string& table_name, const char* record_data, uint16_t record_len);
    
    // Gets record details
    bool GetRecord(const std::string& table_name, RecordID rid, std::vector<char>& out_record);
    
    // Updates a record
    bool UpdateRecord(const std::string& table_name, RecordID rid, const char* record_data, uint16_t record_len);
    
    // Deletes a record (logical delete)
    bool DeleteRecord(const std::string& table_name, RecordID rid);
    
    // Helper to get total number of pages in the table file
    uint32_t GetPageCount(const std::string& table_name);
    
    // Read a specific page from file
    bool ReadPage(const std::string& table_name, uint32_t page_id, Page& out_page);
    
    // Write a specific page to file
    bool WritePage(const std::string& table_name, uint32_t page_id, const Page& page);
};

#endif // STORAGE_MANAGER_H
