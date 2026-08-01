#ifndef EXECUTOR_H
#define EXECUTOR_H

#include "storage/storage_manager.h"
#include "index/index_manager.h"
#include "planner/planner.h"
#include "utils/serialization.h"
#include <vector>
#include <string>
#include <memory>
#include <algorithm>
#include <stdexcept>
#include <iostream>

/**
 * @brief Represents a single row tuple output containing string values.
 */
struct Tuple {
    std::vector<std::string> values;
};

/**
 * @brief Abstract base class for all query execution operators.
 * 
 * Implements the **Volcano Iterator Model**.
 * All physical query operators execute pipeline-style by pulling records
 * one-by-one rather than reading complete table rows into RAM.
 */
class AbstractExecutor {
public:
    virtual ~AbstractExecutor() = default;
    
    /**
     * @brief Initializes execution states, open cursors, or reset pointers.
     */
    virtual void Init() = 0;
    
    /**
     * @brief Pulls the next record tuple in the pipeline.
     * @param tuple Output target reference where values are copied.
     * @return true If a row was successfully fetched.
     * @return false If the record stream has finished.
     */
    virtual bool Next(Tuple* tuple) = 0;
    
    /**
     * @brief Closes resources and releases file handles.
     */
    virtual void Close() = 0;
    
    /**
     * @brief Gets the list of column names produced by this operator.
     */
    virtual const std::vector<std::string>& GetOutputColumns() const = 0;
};

/**
 * @brief SeqScanExecutor executes a sequential table scan.
 * It reads page by page, slot by slot, from the table's disk file.
 */
class SeqScanExecutor : public AbstractExecutor {
private:
    StorageManager& sm_;           // Reference to disk storage manager
    std::string table_name_;       // Target table name
    Schema schema_;                // Layout schema of the table
    std::vector<std::string> out_cols_; // Cached output column names
    
    uint32_t page_count_ = 0;      // Total pages inside table file
    uint32_t curr_page_id_ = 0;    // Current page cursor
    uint16_t curr_slot_id_ = 0;    // Current slot cursor in the active page
    Page curr_page_;               // Physical page cache buffer
    bool page_loaded_ = false;     // State tracking page buffer initialization
    
public:
    SeqScanExecutor(StorageManager& sm, const std::string& table_name, const Schema& schema)
        : sm_(sm), table_name_(table_name), schema_(schema) {
        for (const auto& col : schema_.columns) {
            out_cols_.push_back(col.name);
        }
    }
    
    void Init() override {
        page_count_ = sm_.GetPageCount(table_name_);
        curr_page_id_ = 0;
        curr_slot_id_ = 0;
        page_loaded_ = false;
    }
    
    bool Next(Tuple* tuple) override {
        if (page_count_ == 0) return false;
        
        // Loop through all physical pages in the table file
        while (curr_page_id_ < page_count_) {
            if (!page_loaded_) {
                if (!sm_.ReadPage(table_name_, curr_page_id_, curr_page_)) {
                    curr_page_id_++;
                    curr_slot_id_ = 0;
                    continue;
                }
                page_loaded_ = true;
            }
            
            // Loop through all slot records inside the active page
            uint16_t record_count = curr_page_.GetRecordCount();
            while (curr_slot_id_ < record_count) {
                std::vector<char> record_data;
                if (curr_page_.GetRecord(curr_slot_id_, record_data)) {
                    // Deserialize binary page bytes into human-readable strings
                    tuple->values = DeserializeRow(schema_, record_data);
                    curr_slot_id_++;
                    return true; // Yield a single record up the pipeline
                }
                curr_slot_id_++; // Skip logically deleted slots
            }
            
            // Move cursor to the next physical page
            curr_page_id_++;
            curr_slot_id_ = 0;
            page_loaded_ = false;
        }
        return false; // End of table reached
    }
    
    void Close() override {}
    
    const std::vector<std::string>& GetOutputColumns() const override {
        return out_cols_;
    }
};

/**
 * @brief IndexScanExecutor scans tables via a B+ Tree Index.
 * It executes in logarithmic O(log N) search complexity instead of linear O(N).
 */
class IndexScanExecutor : public AbstractExecutor {
private:
    StorageManager& sm_;           // Storage manager to read pages
    IndexManager& im_;             // Index manager to search trees
    std::string table_name_;       // Target table
    std::string col_name_;         // Indexed column target
    DataType key_type_;            // Key type (INT or TEXT)
    std::string search_val_;       // Target value to lookup
    Schema schema_;                // Layout schema of the table
    std::vector<std::string> out_cols_;
    
