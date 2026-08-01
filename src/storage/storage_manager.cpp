#include "storage_manager.h"
#include <fstream>
#include <iostream>
#include <sys/stat.h>

// Platform-independent utility to create directories.
#ifdef _WIN32
#include <direct.h>
#define mkdir_portable(dir) _mkdir(dir)
#else
#define mkdir_portable(dir) mkdir(dir, 0777)
#endif

std::string StorageManager::GetTablePath(const std::string& table_name) const {
    return data_dir_ + "/" + table_name + ".tbl";
}

StorageManager::StorageManager(const std::string& data_dir) : data_dir_(data_dir) {
    // Bootstrap directory creation to ensure database data folders exist
    mkdir_portable(data_dir_.c_str());
}

bool StorageManager::CreateTableFile(const std::string& table_name) {
    std::string path = GetTablePath(table_name);
    // Create an empty binary file by opening it in output mode
    std::ofstream file(path, std::ios::binary | std::ios::out);
    if (!file.is_open()) {
        std::cerr << "Failed to create table file: " << path << std::endl;
        return false;
    }
    file.close();
    return true;
}

uint32_t StorageManager::GetPageCount(const std::string& table_name) {
    std::string path = GetTablePath(table_name);
    // Open in binary mode and immediately seek to the end (std::ios::ate) to find file size
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        return 0;
    }
    std::streamsize size = file.tellg(); // Get size of the file in bytes
    file.close();
    return static_cast<uint32_t>(size / PAGE_SIZE); // Page count is total bytes / 4096
}

bool StorageManager::ReadPage(const std::string& table_name, uint32_t page_id, Page& out_page) {
    std::string path = GetTablePath(table_name);
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }
    
    // Seek to page offset position (page_id * 4096)
    file.seekg(page_id * PAGE_SIZE);
    file.read(out_page.data, PAGE_SIZE);
    bool success = file.gcount() == PAGE_SIZE; // Ensure a full page of 4096 bytes was read
    file.close();
    return success;
}

bool StorageManager::WritePage(const std::string& table_name, uint32_t page_id, const Page& page) {
    std::string path = GetTablePath(table_name);
    // Open file in read/write binary mode (in | out) to allow in-place overwriting without truncation
    std::fstream file(path, std::ios::binary | std::ios::in | std::ios::out);
    if (!file.is_open()) {
        // Fallback: If table file is new/empty, open in write-only mode
        file.open(path, std::ios::binary | std::ios::out);
        if (!file.is_open()) {
            return false;
        }
    }
    
    // Seek to target write position
    file.seekp(page_id * PAGE_SIZE);
    file.write(page.data, PAGE_SIZE);
    file.close();
    return true;
}

RecordID StorageManager::InsertRecord(const std::string& table_name, const char* record_data, uint16_t record_len) {
    uint32_t page_count = GetPageCount(table_name);
    
    if (page_count == 0) {
        // Table file is completely empty, initialize page 0
        Page page(0);
        int slot_id = page.InsertRecord(record_data, record_len);
        if (slot_id >= 0) {
            WritePage(table_name, 0, page);
            return RecordID{0, static_cast<uint16_t>(slot_id)};
        }
    } else {
        // Read the last page to check if it has space for this record
        uint32_t last_page_id = page_count - 1;
        Page page;
        if (ReadPage(table_name, last_page_id, page)) {
            int slot_id = page.InsertRecord(record_data, record_len);
            if (slot_id >= 0) {
                WritePage(table_name, last_page_id, page);
                return RecordID{last_page_id, static_cast<uint16_t>(slot_id)};
            }
        }
        
        // No space on the last page. Append a new page.
        Page new_page(page_count);
        int slot_id = new_page.InsertRecord(record_data, record_len);
        if (slot_id >= 0) {
            WritePage(table_name, page_count, new_page);
            return RecordID{page_count, static_cast<uint16_t>(slot_id)};
        }
    }
    
    return RecordID{0xFFFFFFFF, 0xFFFF}; // Error sentinel indicating insertion failed
}

bool StorageManager::GetRecord(const std::string& table_name, RecordID rid, std::vector<char>& out_record) {
    Page page;
    if (!ReadPage(table_name, rid.page_id, page)) {
        return false;
    }
    return page.GetRecord(rid.slot_id, out_record);
}

bool StorageManager::UpdateRecord(const std::string& table_name, RecordID rid, const char* record_data, uint16_t record_len) {
    Page page;
    if (!ReadPage(table_name, rid.page_id, page)) {
        return false;
    }
    
    // Try updating in-place inside the page
    if (page.UpdateRecord(rid.slot_id, record_data, record_len)) {
        return WritePage(table_name, rid.page_id, page);
    }
    
    // If update fails (e.g. record grew too large for the page), we return false.
    // Higher-level query executor catches this, deletes the old RecordID, and inserts
    // the updated record as a fresh insertion, dynamically rebuilding indexes.
    return false;
}

bool StorageManager::DeleteRecord(const std::string& table_name, RecordID rid) {
    Page page;
    if (!ReadPage(table_name, rid.page_id, page)) {
        return false;
    }
    if (page.DeleteRecord(rid.slot_id)) {
        return WritePage(table_name, rid.page_id, page);
    }
    return false;
}
