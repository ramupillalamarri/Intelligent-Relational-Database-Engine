#ifndef PLANNER_H
#define PLANNER_H

#include "parser/ast.h"
#include "catalog/catalog.h"
#include <string>
#include <vector>
#include <memory>
#include <sstream>

/**
 * @brief Physical execution operator types.
 */
enum class PlanType {
    SEQ_SCAN,          // Linear file scan
    INDEX_SCAN,        // Logarithmic B+ Tree lookup scan
    FILTER,            // Evaluates filtering conditions
    PROJECT,           // Restricts output column projections
    NESTED_LOOP_JOIN,  // Relational inner join logic
    SORT,              // Memory-based query sorting
    LIMIT              // Restricts output row counts
};

// Converts plan type enum to human-readable strings
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

/**
 * @brief Represents a single physical node inside the compiled Query Execution Plan tree.
 */
struct PlanNode {
    PlanType type;                            // Type of this physical plan step
    std::string detail;                       // Human-readable operational description
    std::vector<std::string> output_columns;  // Columns returned by this step
    std::shared_ptr<PlanNode> left;           // Left child step (nested input source)
    std::shared_ptr<PlanNode> right;          // Right child step (nested input source)
    
    // Node-specific execution configuration parameters
    std::string table_name;       // SeqScan or IndexScan target table
    std::string index_col;        // IndexScan column key
    std::string index_val;        // IndexScan key value to lookup
    WhereCondition filter_cond;   // Filter step evaluation condition
    std::string join_left_col;    // Inner Join matching column (left side)
    std::string join_right_col;   // Inner Join matching column (right side)
    std::string sort_col;         // Sort column target
    bool sort_asc = true;         // Sort direction: true for ASC, false for DESC
    int limit_count = -1;         // Limit row count threshold
};

/**
 * @brief Planner translates a parsed SELECT AST query node into a tree of physical execution operators.
 * It implements a Rule-Based Optimizer (RBO) that optimizes filters into logarithmic index lookups.
 */
class Planner {
private:
    const Catalog& catalog_; // Reference to the system catalog to fetch indexing status
    
public:
    /**
     * @brief Construct a new Planner object.
     */
    Planner(const Catalog& catalog) : catalog_(catalog) {}
    
    /**
     * @brief Translates SELECT AST commands to a physical execution plan tree.
     * 
     * Optimization Rule: If the query filters on a column with a B+ Tree index using
     * equality (`=`), the planner automatically replaces the SeqScan node with an IndexScan node.
     * 
     * @param select The SelectNode AST statement to compile.
     * @return std::shared_ptr<PlanNode> Root node of the compiled physical execution plan.
     */
    std::shared_ptr<PlanNode> GeneratePlan(const SelectNode& select) {
        // Base Operator: Default to a linear table Sequential Scan
        std::shared_ptr<PlanNode> plan = std::make_shared<PlanNode>();
        plan->type = PlanType::SEQ_SCAN;
        plan->table_name = select.table_name;
        plan->detail = "Table: " + select.table_name;
        
        // Populate base table output columns from Catalog schema
        const auto& schema = catalog_.GetSchema(select.table_name);
        for (const auto& col : schema.columns) {
            plan->output_columns.push_back(col.name);
        }
        
        // Handle Join clauses: Wrap standard scans inside a Nested Loop Join node
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
            
            // Join output attributes are the union of columns of both tables
            join_node->output_columns = plan->output_columns;
            join_node->output_columns.insert(join_node->output_columns.end(), 
                                             right_scan->output_columns.begin(), 
                                             right_scan->output_columns.end());
            plan = join_node;
        }
        
        // Handle Filtering (WHERE clause) and Index Optimization rules
        if (select.where.is_valid) {
            bool use_index = false;
            std::string indexed_table = "";
            std::string indexed_col = "";
            
            // Optimizer check: Can we run a logarithmic index search?
            if (select.where.op == TokenType::EQUAL) {
                if (!select.join.has_join) {
                    if (catalog_.HasIndex(select.table_name, select.where.col_name)) {
                        use_index = true;
                        indexed_table = select.table_name;
                        indexed_col = select.where.col_name;
                    }
                } else {
                    // Joins index checks
                    if (catalog_.HasIndex(select.table_name, select.where.col_name)) {
                        use_index = true;
                        indexed_table = select.table_name;
                        indexed_col = select.where.col_name;
                    }
                    else if (catalog_.HasIndex(select.join.table_name, select.where.col_name)) {
                        use_index = true;
                        indexed_table = select.join.table_name;
                        indexed_col = select.where.col_name;
                    }
                }
            }
            
            if (use_index) {
                // Rule matched! Replace SeqScan with IndexScan in the execution tree.
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
                        // Fallback: Default filter operator
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
            } else {
                // Default non-indexed Filter operator mapping
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
        
        // Handle Sorting (ORDER BY)
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
        
        // Handle Limit operators
        if (select.limit.has_limit) {
            std::shared_ptr<PlanNode> limit_node = std::make_shared<PlanNode>();
            limit_node->type = PlanType::LIMIT;
            limit_node->left = plan;
            limit_node->limit_count = select.limit.limit_count;
            limit_node->detail = "Count: " + std::to_string(select.limit.limit_count);
            limit_node->output_columns = plan->output_columns;
            plan = limit_node;
        }
        
        // Handle Column Projections (SELECT column lists)
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
    
    /**
     * @brief Serializes the physical plan tree into a JSON structure for the frontend UI.
     * @param node The root plan node.
     * @return std::string Formatted JSON string.
     */
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