    std::vector<RecordID> rids_;   // List of physical RecordIDs retrieved from B+ Tree
    size_t rid_idx_ = 0;           // RecordID list cursor pointer
    
public:
    IndexScanExecutor(StorageManager& sm, IndexManager& im, const std::string& table_name,
                      const std::string& col_name, DataType key_type, const std::string& search_val, const Schema& schema)
        : sm_(sm), im_(im), table_name_(table_name), col_name_(col_name), key_type_(key_type), search_val_(search_val), schema_(schema) {
        for (const auto& col : schema_.columns) {
            out_cols_.push_back(col.name);
        }
    }
    
    void Init() override {
        // Query the B+ tree in memory to get physical page/slot locations
        rids_ = im_.Search(table_name_, col_name_, key_type_, search_val_);
        rid_idx_ = 0;
    }
    
    bool Next(Tuple* tuple) override {
        // Sequentially load pages and fetch records by their direct RIDs
        while (rid_idx_ < rids_.size()) {
            RecordID rid = rids_[rid_idx_++];
            std::vector<char> record_data;
            if (sm_.GetRecord(table_name_, rid, record_data)) {
                tuple->values = DeserializeRow(schema_, record_data);
                return true; // Yield record
            }
        }
        return false;
    }
    
    void Close() override {}
    
    const std::vector<std::string>& GetOutputColumns() const override {
        return out_cols_;
    }
};

/**
 * @brief FilterExecutor evaluates a WHERE expression.
 * It filters rows yielded by its child operator.
 */
class FilterExecutor : public AbstractExecutor {
private:
    std::unique_ptr<AbstractExecutor> child_; // Child operator (e.g. SeqScan)
    WhereCondition cond_;                     // Evaluating clause
    std::vector<std::string> out_cols_;
    int col_idx_ = -1;                        // Column index inside tuples
    DataType col_type_;                       // Data type of the filtering column
    
    /**
     * @brief Performs standard comparison operations based on data types.
     */
    bool EvaluateCondition(const std::string& actual_val) {
        if (col_type_ == DataType::INT) {
            int actual = 0;
            int expected = 0;
            try {
                actual = std::stoi(actual_val);
                expected = std::stoi(cond_.value);
            } catch (...) {}
            
            switch (cond_.op) {
                case TokenType::EQUAL: return actual == expected;
                case TokenType::GREATER: return actual > expected;
                case TokenType::LESS: return actual < expected;
                case TokenType::GREATER_EQUAL: return actual >= expected;
                case TokenType::LESS_EQUAL: return actual <= expected;
                case TokenType::NOT_EQUAL: return actual != expected;
                default: return false;
            }
        } else {
            switch (cond_.op) {
                case TokenType::EQUAL: return actual_val == cond_.value;
                case TokenType::GREATER: return actual_val > cond_.value;
                case TokenType::LESS: return actual_val < cond_.value;
                case TokenType::GREATER_EQUAL: return actual_val >= cond_.value;
                case TokenType::LESS_EQUAL: return actual_val <= cond_.value;
                case TokenType::NOT_EQUAL: return actual_val != cond_.value;
                default: return false;
            }
        }
    }
    
public:
    FilterExecutor(std::unique_ptr<AbstractExecutor> child, WhereCondition cond, const Schema& schema)
        : child_(std::move(child)), cond_(cond) {
        out_cols_ = child_->GetOutputColumns();
        
        // Find position of the filter target column in schema
        for (size_t i = 0; i < schema.columns.size(); ++i) {
            if (schema.columns[i].name == cond_.col_name) {
                col_idx_ = static_cast<int>(i);
                col_type_ = schema.columns[i].type;
                break;
            }
        }
        
        // Handle join column naming variations (e.g. prefix resolution)
        if (col_idx_ == -1) {
            for (size_t i = 0; i < out_cols_.size(); ++i) {
                if (out_cols_[i] == cond_.col_name) {
                    col_idx_ = static_cast<int>(i);
                    col_type_ = DataType::TEXT;
                    break;
                }
            }
        }
    }
    
    void Init() override {
        child_->Init();
    }
    
    bool Next(Tuple* tuple) override {
        // Keep pulling rows from child operator until one passes the filter condition
        while (child_->Next(tuple)) {
            if (col_idx_ != -1 && col_idx_ < static_cast<int>(tuple->values.size())) {
                if (EvaluateCondition(tuple->values[col_idx_])) {
                    return true;
                }
            }
        }
        return false;
    }
    
    void Close() override {
        child_->Close();
    }
    
    const std::vector<std::string>& GetOutputColumns() const override {
        return out_cols_;
    }
};

