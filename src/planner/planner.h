#ifndef PLANNER_H
#define PLANNER_H

#include "parser/ast.h"
#include "catalog/catalog.h"
#include <string>
#include <vector>
#include <memory>
#include <sstream>

enum class PlanType {
    SEQ_SCAN,
    INDEX_SCAN,
    FILTER,
    PROJECT,
    NESTED_LOOP_JOIN,
    SORT,
    LIMIT
};

inline std::string PlanTypeToString(PlanType type) {
    switch (type) {
        case PlanType::SEQ_SCAN: return "SeqScan";
        case PlanType::INDEX_SCAN: return "IndexScan";
        case PlanType::FILTER: return "Filter";
        case PlanType::PROJECT: return "Projection";
        case PlanType::NESTED_LOOP_JOIN: return "NestedLoopJoin";
        case PlanType::SORT: return "Sort";
        case PlanType::LIMIT: return "Limit";
    }
    return "Unknown";
}

struct PlanNode {
    PlanType type;
    std::string detail;
    std::vector<std::string> output_columns;
    std::shared_ptr<PlanNode> left;
    std::shared_ptr<PlanNode> right;
    
    // For specific execution details
    std::string table_name;
    std::string index_col;
    std::string index_val;
    WhereCondition filter_cond;
    std::string join_left_col;
    std::string join_right_col;
    std::string sort_col;
    bool sort_asc = true;
    int limit_count = -1;
};

class Planner {
private:
    const Catalog& catalog_;
    
public:
    Planner(const Catalog& catalog) : catalog_(catalog) {}
    
