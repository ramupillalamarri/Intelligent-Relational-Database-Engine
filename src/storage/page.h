#ifndef PAGE_H
#define PAGE_H

#include <cstdint>
#include <cstring>
#include <vector>
#include <iostream>

const uint16_t PAGE_SIZE = 4096;

struct RecordID {
    uint32_t page_id;
    uint16_t slot_id;
    
    bool operator==(const RecordID& other) const {
        return page_id == other.page_id && slot_id == other.slot_id;
    }
};

// Slotted Page Layout:
// +------------------+------------------+-----------------------+-------------------+
// | PageHeader       | Slot Array       | ..... Free Space .... | Records           |
// | (page_id, count, | (offset, length) |                       | (grow backwards)  |
// |  free_space_off) |                  |                       |                   |
// +------------------+------------------+-----------------------+-------------------+
//
// PageHeader size: 8 bytes
// Slot size: 4 bytes (2 bytes offset, 2 bytes length)

class Page {
public:
    char data[PAGE_SIZE];
    
    Page(uint32_t page_id = 0) {
        Reset(page_id);
    }
    
    void Reset(uint32_t page_id) {
        std::memset(data, 0, PAGE_SIZE);
        SetPageID(page_id);
        SetRecordCount(0);
        SetFreeSpaceOffset(PAGE_SIZE);
    }
    
    uint32_t GetPageID() const {
        uint32_t val;
        std::memcpy(&val, data, sizeof(uint32_t));
        return val;
    }
    
    void SetPageID(uint32_t val) {
        std::memcpy(data, &val, sizeof(uint32_t));
    }
    
    uint16_t GetRecordCount() const {
        uint16_t val;
        std::memcpy(&val, data + 4, sizeof(uint16_t));
        return val;
    }
    
    void SetRecordCount(uint16_t val) {
        std::memcpy(data + 4, &val, sizeof(uint16_t));
    }
    
    uint16_t GetFreeSpaceOffset() const {
        uint16_t val;
        std::memcpy(&val, data + 6, sizeof(uint16_t));
        return val;
    }
    
    void SetFreeSpaceOffset(uint16_t val) {
        std::memcpy(data + 6, &val, sizeof(uint16_t));
    }
    
    uint16_t GetFreeSpaceAmount() const {
        uint16_t count = GetRecordCount();
        uint16_t slot_array_end = 8 + count * 4;
        uint16_t free_offset = GetFreeSpaceOffset();
        if (free_offset < slot_array_end) return 0;
        return free_offset - slot_array_end;
    }
    
    // Inserts a record. Returns slot_id or -1 if no space.
    int InsertRecord(const char* record_data, uint16_t record_len) {
        uint16_t count = GetRecordCount();
        uint16_t needed_space = record_len + 4; // record length + 4 bytes slot
        
        if (GetFreeSpaceAmount() < needed_space) {
            return -1; // Page full
        }
        
        uint16_t free_offset = GetFreeSpaceOffset();
        uint16_t new_record_offset = free_offset - record_len;
        
        // Write record
        std::memcpy(data + new_record_offset, record_data, record_len);
        
        // Write slot
        uint16_t slot_offset_pos = 8 + count * 4;
        std::memcpy(data + slot_offset_pos, &new_record_offset, sizeof(uint16_t));
        std::memcpy(data + slot_offset_pos + 2, &record_len, sizeof(uint16_t));
        
        // Update header
        SetRecordCount(count + 1);
        SetFreeSpaceOffset(new_record_offset);
        
        return count; // slot_id
    }
    
    // Gets a record. Returns false if deleted or invalid slot_id.
    bool GetRecord(uint16_t slot_id, std::vector<char>& out_record) const {
        uint16_t count = GetRecordCount();
        if (slot_id >= count) return false;
        
        uint16_t slot_offset_pos = 8 + slot_id * 4;
        uint16_t offset, length;
        std::memcpy(&offset, data + slot_offset_pos, sizeof(uint16_t));
        std::memcpy(&length, data + slot_offset_pos + 2, sizeof(uint16_t));
        
        if (offset == 0 && length == 0) {
            return false; // Deleted record
        }
        
        out_record.resize(length);
        std::memcpy(out_record.data(), data + offset, length);
        return true;
    }
    
    // Deletes a record by setting slot offset/length to 0 (logical delete).
    bool DeleteRecord(uint16_t slot_id) {
        uint16_t count = GetRecordCount();
        if (slot_id >= count) return false;
        
        uint16_t slot_offset_pos = 8 + slot_id * 4;
        uint16_t zero = 0;
        std::memcpy(data + slot_offset_pos, &zero, sizeof(uint16_t));
        std::memcpy(data + slot_offset_pos + 2, &zero, sizeof(uint16_t));
        // Note: For simplicity, we don't perform page defragmentation (compaction) immediately.
        return true;
    }
    
    // Updates a record. If new length <= old length, we overwrite in place.
    // If not, we logical-delete and insert at the end of free space (if there is room).
    bool UpdateRecord(uint16_t slot_id, const char* record_data, uint16_t record_len) {
        uint16_t count = GetRecordCount();
        if (slot_id >= count) return false;
        
        uint16_t slot_offset_pos = 8 + slot_id * 4;
        uint16_t offset, length;
        std::memcpy(&offset, data + slot_offset_pos, sizeof(uint16_t));
        std::memcpy(&length, data + slot_offset_pos + 2, sizeof(uint16_t));
        
        if (offset == 0 && length == 0) return false; // Already deleted
        
        if (record_len <= length) {
            // Overwrite in place
            std::memcpy(data + offset, record_data, record_len);
            // Update length in slot
            std::memcpy(data + slot_offset_pos + 2, &record_len, sizeof(uint16_t));
            return true;
        } else {
            // Check if there is enough free space for the expansion
            if (GetFreeSpaceAmount() < record_len) {
                return false; // No space on page
            }
            
            // Mark old slot as free (logical delete) but we will keep slot_id
            uint16_t free_offset = GetFreeSpaceOffset();
            uint16_t new_record_offset = free_offset - record_len;
            
            // Write record
            std::memcpy(data + new_record_offset, record_data, record_len);
            
            // Update slot pointer to new location
            std::memcpy(data + slot_offset_pos, &new_record_offset, sizeof(uint16_t));
            std::memcpy(data + slot_offset_pos + 2, &record_len, sizeof(uint16_t));
            
            // Update free space offset
            SetFreeSpaceOffset(new_record_offset);
            return true;
        }
    }
};

#endif // PAGE_H
