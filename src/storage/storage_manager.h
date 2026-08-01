#ifndef STORAGE_MANAGER_H
#define STORAGE_MANAGER_H

#include "page.h"
#include <string>
#include <vector>

/**
 * @brief StorageManager handles physical reading and writing of database tables on disk.
 * 
 * Each table is stored as a single binary file (table_name.tbl) in the data directory.
 * The file is structured as a sequence of fixed-size physical pages (each PAGE_SIZE = 4096 bytes).
 */
class StorageManager {
private:
    std::string data_dir_;  // Directory path where binary database table files reside
    
    /**
     * @brief Generates the filesystem path for a table's data file.
     * @param table_name Name of the database table.
     * @return std::string Path to file (e.g., data/table_name.tbl).
     */
    std::string GetTablePath(const std::string& table_name) const;
    
public:
    /**
     * @brief Construct a new StorageManager object.
     * @param data_dir Path to directory where table files will be saved.
     */
    StorageManager(const std::string& data_dir);
    
    /**
     * @brief Creates an empty binary table file on disk.
     * @param table_name Name of the table.
     * @return true If the table file was created successfully.
     * @return false If file creation failed.
     */
    bool CreateTableFile(const std::string& table_name);
    
    /**
     * @brief Inserts a record byte array payload into the table.
     * 
     * Reads the last page of the table. If space is available, writes the record.
     * If the page is full or no page exists, it allocates a new physical page on disk.
     * 
     * @param table_name Name of the table to insert into.
     * @param record_data Pointer to the raw record byte payload.
     * @param record_len Length of the record in bytes.
     * @return RecordID Physical position (PageID, SlotID) where the record was inserted.
     */
    RecordID InsertRecord(const std::string& table_name, const char* record_data, uint16_t record_len);
    
    /**
     * @brief Retrieves a record payload from disk by its physical RecordID.
     * @param table_name Name of the table.
     * @param rid The RecordID (PageID + SlotID).
     * @param out_record Vector where the fetched bytes will be stored.
     * @return true If fetched successfully.
     * @return false If the record is deleted or read failed.
     */
    bool GetRecord(const std::string& table_name, RecordID rid, std::vector<char>& out_record);
    
    /**
     * @brief Updates a record payload at the specified RecordID.
     * @param table_name Name of the table.
     * @param rid The RecordID of the record to update.
     * @param record_data Pointer to the updated record bytes.
     * @param record_len Updated record length in bytes.
     * @return true If the update succeeded in-place.
     * @return false If the record grew too large and must be handled by relocation (deletion and insertion).
     */
    bool UpdateRecord(const std::string& table_name, RecordID rid, const char* record_data, uint16_t record_len);
    
    /**
     * @brief Deletes a record from disk by marking its slot as logically deleted.
     * @param table_name Name of the table.
     * @param rid The RecordID to delete.
     * @return true If marked as deleted successfully.
     * @return false If delete failed.
     */
    bool DeleteRecord(const std::string& table_name, RecordID rid);
    
    /**
     * @brief Calculates the total number of physical pages in a table's disk file.
     * @param table_name Name of the table.
     * @return uint32_t Number of pages.
     */
    uint32_t GetPageCount(const std::string& table_name);
    
    /**
     * @brief Reads a 4KB chunk of binary data from disk into a Page object buffer.
     * @param table_name Name of the table file.
     * @param page_id The physical page number (offset = page_id * 4096).
     * @param out_page The target Page object buffer.
     * @return true If read succeeded.
     * @return false If file read failed or page_id is out of bounds.
     */
    bool ReadPage(const std::string& table_name, uint32_t page_id, Page& out_page);
    
    /**
     * @brief Writes a 4KB Page object buffer back to disk.
     * @param table_name Name of the table file.
     * @param page_id The physical page number to overwrite or append.
     * @param page The Page object containing buffer data.
     * @return true If write succeeded.
     * @return false If file write failed.
     */
    bool WritePage(const std::string& table_name, uint32_t page_id, const Page& page);
};

#endif // STORAGE_MANAGER_H
