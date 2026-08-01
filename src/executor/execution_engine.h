#ifndef EXECUTION_ENGINE_H
#define EXECUTION_ENGINE_H

#include "executor.h"
#include "planner/planner.h"
#include "catalog/catalog.h"
#include "storage/storage_manager.h"
#include "index/index_manager.h"
#include <chrono>
#include <sstream>
#include <iomanip>

/**
 * @brief The ExecutionEngine coordinates query compilation and statement execution.
 * 
 * It matches AST Node Statements (CREATE, INSERT, UPDATE, DELETE, SELECT) to their
 * appropriate database actions. For SELECT statements, it translates physical execution
 * plans into nested iterators.
 */
class ExecutionEngine {
private:
    Catalog& catalog_;       // System catalog reference to check schema definitions
    StorageManager& sm_;     // Storage manager reference to read/write pages
    IndexManager& im_;       // Index manager reference to traverse B+ Trees
    
    /**
     * @brief Recursively compiles a PlanNode execution tree into nested AbstractExecutor iterators.
     * @param plan The physical plan step node.
     * @return std::unique_ptr<AbstractExecutor> A Volcano-style iterator.
     */
    std::unique_ptr<AbstractExecutor> BuildExecutor(std::shared_ptr<PlanNode> plan) {
        if (!plan) return nullptr;
        
        switch (plan->type) {
            case PlanType::SEQ_SCAN: {
                Schema schema = catalog_.GetSchema(plan->table_name);
                return std::make_unique<SeqScanExecutor>(sm_, plan->table_name, schema);
            }
            case PlanType::INDEX_SCAN: {
                Schema schema = catalog_.GetSchema(plan->table_name);
                DataType col_type = DataType::TEXT;
                for (const auto& col : schema.columns) {
                    if (col.name == plan->index_col) {
                        col_type = col.type;
                        break;
                    }
                }
                return std::make_unique<IndexScanExecutor>(sm_, im_, plan->table_name, plan->index_col, col_type, plan->index_val, schema);
            }
            case PlanType::FILTER: {
                std::unique_ptr<AbstractExecutor> left_exec = BuildExecutor(plan->left);
                Schema schema;
                if (plan->left->type == PlanType::SEQ_SCAN || plan->left->type == PlanType::INDEX_SCAN) {
                    schema = catalog_.GetSchema(plan->left->table_name);
                }
                return std::make_unique<FilterExecutor>(std::move(left_exec), plan->filter_cond, schema);
            }
            case PlanType::PROJECT: {
                std::unique_ptr<AbstractExecutor> left_exec = BuildExecutor(plan->left);
                return std::make_unique<ProjectExecutor>(std::move(left_exec), plan->output_columns);
            }
            case PlanType::NESTED_LOOP_JOIN: {
                std::unique_ptr<AbstractExecutor> left_exec = BuildExecutor(plan->left);
                std::unique_ptr<AbstractExecutor> right_exec = BuildExecutor(plan->right);
                return std::make_unique<NestedLoopJoinExecutor>(std::move(left_exec), std::move(right_exec), plan->join_left_col, plan->join_right_col);
            }
            case PlanType::SORT: {
                std::unique_ptr<AbstractExecutor> left_exec = BuildExecutor(plan->left);
                return std::make_unique<SortExecutor>(std::move(left_exec), plan->sort_col, plan->sort_asc);
            }
            case PlanType::LIMIT: {
                std::unique_ptr<AbstractExecutor> left_exec = BuildExecutor(plan->left);
                return std::make_unique<LimitExecutor>(std::move(left_exec), plan->limit_count);
            }
        }
        return nullptr;
    }
    
    /**
     * @brief Scans the catalog schemas and pre-loads all existing indices into active memory.
     */
    void LoadAllActiveIndexes() {
        for (const auto& pair : catalog_.GetTables()) {
            std::string table_name = pair.first;
            for (const auto& col : pair.second.columns) {
                if (col.has_index) {
                    im_.LoadIndex(table_name, col.name, col.type);
                }
            }
        }
    }
    
public:
    /**
     * @brief Construct a new Execution Engine.
     */
    ExecutionEngine(Catalog& catalog, StorageManager& sm, IndexManager& im)
        : catalog_(catalog), sm_(sm), im_(im) {
        LoadAllActiveIndexes();
    }
    
