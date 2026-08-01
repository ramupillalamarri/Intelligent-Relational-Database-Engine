#ifndef PAGE_H
#define PAGE_H

#include <cstdint>
#include <cstring>
#include <vector>
#include <iostream>

// Standard size of a single physical database page (4KB).
// Aligns with typical OS filesystem block size to optimize disk I/O operations.
const uint16_t PAGE_SIZE = 4096;

/**
 * @brief RecordID uniquely identifies a record's physical position in the database.
 * It consists of a Page ID (which page file) and a Slot ID (which slot index inside that page).
 */
struct RecordID {
    uint32_t page_id;  // The physical block/page number in the table file
    uint16_t slot_id;  // The index position in the page's slot array
    
    // Equality operator to check if two record pointers point to the exact same database row.
    bool operator==(const RecordID& other) const {
        return page_id == other.page_id && slot_id == other.slot_id;
    }
};

/**
 * @brief Represents a physical slotted page stored on disk or loaded in memory.
 * 
 * Slotted Page Layout:
 * +------------------+------------------+-----------------------+-------------------+
 * | PageHeader       | Slot Array       | ..... Free Space .... | Records           |
 * | (page_id, count, | (offset, length) |                       | (grow backwards)  |
 * |  free_space_off) |                  |                       |                   |
 * +------------------+------------------+-----------------------+-------------------+
 * 
 * PageHeader size: 8 bytes
 * - Page ID: 4 bytes (offset 0)
 * - Record Count: 2 bytes (offset 4)
 * - Free Space Offset: 2 bytes (offset 6) - points to the start of the last record written.
 * 
 * Slot size: 4 bytes (2 bytes offset, 2 bytes length)
 * - Grows forwards from offset 8.
 * Records:
 * - Grow backwards from the end of the page (PAGE_SIZE = 4096).
 */
class Page {
public:
    // Raw binary byte buffer of the 4KB page.
    char data[PAGE_SIZE];
    
    /**
     * @brief Construct a new Page object with a given ID.
     */
    Page(uint32_t page_id = 0) {
        Reset(page_id);
    }
    
    /**
     * @brief Resets/Initializes the page layout with zeroed data and header meta.
     * @param page_id The physical page number to allocate.
     */
    void Reset(uint32_t page_id) {
        std::memset(data, 0, PAGE_SIZE);
        SetPageID(page_id);
        SetRecordCount(0);
        SetFreeSpaceOffset(PAGE_SIZE); // Free space starts at the end of the page (4096)
    }
    
    /**
     * @brief Reads the Page ID from the first 4 bytes of the header.
     * @return uint32_t The ID of this page.
     */
    uint32_t GetPageID() const {
        uint32_t val;
        std::memcpy(&val, data, sizeof(uint32_t));
        return val;
    }
    
    /**
     * @brief Writes the Page ID into the first 4 bytes of the header.
     * @param val The Page ID to set.
     */
    void SetPageID(uint32_t val) {
        std::memcpy(data, &val, sizeof(uint32_t));
    }
    
    /**
     * @brief Gets the number of records (slots) currently allocated on this page.
     * @return uint16_t The record count.
     */
    uint16_t GetRecordCount() const {
        uint16_t val;
        std::memcpy(&val, data + 4, sizeof(uint16_t));
        return val;
    }
    
    /**
     * @brief Sets the number of records (slots) allocated on this page.
     * @param val The count to set.
     */
    void SetRecordCount(uint16_t val) {
        std::memcpy(data + 4, &val, sizeof(uint16_t));
    }
    
    /**
     * @brief Gets the offset indicating the boundary where the free space ends and records begin.
     * @return uint16_t The byte offset in the page.
     */
    uint16_t GetFreeSpaceOffset() const {
        uint16_t val;
        std::memcpy(&val, data + 6, sizeof(uint16_t));
        return val;
    }
    
    /**
     * @brief Sets the offset boundary where the free space ends and records begin.
     * @param val The byte offset to set.
     */
    void SetFreeSpaceOffset(uint16_t val) {
        std::memcpy(data + 6, &val, sizeof(uint16_t));
    }
    
    /**
     * @brief Calculates the contiguous free space left in the middle of the page.
     * Contiguous Free Space = Free Space Offset - End of the Slot Array.
     * @return uint16_t The amount of free space in bytes.
     */
    uint16_t GetFreeSpaceAmount() const {
        uint16_t count = GetRecordCount();
        uint16_t slot_array_end = 8 + count * 4; // 8 bytes header + 4 bytes per slot
        uint16_t free_offset = GetFreeSpaceOffset();
        if (free_offset < slot_array_end) return 0;
        return free_offset - slot_array_end;
    }
    
