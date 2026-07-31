#include "storage_manager.h"
#include <fstream>
#include <iostream>
#include <sys/stat.h>

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
    // Ensure data directory exists
    mkdir_portable(data_dir_.c_str());
}

bool StorageManager::CreateTableFile(const std::string& table_name) {
    std::string path = GetTablePath(table_name);
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
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        return 0;
    }
    std::streamsize size = file.tellg();
    file.close();
    return static_cast<uint32_t>(size / PAGE_SIZE);
}

bool StorageManager::ReadPage(const std::string& table_name, uint32_t page_id, Page& out_page) {
    std::string path = GetTablePath(table_name);
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }
    
    file.seekg(page_id * PAGE_SIZE);
    file.read(out_page.data, PAGE_SIZE);
    bool success = file.gcount() == PAGE_SIZE;
    file.close();
    return success;
}

bool StorageManager::WritePage(const std::string& table_name, uint32_t page_id, const Page& page) {
    std::string path = GetTablePath(table_name);
    std::fstream file(path, std::ios::binary | std::ios::in | std::ios::out);
    if (!file.is_open()) {
        // If file doesn't exist for read/write, open in out mode only
        file.open(path, std::ios::binary | std::ios::out);
        if (!file.is_open()) {
            return false;
        }
    }
    
    file.seekp(page_id * PAGE_SIZE);
    file.write(page.data, PAGE_SIZE);
    file.close();
    return true;
}

RecordID StorageManager::InsertRecord(const std::string& table_name, const char* record_data, uint16_t record_len) {
    uint32_t page_count = GetPageCount(table_name);
    
    if (page_count == 0) {
        Page page(0);
        int slot_id = page.InsertRecord(record_data, record_len);
        if (slot_id >= 0) {
            WritePage(table_name, 0, page);
            return RecordID{0, static_cast<uint16_t>(slot_id)};
        }
    } else {
        uint32_t last_page_id = page_count - 1;
        Page page;
        if (ReadPage(table_name, last_page_id, page)) {
            int slot_id = page.InsertRecord(record_data, record_len);
            if (slot_id >= 0) {
                WritePage(table_name, last_page_id, page);
                return RecordID{last_page_id, static_cast<uint16_t>(slot_id)};
            }
        }
        
        // No space on last page, create new page
        Page new_page(page_count);
        int slot_id = new_page.InsertRecord(record_data, record_len);
        if (slot_id >= 0) {
            WritePage(table_name, page_count, new_page);
            return RecordID{page_count, static_cast<uint16_t>(slot_id)};
        }
    }
    
    return RecordID{0xFFFFFFFF, 0xFFFF}; // Error
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
    
    if (page.UpdateRecord(rid.slot_id, record_data, record_len)) {
        return WritePage(table_name, rid.page_id, page);
    }
    
    // If update failed due to space, we logical delete from current page,
    // and insert into the last page (relocate record).
    // Note: To keep B+ tree pointers and rid consistent, a real engine would use forwarding pointers,
    // or return false and require the higher layers to update indices. Let's return false and
    // let our Execution Engine handle updates by deleting and re-inserting, which is a common and
    // very clean implementation.
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