    /**
     * @brief Executes a compiled AST statement and returns results formatted as a JSON string.
     * 
     * @param stmt The parsed AST statement node.
     * @param physical_plan The pre-compiled physical plan (if statement is a SELECT query).
     * @param duration_ms Time spent during tokenizing and planning.
     * @return std::string JSON response string outlining execution results, messages, or errors.
     */
    std::string ExecuteQuery(std::unique_ptr<ASTNode> stmt, std::shared_ptr<PlanNode> physical_plan, double duration_ms) {
        if (!stmt) {
            return "{\"success\": false, \"error\": \"Invalid or empty statement AST\"}";
        }
        
        std::stringstream ss;
        
        // 1. CREATE TABLE Execution
        if (stmt->GetType() == ASTType::CREATE_TABLE) {
            auto node = static_cast<CreateNode*>(stmt.get());
            std::vector<Column> cols;
            for (const auto& col_def : node->columns) {
                cols.push_back(Column{col_def.name, col_def.type, false});
            }
            bool success = catalog_.CreateTable(node->table_name, cols);
            if (success) {
                sm_.CreateTableFile(node->table_name);
                return "{\"success\": true, \"message\": \"Table created successfully\", \"latency_ms\": " + std::to_string(duration_ms) + "}";
            } else {
                return "{\"success\": false, \"error\": \"Table already exists\", \"latency_ms\": " + std::to_string(duration_ms) + "}";
            }
        }
        
        // 2. CREATE INDEX Execution
        else if (stmt->GetType() == ASTType::CREATE_INDEX) {
            auto node = static_cast<CreateIndexNode*>(stmt.get());
            if (!catalog_.TableExists(node->table_name)) {
                return "{\"success\": false, \"error\": \"Table does not exist\"}";
            }
            Schema schema = catalog_.GetSchema(node->table_name);
            int col_idx = schema.GetColIndex(node->col_name);
            if (col_idx == -1) {
                return "{\"success\": false, \"error\": \"Column does not exist in table\"}";
            }
            DataType col_type = schema.columns[col_idx].type;
            
            // Register index allocation in system catalog metafile
            catalog_.CreateIndex(node->table_name, node->col_name);
            
            // Instantiate index structure in memory
            im_.CreateIndex(node->table_name, node->col_name, col_type);
            
            // Populate index by scanning the table file and inserting existing keys
            uint32_t page_count = sm_.GetPageCount(node->table_name);
            Page page;
            for (uint32_t p = 0; p < page_count; ++p) {
                if (sm_.ReadPage(node->table_name, p, page)) {
                    uint16_t record_count = page.GetRecordCount();
                    for (uint16_t s = 0; s < record_count; ++s) {
                        std::vector<char> record_data;
                        if (page.GetRecord(s, record_data)) {
                            std::vector<std::string> vals = DeserializeRow(schema, record_data);
                            im_.Insert(node->table_name, node->col_name, col_type, vals[col_idx], RecordID{p, s});
                        }
                    }
                }
            }
            
            im_.SaveIndex(node->table_name, node->col_name, col_type);
            return "{\"success\": true, \"message\": \"Index created successfully\", \"latency_ms\": " + std::to_string(duration_ms) + "}";
        }
        
        // 3. INSERT Execution
        else if (stmt->GetType() == ASTType::INSERT) {
            auto node = static_cast<InsertNode*>(stmt.get());
            if (!catalog_.TableExists(node->table_name)) {
                return "{\"success\": false, \"error\": \"Table does not exist\"}";
            }
            Schema schema = catalog_.GetSchema(node->table_name);
            if (node->values.size() != schema.columns.size()) {
                return "{\"success\": false, \"error\": \"Column size mismatch: values size is " + std::to_string(node->values.size()) + ", table has " + std::to_string(schema.columns.size()) + "\"}";
            }
            
            // Serialize row values into binary page bytes
            std::vector<char> record_bytes = SerializeRow(schema, node->values);
            RecordID rid = sm_.InsertRecord(node->table_name, record_bytes.data(), record_bytes.size());
            
            if (rid.page_id == 0xFFFFFFFF) {
                return "{\"success\": false, \"error\": \"Disk write failed\"}";
            }
            
            // Insert the newly generated RecordID into all active table indexes
            for (size_t i = 0; i < schema.columns.size(); ++i) {
                const auto& col = schema.columns[i];
                if (col.has_index) {
                    im_.Insert(node->table_name, col.name, col.type, node->values[i], rid);
                    im_.SaveIndex(node->table_name, col.name, col.type);
                }
            }
            
            return "{\"success\": true, \"message\": \"Record inserted successfully\", \"latency_ms\": " + std::to_string(duration_ms) + "}";
        }
        
        // 4. DELETE Execution
        else if (stmt->GetType() == ASTType::DELETE) {
            auto node = static_cast<DeleteNode*>(stmt.get());
            if (!catalog_.TableExists(node->table_name)) {
                return "{\"success\": false, \"error\": \"Table does not exist\"}";
            }
            
            Schema schema = catalog_.GetSchema(node->table_name);
            int col_idx = schema.GetColIndex(node->where.col_name);
            if (col_idx == -1) {
                return "{\"success\": false, \"error\": \"Column does not exist\"}";
            }
            
            uint32_t page_count = sm_.GetPageCount(node->table_name);
            Page page;
            int deleted_count = 0;
            
            // Sequential scan and log matching records as deleted
            for (uint32_t p = 0; p < page_count; ++p) {
                bool page_dirty = false;
                if (sm_.ReadPage(node->table_name, p, page)) {
                    uint16_t record_count = page.GetRecordCount();
                    for (uint16_t s = 0; s < record_count; ++s) {
                        std::vector<char> record_data;
                        if (page.GetRecord(s, record_data)) {
                            std::vector<std::string> vals = DeserializeRow(schema, record_data);
                            if (vals[col_idx] == node->where.value) {
                                page.DeleteRecord(s);
                                page_dirty = true;
                                deleted_count++;
                            }
                        }
                    }
                    if (page_dirty) {
                        sm_.WritePage(node->table_name, p, page);
                    }
                }
            }
            
            // Rebuild active indices to prune deleted keys from the B+ Tree
            if (deleted_count > 0) {
                for (const auto& col : schema.columns) {
                    if (col.has_index) {
                        im_.ClearIndex(node->table_name, col.name, col.type);
                        im_.CreateIndex(node->table_name, col.name, col.type);
                        
                        Page idx_page;
                        for (uint32_t p = 0; p < page_count; ++p) {
                            if (sm_.ReadPage(node->table_name, p, idx_page)) {
                                uint16_t rec_count = idx_page.GetRecordCount();
                                for (uint16_t s = 0; s < rec_count; ++s) {
                                    std::vector<char> rec_data;
                                    if (idx_page.GetRecord(s, rec_data)) {
                                        std::vector<std::string> vals = DeserializeRow(schema, rec_data);
                                        int c_idx = schema.GetColIndex(col.name);
                                        im_.Insert(node->table_name, col.name, col.type, vals[c_idx], RecordID{p, s});
                                    }
                                }
                            }
                        }
                        im_.SaveIndex(node->table_name, col.name, col.type);
                    }
                }
            }
            
            return "{\"success\": true, \"message\": \"Deleted " + std::to_string(deleted_count) + " records\", \"latency_ms\": " + std::to_string(duration_ms) + "}";
        }
        
        // 5. UPDATE Execution
        else if (stmt->GetType() == ASTType::UPDATE) {
            auto node = static_cast<UpdateNode*>(stmt.get());
            if (!catalog_.TableExists(node->table_name)) {
                return "{\"success\": false, \"error\": \"Table does not exist\"}";
            }
            
            Schema schema = catalog_.GetSchema(node->table_name);
            int where_col_idx = schema.GetColIndex(node->where.col_name);
            int set_col_idx = schema.GetColIndex(node->set_column);
            if (where_col_idx == -1 || set_col_idx == -1) {
                return "{\"success\": false, \"error\": \"Columns do not exist\"}";
            }
            
            uint32_t page_count = sm_.GetPageCount(node->table_name);
            Page page;
            int updated_count = 0;
            
            for (uint32_t p = 0; p < page_count; ++p) {
                bool page_dirty = false;
                if (sm_.ReadPage(node->table_name, p, page)) {
                    uint16_t record_count = page.GetRecordCount();
                    for (uint16_t s = 0; s < record_count; ++s) {
                        std::vector<char> record_data;
                        if (page.GetRecord(s, record_data)) {
                            std::vector<std::string> vals = DeserializeRow(schema, record_data);
                            if (vals[where_col_idx] == node->where.value) {
                                vals[set_col_idx] = node->set_value;
                                std::vector<char> updated_bytes = SerializeRow(schema, vals);
                                
                                // Try updating in-place on the same page
                                if (page.UpdateRecord(s, updated_bytes.data(), updated_bytes.size())) {
                                    page_dirty = true;
                                    updated_count++;
                                } else {
                                    // Relocate record: delete old and insert at the end of the file
                                    page.DeleteRecord(s);
                                    page_dirty = true;
                                    
                                    sm_.InsertRecord(node->table_name, updated_bytes.data(), updated_bytes.size());
                                    updated_count++;
                                }
                            }
                        }
                    }
                    if (page_dirty) {
                        sm_.WritePage(node->table_name, p, page);
                    }
                }
            }
            
            // Rebuild all indices if updates occurred
            if (updated_count > 0) {
                for (const auto& col : schema.columns) {
                    if (col.has_index) {
                        im_.ClearIndex(node->table_name, col.name, col.type);
                        im_.CreateIndex(node->table_name, col.name, col.type);
                        
                        Page idx_page;
                        uint32_t new_page_count = sm_.GetPageCount(node->table_name);
                        for (uint32_t p = 0; p < new_page_count; ++p) {
                            if (sm_.ReadPage(node->table_name, p, idx_page)) {
                                uint16_t rec_count = idx_page.GetRecordCount();
                                for (uint16_t s = 0; s < rec_count; ++s) {
                                    std::vector<char> rec_data;
                                    if (idx_page.GetRecord(s, rec_data)) {
                                        std::vector<std::string> vals = DeserializeRow(schema, rec_data);
                                        int c_idx = schema.GetColIndex(col.name);
                                        im_.Insert(node->table_name, col.name, col.type, vals[c_idx], RecordID{p, s});
                                    }
                                }
                            }
                        }
                        im_.SaveIndex(node->table_name, col.name, col.type);
                    }
                }
            }
            
            return "{\"success\": true, \"message\": \"Updated " + std::to_string(updated_count) + " records\", \"latency_ms\": " + std::to_string(duration_ms) + "}";
        }
        
        // 6. SELECT Query Execution (Volcano Pipelines)
        else if (stmt->GetType() == ASTType::SELECT) {
            std::unique_ptr<AbstractExecutor> exec = BuildExecutor(physical_plan);
            exec->Init();
            
            ss << "{\n";
            ss << "  \"success\": true,\n";
            ss << "  \"latency_ms\": " << duration_ms << ",\n";
            
            // Include JSON representation of the physical plan tree
            Planner planner(catalog_);
            ss << "  \"execution_plan\": " << planner.SerializePlanTree(physical_plan) << ",\n";
            
            // Output Columns metadata
            const auto& cols = exec->GetOutputColumns();
            ss << "  \"columns\": [";
            for (size_t i = 0; i < cols.size(); ++i) {
                ss << "\"" << cols[i] << "\"" << (i + 1 < cols.size() ? ", " : "");
            }
            ss << "],\n";
            
            // Output Records payload
            ss << "  \"records\": [\n";
            Tuple tuple;
            bool first = true;
            int count = 0;
            // Pull records sequentially using Volcano iterator interface
            while (exec->Next(&tuple)) {
                if (!first) ss << ",\n";
                first = false;
                ss << "    [";
                for (size_t i = 0; i < tuple.values.size(); ++i) {
                    std::string val = tuple.values[i];
                    std::string escaped = "";
                    for (char c : val) {
                        if (c == '"') escaped += "\\\"";
                        else escaped += c;
                    }
                    ss << "\"" << escaped << "\"" << (i + 1 < tuple.values.size() ? ", " : "");
                }
                ss << "]";
                count++;
            }
            ss << "\n  ],\n";
            ss << "  \"count\": " << count << "\n";
            ss << "}";
            
            exec->Close();
            return ss.str();
        }
        
        return "{\"success\": false, \"error\": \"Invalid execution flow\"}";
    }
};

#endif // EXECUTION_ENGINE_H
