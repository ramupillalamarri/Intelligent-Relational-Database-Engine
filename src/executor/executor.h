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

struct Tuple {
    std::vector<std::string> values;
};

class AbstractExecutor {
public:
    virtual ~AbstractExecutor() = default;
    virtual void Init() = 0;
    virtual bool Next(Tuple* tuple) = 0;
    virtual void Close() = 0;
    virtual const std::vector<std::string>& GetOutputColumns() const = 0;
};

// --- SeqScanExecutor ---
class SeqScanExecutor : public AbstractExecutor {
private:
    StorageManager& sm_;
    std::string table_name_;
    Schema schema_;
    std::vector<std::string> out_cols_;
    
    uint32_t page_count_ = 0;
    uint32_t curr_page_id_ = 0;
    uint16_t curr_slot_id_ = 0;
    Page curr_page_;
    bool page_loaded_ = false;
    
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
        
        while (curr_page_id_ < page_count_) {
            if (!page_loaded_) {
                if (!sm_.ReadPage(table_name_, curr_page_id_, curr_page_)) {
                    curr_page_id_++;
                    curr_slot_id_ = 0;
                    continue;
                }
                page_loaded_ = true;
            }
            
            uint16_t record_count = curr_page_.GetRecordCount();
            while (curr_slot_id_ < record_count) {
                std::vector<char> record_data;
                if (curr_page_.GetRecord(curr_slot_id_, record_data)) {
                    tuple->values = DeserializeRow(schema_, record_data);
                    curr_slot_id_++;
                    return true;
                }
                curr_slot_id_++; // Deleted slot
            }
            
            // Move to next page
            curr_page_id_++;
            curr_slot_id_ = 0;
            page_loaded_ = false;
        }
        return false;
    }
    
    void Close() override {}
    
    const std::vector<std::string>& GetOutputColumns() const override {
        return out_cols_;
    }
};

// --- IndexScanExecutor ---
class IndexScanExecutor : public AbstractExecutor {
private:
    StorageManager& sm_;
    IndexManager& im_;
    std::string table_name_;
    std::string col_name_;
    DataType key_type_;
    std::string search_val_;
    Schema schema_;
    std::vector<std::string> out_cols_;
    
    std::vector<RecordID> rids_;
    size_t rid_idx_ = 0;
    
public:
    IndexScanExecutor(StorageManager& sm, IndexManager& im, const std::string& table_name,
                      const std::string& col_name, DataType key_type, const std::string& search_val, const Schema& schema)
        : sm_(sm), im_(im), table_name_(table_name), col_name_(col_name), key_type_(key_type), search_val_(search_val), schema_(schema) {
        for (const auto& col : schema_.columns) {
            out_cols_.push_back(col.name);
        }
    }
    
    void Init() override {
        rids_ = im_.Search(table_name_, col_name_, key_type_, search_val_);
        rid_idx_ = 0;
    }
    
    bool Next(Tuple* tuple) override {
        while (rid_idx_ < rids_.size()) {
            RecordID rid = rids_[rid_idx_++];
            std::vector<char> record_data;
            if (sm_.GetRecord(table_name_, rid, record_data)) {
                tuple->values = DeserializeRow(schema_, record_data);
                return true;
            }
        }
        return false;
    }
    
    void Close() override {}
    
    const std::vector<std::string>& GetOutputColumns() const override {
        return out_cols_;
    }
};

// --- FilterExecutor ---
class FilterExecutor : public AbstractExecutor {
private:
    std::unique_ptr<AbstractExecutor> child_;
    WhereCondition cond_;
    std::vector<std::string> out_cols_;
    int col_idx_ = -1;
    DataType col_type_;
    
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
        
        // Find filter column schema details
        for (size_t i = 0; i < schema.columns.size(); ++i) {
            if (schema.columns[i].name == cond_.col_name) {
                col_idx_ = static_cast<int>(i);
                col_type_ = schema.columns[i].type;
                break;
            }
        }
        