    /**
     * @brief Inserts a serialized row record payload into the page.
     * Grows the slot array forward, and writes the record payload growing backward.
     * @param record_data Pointer to the raw record byte array.
     * @param record_len The length of the record in bytes.
     * @return int The assigned slot_id (index) or -1 if the page is full.
     */
    int InsertRecord(const char* record_data, uint16_t record_len) {
        uint16_t count = GetRecordCount();
        uint16_t needed_space = record_len + 4; // record length + 4 bytes for its slot entry
        
        if (GetFreeSpaceAmount() < needed_space) {
            return -1; // Page does not have enough free space
        }
        
        uint16_t free_offset = GetFreeSpaceOffset();
        uint16_t new_record_offset = free_offset - record_len; // Grows backwards
        
        // Write the raw record byte payload to page memory
        std::memcpy(data + new_record_offset, record_data, record_len);
        
        // Write the slot meta (offset and length) in the header slot array
        uint16_t slot_offset_pos = 8 + count * 4;
        std::memcpy(data + slot_offset_pos, &new_record_offset, sizeof(uint16_t));
        std::memcpy(data + slot_offset_pos + 2, &record_len, sizeof(uint16_t));
        
        // Update header fields
        SetRecordCount(count + 1);
        SetFreeSpaceOffset(new_record_offset);
        
        return count; // Returns the slot_id index
    }
    
    /**
     * @brief Fetches a record payload from a specific slot.
     * @param slot_id The slot index within the page.
     * @param out_record Reference to vector where the record bytes will be copied.
     * @return true If the record was successfully fetched.
     * @return false If the record is logically deleted or slot_id is invalid.
     */
    bool GetRecord(uint16_t slot_id, std::vector<char>& out_record) const {
        uint16_t count = GetRecordCount();
        if (slot_id >= count) return false;
        
        // Read slot offset and length
        uint16_t slot_offset_pos = 8 + slot_id * 4;
        uint16_t offset, length;
        std::memcpy(&offset, data + slot_offset_pos, sizeof(uint16_t));
        std::memcpy(&length, data + slot_offset_pos + 2, sizeof(uint16_t));
        
        // Offset and length of 0 designates a logically deleted record
        if (offset == 0 && length == 0) {
            return false;
        }
        
        out_record.resize(length);
        std::memcpy(out_record.data(), data + offset, length);
        return true;
    }
    
    /**
     * @brief Logically deletes a record by setting its slot pointers to 0.
     * Defragmentation/compaction is deferred to keep operations O(1).
     * @param slot_id The slot index to delete.
     * @return true If successfully marked as deleted.
     * @return false If slot_id is out of bounds.
     */
    bool DeleteRecord(uint16_t slot_id) {
        uint16_t count = GetRecordCount();
        if (slot_id >= count) return false;
        
        uint16_t slot_offset_pos = 8 + slot_id * 4;
        uint16_t zero = 0;
        std::memcpy(data + slot_offset_pos, &zero, sizeof(uint16_t));
        std::memcpy(data + slot_offset_pos + 2, &zero, sizeof(uint16_t));
        return true;
    }
    
    /**
     * @brief Updates the record payload inside a specific slot.
     * 
     * If the new record size is <= old size, we overwrite in-place.
     * If it is larger, we perform a logical delete on the old location and
     * write the updated record to the end of free space (if enough space remains).
     * 
     * @param slot_id The slot index to update.
     * @param record_data Pointer to the updated record byte array.
     * @param record_len The updated record length.
     * @return true If updated successfully.
     * @return false If the record is deleted or there is insufficient space on the page.
     */
    bool UpdateRecord(uint16_t slot_id, const char* record_data, uint16_t record_len) {
        uint16_t count = GetRecordCount();
        if (slot_id >= count) return false;
        
        uint16_t slot_offset_pos = 8 + slot_id * 4;
        uint16_t offset, length;
        std::memcpy(&offset, data + slot_offset_pos, sizeof(uint16_t));
        std::memcpy(&length, data + slot_offset_pos + 2, sizeof(uint16_t));
        
        if (offset == 0 && length == 0) return false; // Already deleted
        
        if (record_len <= length) {
            // Optimization: Overwrite in place since the payload fits in the original slot
            std::memcpy(data + offset, record_data, record_len);
            std::memcpy(data + slot_offset_pos + 2, &record_len, sizeof(uint16_t));
            return true;
        } else {
            // Relocation is required. Check if there is enough space left on this page.
            if (GetFreeSpaceAmount() < record_len) {
                return false; // Insufficient page space, record must relocate to another page
            }
            
            // Allocate record from the end of free space
            uint16_t free_offset = GetFreeSpaceOffset();
            uint16_t new_record_offset = free_offset - record_len;
            
            // Write record payload
            std::memcpy(data + new_record_offset, record_data, record_len);
            
            // Update slot pointer to target the new page offset
            std::memcpy(data + slot_offset_pos, &new_record_offset, sizeof(uint16_t));
            std::memcpy(data + slot_offset_pos + 2, &record_len, sizeof(uint16_t));
            
            // Update free space offset
            SetFreeSpaceOffset(new_record_offset);
            return true;
        }
    }
};

#endif // PAGE_H