    std::shared_ptr<PlanNode> GeneratePlan(const SelectNode& select) {
        // Base: Main table scan
        std::shared_ptr<PlanNode> plan = std::make_shared<PlanNode>();
        plan->type = PlanType::SEQ_SCAN;
        plan->table_name = select.table_name;
        plan->detail = "Table: " + select.table_name;
        
        // Populate base table output columns
        const auto& schema = catalog_.GetSchema(select.table_name);
        for (const auto& col : schema.columns) {
            plan->output_columns.push_back(col.name);
        }
        
        // Handle Join
        if (select.join.has_join) {
            std::shared_ptr<PlanNode> right_scan = std::make_shared<PlanNode>();
            right_scan->type = PlanType::SEQ_SCAN;
            right_scan->table_name = select.join.table_name;
            right_scan->detail = "Table: " + select.join.table_name;
            
            const auto& right_schema = catalog_.GetSchema(select.join.table_name);
            for (const auto& col : right_schema.columns) {
                right_scan->output_columns.push_back(col.name);
            }
            
            std::shared_ptr<PlanNode> join_node = std::make_shared<PlanNode>();
            join_node->type = PlanType::NESTED_LOOP_JOIN;
            join_node->left = plan;
            join_node->right = right_scan;
            join_node->join_left_col = select.join.left_col;
            join_node->join_right_col = select.join.right_col;
            join_node->detail = "Condition: " + select.join.left_col + " = " + select.join.right_col;
            
            // Output of Join is union of both
            join_node->output_columns = plan->output_columns;
            join_node->output_columns.insert(join_node->output_columns.end(), 
                                             right_scan->output_columns.begin(), 
                                             right_scan->output_columns.end());
            plan = join_node;
        }
        
        // Handle Filter (WHERE clause) & Optimize with B+ Tree Index
        if (select.where.is_valid) {
            bool use_index = false;
            std::string indexed_table = "";
            std::string indexed_col = "";
            
            // Optimization Check: Can we use an index?
            // If operator is EQUAL, check if the columns have an index.
            if (select.where.op == TokenType::EQUAL) {
                // If it's a simple query (no join), check main table
                if (!select.join.has_join) {
                    if (catalog_.HasIndex(select.table_name, select.where.col_name)) {
                        use_index = true;
                        indexed_table = select.table_name;
                        indexed_col = select.where.col_name;
                    }
                } else {
                    // With a join, index can be used on the filter column if it belongs to one of the tables
                    // Check main table
                    if (catalog_.HasIndex(select.table_name, select.where.col_name)) {
                        use_index = true;
                        indexed_table = select.table_name;
                        indexed_col = select.where.col_name;
                    }
                    // Check join table
                    else if (catalog_.HasIndex(select.join.table_name, select.where.col_name)) {
                        use_index = true;
                        indexed_table = select.join.table_name;
                        indexed_col = select.where.col_name;
                    }
                }
            }
            
            if (use_index) {
                // Optimizer optimization: replace SeqScan with IndexScan
                // Let's find where the SeqScan is in our tree and replace it.
                if (plan->type == PlanType::SEQ_SCAN && plan->table_name == indexed_table) {
                    plan->type = PlanType::INDEX_SCAN;
                    plan->index_col = indexed_col;
                    plan->index_val = select.where.value;
                    plan->detail = "Index: " + indexed_col + " = " + select.where.value;
                } else if (plan->type == PlanType::NESTED_LOOP_JOIN) {
                    if (plan->left->type == PlanType::SEQ_SCAN && plan->left->table_name == indexed_table) {
                        plan->left->type = PlanType::INDEX_SCAN;
                        plan->left->index_col = indexed_col;
                        plan->left->index_val = select.where.value;
                        plan->left->detail = "Index: " + indexed_col + " = " + select.where.value;
                    } else if (plan->right->type == PlanType::SEQ_SCAN && plan->right->table_name == indexed_table) {
                        plan->right->type = PlanType::INDEX_SCAN;
                        plan->right->index_col = indexed_col;
                        plan->right->index_val = select.where.value;
                        plan->right->detail = "Index: " + indexed_col + " = " + select.where.value;
                    } else {
                        // Standard filter fallback
                        std::shared_ptr<PlanNode> filter_node = std::make_shared<PlanNode>();
                        filter_node->type = PlanType::FILTER;
                        filter_node->left = plan;
                        filter_node->filter_cond = select.where;
                        filter_node->output_columns = plan->output_columns;
                        
                        std::string op_str = "="; // Default
                        if (select.where.op == TokenType::GREATER) op_str = ">";
                        if (select.where.op == TokenType::LESS) op_str = "<";
                        if (select.where.op == TokenType::GREATER_EQUAL) op_str = ">=";
                        if (select.where.op == TokenType::LESS_EQUAL) op_str = "<=";
                        if (select.where.op == TokenType::NOT_EQUAL) op_str = "!=";
                        
                        filter_node->detail = "Condition: " + select.where.col_name + " " + op_str + " " + select.where.value;
                        plan = filter_node;
                    }
                }
            } else {
                // Standard Filter node
                std::shared_ptr<PlanNode> filter_node = std::make_shared<PlanNode>();
                filter_node->type = PlanType::FILTER;
                filter_node->left = plan;
                filter_node->filter_cond = select.where;
                filter_node->output_columns = plan->output_columns;
                
                std::string op_str = "=";
                if (select.where.op == TokenType::GREATER) op_str = ">";
                if (select.where.op == TokenType::LESS) op_str = "<";
                if (select.where.op == TokenType::GREATER_EQUAL) op_str = ">=";
                if (select.where.op == TokenType::LESS_EQUAL) op_str = "<=";
                if (select.where.op == TokenType::NOT_EQUAL) op_str = "!=";
                
                filter_node->detail = "Condition: " + select.where.col_name + " " + op_str + " " + select.where.value;
                plan = filter_node;
            }
        }
        
        // Handle Sort (ORDER BY)
        if (select.order_by.has_order_by) {
            std::shared_ptr<PlanNode> sort_node = std::make_shared<PlanNode>();
            sort_node->type = PlanType::SORT;
            sort_node->left = plan;
            sort_node->sort_col = select.order_by.col_name;
            sort_node->sort_asc = select.order_by.asc;
            sort_node->detail = "Sort by: " + select.order_by.col_name + (select.order_by.asc ? " ASC" : " DESC");
            sort_node->output_columns = plan->output_columns;
            plan = sort_node;
        }
        
        // Handle Limit
        if (select.limit.has_limit) {
            std::shared_ptr<PlanNode> limit_node = std::make_shared<PlanNode>();
            limit_node->type = PlanType::LIMIT;
            limit_node->left = plan;
            limit_node->limit_count = select.limit.limit_count;
            limit_node->detail = "Count: " + std::to_string(select.limit.limit_count);
            limit_node->output_columns = plan->output_columns;
            plan = limit_node;
        }
        
        // Handle Projection (Project columns select list)
        std::shared_ptr<PlanNode> project_node = std::make_shared<PlanNode>();
        project_node->type = PlanType::PROJECT;
        project_node->left = plan;
        project_node->detail = "";
        
        if (select.columns.size() == 1 && select.columns[0] == "*") {
            project_node->output_columns = plan->output_columns;
            project_node->detail = "Columns: *";
        } else {
            project_node->output_columns = select.columns;
            std::string cols_str = "Columns: ";
            for (size_t i = 0; i < select.columns.size(); ++i) {
                cols_str += select.columns[i] + (i + 1 < select.columns.size() ? ", " : "");
            }
            project_node->detail = cols_str;
        }
        
        plan = project_node;
        return plan;
    }
    
    // Prints the plan node to a JSON structure for Next.js to render
    std::string SerializePlanTree(std::shared_ptr<PlanNode> node) {
        if (!node) return "null";
        
        std::stringstream ss;
        ss << "{\n";
        ss << "  \"type\": \"" << PlanTypeToString(node->type) << "\",\n";
        ss << "  \"detail\": \"" << node->detail << "\",\n";
        
        ss << "  \"columns\": [";
        for (size_t i = 0; i < node->output_columns.size(); ++i) {
            ss << "\"" << node->output_columns[i] << "\"" << (i + 1 < node->output_columns.size() ? ", " : "");
        }
        ss << "],\n";
        
        ss << "  \"left\": " << SerializePlanTree(node->left) << ",\n";
        ss << "  \"right\": " << SerializePlanTree(node->right) << "\n";
        ss << "}";
        return ss.str();
    }
};

#endif // PLANNER_H