        // If join columns are in child schemas, resolve column index in output columns
        if (col_idx_ == -1) {
            for (size_t i = 0; i < out_cols_.size(); ++i) {
                if (out_cols_[i] == cond_.col_name) {
                    col_idx_ = static_cast<int>(i);
                    // Estimate data type based on simple logic (if number -> INT, else TEXT)
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

// --- ProjectExecutor ---
class ProjectExecutor : public AbstractExecutor {
private:
    std::unique_ptr<AbstractExecutor> child_;
    std::vector<std::string> select_cols_;
    std::vector<int> col_indices_;
    
public:
    ProjectExecutor(std::unique_ptr<AbstractExecutor> child, const std::vector<std::string>& select_cols)
        : child_(std::move(child)), select_cols_(select_cols) {
        const auto& child_cols = child_->GetOutputColumns();
        
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

// --- NestedLoopJoinExecutor ---
class NestedLoopJoinExecutor : public AbstractExecutor {
private:
    std::unique_ptr<AbstractExecutor> left_child_;
    std::unique_ptr<AbstractExecutor> right_child_;
    std::string left_col_;
    std::string right_col_;
    std::vector<std::string> out_cols_;
    
    int left_col_idx_ = -1;
    int right_col_idx_ = -1;
    
    Tuple curr_left_tuple_;
    bool has_left_tuple_ = false;
    
public:
    NestedLoopJoinExecutor(std::unique_ptr<AbstractExecutor> left_child,
                           std::unique_ptr<AbstractExecutor> right_child,
                           const std::string& left_col, const std::string& right_col)
        : left_child_(std::move(left_child)), right_child_(std::move(right_child)),
          left_col_(left_col), right_col_(right_col) {
          
        out_cols_ = left_child_->GetOutputColumns();
        const auto& right_cols = right_child_->GetOutputColumns();
        out_cols_.insert(out_cols_.end(), right_cols.begin(), right_cols.end());
        
        // Find indices of join keys
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
            if (!has_left_tuple_) {
                if (!left_child_->Next(&curr_left_tuple_)) {
                    return false; // Outer table finished
                }
                has_left_tuple_ = true;
                right_child_->Init(); // Reset inner table scan
            }
            
            if (right_child_->Next(&right_tuple)) {
                if (left_col_idx_ != -1 && right_col_idx_ != -1) {
                    const std::string& left_val = curr_left_tuple_.values[left_col_idx_];
                    const std::string& right_val = right_tuple.values[right_col_idx_];
                    if (left_val == right_val) {
                        // Join match
                        tuple->values = curr_left_tuple_.values;
                        tuple->values.insert(tuple->values.end(), right_tuple.values.begin(), right_tuple.values.end());
                        return true;
                    }
                }
            } else {
                // Inner table finished for this outer row
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

// --- SortExecutor ---
class SortExecutor : public AbstractExecutor {
private:
    std::unique_ptr<AbstractExecutor> child_;
    std::string sort_col_;
    bool asc_;
    std::vector<std::string> out_cols_;
    
    int sort_idx_ = -1;
    std::vector<Tuple> sorted_tuples_;
    size_t curr_idx_ = 0;
    
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
        
        Tuple t;
        while (child_->Next(&t)) {
            sorted_tuples_.push_back(t);
        }
        
        // Sort tuples
        if (sort_idx_ != -1 && !sorted_tuples_.empty()) {
            std::sort(sorted_tuples_.begin(), sorted_tuples_.end(), [this](const Tuple& a, const Tuple& b) {
                const std::string& val_a = a.values[this->sort_idx_];
                const std::string& val_b = b.values[this->sort_idx_];
                
                // Estimate if keys are numeric to sort properly
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
            return true;
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

// --- LimitExecutor ---
class LimitExecutor : public AbstractExecutor {
private:
    std::unique_ptr<AbstractExecutor> child_;
    int limit_count_;
    int curr_count_ = 0;
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
        if (curr_count_ >= limit_count_) return false;
        if (child_->Next(tuple)) {
            curr_count_++;
            return true;
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