/**
 * @brief ProjectExecutor restricts the fields yielded by the query.
 * Maps projected columns to indexes in child output lists.
 */
class ProjectExecutor : public AbstractExecutor {
private:
    std::unique_ptr<AbstractExecutor> child_;
    std::vector<std::string> select_cols_; // Columns requested by project
    std::vector<int> col_indices_;         // Target index mapping
    
public:
    ProjectExecutor(std::unique_ptr<AbstractExecutor> child, const std::vector<std::string>& select_cols)
        : child_(std::move(child)), select_cols_(select_cols) {
        const auto& child_cols = child_->GetOutputColumns();
        
        // Wildcard '*' expands to include all child columns
        if (select_cols_.size() == 1 && select_cols_[0] == "*") {
            select_cols_ = child_cols;
        }
        
        for (const auto& sel : select_cols_) {
            int found_idx = -1;
            for (size_t i = 0; i < child_cols.size(); ++i) {
                if (child_cols[i] == sel) {
                    found_idx = static_cast<int>(i);
                    break;
                }
            }
            col_indices_.push_back(found_idx);
        }
    }
    
    void Init() override {
        child_->Init();
    }
    
    bool Next(Tuple* tuple) override {
        Tuple child_tuple;
        if (child_->Next(&child_tuple)) {
            tuple->values.clear();
            // Re-order and select columns based on mapped indices
            for (int idx : col_indices_) {
                if (idx != -1 && idx < static_cast<int>(child_tuple.values.size())) {
                    tuple->values.push_back(child_tuple.values[idx]);
                } else {
                    tuple->values.push_back("NULL");
                }
            }
            return true;
        }
        return false;
    }
    
    void Close() override {
        child_->Close();
    }
    
    const std::vector<std::string>& GetOutputColumns() const override {
        return select_cols_;
    }
};

/**
 * @brief NestedLoopJoinExecutor executes inner relational joins.
 * It matches rows between outer and inner child operators.
 */
class NestedLoopJoinExecutor : public AbstractExecutor {
private:
    std::unique_ptr<AbstractExecutor> left_child_;  // Outer table operator
    std::unique_ptr<AbstractExecutor> right_child_; // Inner table operator
    std::string left_col_;                          // Joined column key (left side)
    std::string right_col_;                         // Joined column key (right side)
    std::vector<std::string> out_cols_;
    
    int left_col_idx_ = -1;
    int right_col_idx_ = -1;
    
    Tuple curr_left_tuple_;                         // Cache of the active outer row
    bool has_left_tuple_ = false;                   // State tracking outer row presence
    
public:
    NestedLoopJoinExecutor(std::unique_ptr<AbstractExecutor> left_child,
                           std::unique_ptr<AbstractExecutor> right_child,
                           const std::string& left_col, const std::string& right_col)
        : left_child_(std::move(left_child)), right_child_(std::move(right_child)),
          left_col_(left_col), right_col_(right_col) {
          
        out_cols_ = left_child_->GetOutputColumns();
        const auto& right_cols = right_child_->GetOutputColumns();
        out_cols_.insert(out_cols_.end(), right_cols.begin(), right_cols.end());
        
        // Find position of join keys inside child outputs
        const auto& left_out = left_child_->GetOutputColumns();
        for (size_t i = 0; i < left_out.size(); ++i) {
            if (left_out[i] == left_col_ || left_out[i] == left_col_.substr(left_col_.find('.') + 1)) {
                left_col_idx_ = static_cast<int>(i);
                break;
            }
        }
        
        for (size_t i = 0; i < right_cols.size(); ++i) {
            if (right_cols[i] == right_col_ || right_cols[i] == right_col_.substr(right_col_.find('.') + 1)) {
                right_col_idx_ = static_cast<int>(i);
                break;
            }
        }
    }
    
    void Init() override {
        left_child_->Init();
        right_child_->Init();
        has_left_tuple_ = false;
    }
    
    bool Next(Tuple* tuple) override {
        Tuple right_tuple;
        while (true) {
            // If we don't have an active outer row, fetch one and reset the inner table scan
            if (!has_left_tuple_) {
                if (!left_child_->Next(&curr_left_tuple_)) {
                    return false; // Outer table finished; join complete
                }
                has_left_tuple_ = true;
                right_child_->Init(); // Reset inner table scan cursor
            }
            
            // Loop through the inner table to find rows matching the outer row key
            if (right_child_->Next(&right_tuple)) {
                if (left_col_idx_ != -1 && right_col_idx_ != -1) {
                    const std::string& left_val = curr_left_tuple_.values[left_col_idx_];
                    const std::string& right_val = right_tuple.values[right_col_idx_];
                    if (left_val == right_val) {
                        // Match found: Merge fields and yield combined tuple
                        tuple->values = curr_left_tuple_.values;
                        tuple->values.insert(tuple->values.end(), right_tuple.values.begin(), right_tuple.values.end());
                        return true;
                    }
                }
            } else {
                // Inner table scan finished for this outer row; trigger next outer row load
                has_left_tuple_ = false;
            }
        }
    }
    
    void Close() override {
        left_child_->Close();
        right_child_->Close();
    }
    
    const std::vector<std::string>& GetOutputColumns() const override {
        return out_cols_;
    }
};

/**
 * @brief SortExecutor sorts rows yielded by its child.
 * 
 * Note: Sort is a **blocking operator**. It must read all child records into memory
 * during Init() before sorting and yielding the first record.
 */
class SortExecutor : public AbstractExecutor {
private:
    std::unique_ptr<AbstractExecutor> child_;
    std::string sort_col_;
    bool asc_;
    std::vector<std::string> out_cols_;
    
    int sort_idx_ = -1;
    std::vector<Tuple> sorted_tuples_; // Cached list of all sorted records
    size_t curr_idx_ = 0;              // Iterator yield cursor
    
public:
    SortExecutor(std::unique_ptr<AbstractExecutor> child, const std::string& sort_col, bool asc)
        : child_(std::move(child)), sort_col_(sort_col), asc_(asc) {
        out_cols_ = child_->GetOutputColumns();
        for (size_t i = 0; i < out_cols_.size(); ++i) {
            if (out_cols_[i] == sort_col_ || out_cols_[i] == sort_col_.substr(sort_col_.find('.') + 1)) {
                sort_idx_ = static_cast<int>(i);
                break;
            }
        }
    }
    
    void Init() override {
        child_->Init();
        sorted_tuples_.clear();
        curr_idx_ = 0;
        
        // Read all rows into memory
        Tuple t;
        while (child_->Next(&t)) {
            sorted_tuples_.push_back(t);
        }
        
        // Sort the cached collection based on data type heuristics (numeric vs alphabetical)
        if (sort_idx_ != -1 && !sorted_tuples_.empty()) {
            std::sort(sorted_tuples_.begin(), sorted_tuples_.end(), [this](const Tuple& a, const Tuple& b) {
                const std::string& val_a = a.values[this->sort_idx_];
                const std::string& val_b = b.values[this->sort_idx_];
                
                // Heuristic: check if strings are numeric values
                bool is_num = true;
                for (char c : val_a) {
                    if (!std::isdigit(c) && c != '-' && c != '.') { is_num = false; break; }
                }
                for (char c : val_b) {
                    if (!std::isdigit(c) && c != '-' && c != '.') { is_num = false; break; }
                }
                
                if (is_num && !val_a.empty() && !val_b.empty()) {
                    try {
                        double da = std::stod(val_a);
                        double db = std::stod(val_b);
                        return this->asc_ ? (da < db) : (da > db);
                    } catch (...) {}
                }
                return this->asc_ ? (val_a < val_b) : (val_a > val_b);
            });
        }
    }
    
    bool Next(Tuple* tuple) override {
        if (curr_idx_ < sorted_tuples_.size()) {
            *tuple = sorted_tuples_[curr_idx_++];
            return true; // Yield sorted record
        }
        return false;
    }
    
    void Close() override {
        child_->Close();
    }
    
    const std::vector<std::string>& GetOutputColumns() const override {
        return out_cols_;
    }
};

/**
 * @brief LimitExecutor limits the returned record count.
 */
class LimitExecutor : public AbstractExecutor {
private:
    std::unique_ptr<AbstractExecutor> child_;
    int limit_count_;   // Target maximum rows
    int curr_count_ = 0; // Running counter of yielded rows
    std::vector<std::string> out_cols_;
    
public:
    LimitExecutor(std::unique_ptr<AbstractExecutor> child, int limit_count)
        : child_(std::move(child)), limit_count_(limit_count) {
        out_cols_ = child_->GetOutputColumns();
    }
    
    void Init() override {
        child_->Init();
        curr_count_ = 0;
    }
    
    bool Next(Tuple* tuple) override {
        if (curr_count_ >= limit_count_) return false; // Threshold reached
        if (child_->Next(tuple)) {
            curr_count_++;
            return true; // Yield record
        }
        return false;
    }
    
    void Close() override {
        child_->Close();
    }
    
    const std::vector<std::string>& GetOutputColumns() const override {
        return out_cols_;
    }
};

#endif // EXECUTOR_H
