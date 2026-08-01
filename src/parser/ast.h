#ifndef AST_H
#define AST_H

#include "catalog/catalog.h"
#include "lexer/lexer.h"
#include <string>
#include <vector>
#include <memory>

/**
 * @brief Categorization types for Abstract Syntax Tree (AST) nodes.
 */
enum class ASTType {
    CREATE_TABLE,
    INSERT,
    SELECT,
    UPDATE,
    DELETE,
    CREATE_INDEX
};

/**
 * @brief Base Abstract class representing any parsed SQL statement node in the AST.
 */
class ASTNode {
public:
    virtual ~ASTNode() = default;
    
    /**
     * @brief Gets the node type.
     */
    virtual ASTType GetType() const = 0;
};

/**
 * @brief Represents a column definition inside a CREATE TABLE clause.
 */
struct ColumnDef {
    std::string name; // Name of the column
    DataType type;    // Data type of the column (INT or TEXT)
};

/**
 * @brief AST Node representing a CREATE TABLE statement.
 * Format: CREATE TABLE table_name (col1 type1, col2 type2, ...)
 */
class CreateNode : public ASTNode {
public:
    std::string table_name;
    std::vector<ColumnDef> columns;
    ASTType GetType() const override { return ASTType::CREATE_TABLE; }
};

/**
 * @brief AST Node representing an INSERT INTO statement.
 * Format: INSERT INTO table_name VALUES (val1, val2, ...)
 */
class InsertNode : public ASTNode {
public:
    std::string table_name;
    std::vector<std::string> values; // Row values stored as raw string literals
    ASTType GetType() const override { return ASTType::INSERT; }
};

/**
 * @brief Represents a single filtering condition within a WHERE clause.
 * Format: col_name op value (e.g., id = 2)
 */
struct WhereCondition {
    std::string col_name;   // Target column name to filter
    TokenType op;           // Comparison operator (EQUAL, GREATER, etc.)
    std::string value;      // Value literal to compare against
    bool is_valid = false;  // Flag indicating if the condition exists
};

/**
 * @brief Represents a relational JOIN clause on two tables.
 * Format: JOIN table_name ON left_col = right_col
 */
struct JoinClause {
    std::string table_name; // Table name of the right side table
    std::string left_col;   // Joined column belonging to the left table
    std::string right_col;  // Joined column belonging to the right table
    bool has_join = false;  // Flag indicating if a JOIN was requested
};

/**
 * @brief Represents sorting configuration in the query.
 * Format: ORDER BY col_name [ASC|DESC]
 */
struct OrderByClause {
    std::string col_name;       // Column to sort by
    bool asc = true;            // Sorting direction: true for ASC, false for DESC
    bool has_order_by = false;  // Flag indicating if sorting was requested
};

/**
 * @brief Represents limits placed on the returned row count.
 * Format: LIMIT limit_count
 */
struct LimitClause {
    int limit_count = -1;    // Maximum rows to retrieve
    bool has_limit = false;  // Flag indicating if a limit was requested
};

/**
 * @brief AST Node representing a SELECT statement.
 */
class SelectNode : public ASTNode {
public:
    std::vector<std::string> columns; // Project list columns (e.g. name, id or *)
    std::string table_name;           // Outer table name
    JoinClause join;                  // Optional JOIN clause configuration
    WhereCondition where;             // Optional WHERE clause configuration
    OrderByClause order_by;           // Optional ORDER BY clause configuration
    LimitClause limit;                // Optional LIMIT clause configuration
    ASTType GetType() const override { return ASTType::SELECT; }
};

/**
 * @brief AST Node representing an UPDATE table statement.
 * Format: UPDATE table_name SET set_column = set_value WHERE condition
 */
class UpdateNode : public ASTNode {
public:
    std::string table_name;
    std::string set_column; // Column target to modify
    std::string set_value;  // New value to assign
    WhereCondition where;   // Filtering condition to match rows (safety enforcement)
    ASTType GetType() const override { return ASTType::UPDATE; }
};

/**
 * @brief AST Node representing a DELETE FROM table statement.
 * Format: DELETE FROM table_name WHERE condition
 */
class DeleteNode : public ASTNode {
public:
    std::string table_name;
    WhereCondition where; // Filtering condition to match rows to delete (safety enforcement)
    ASTType GetType() const override { return ASTType::DELETE; }
};

/**
 * @brief AST Node representing a CREATE INDEX statement on a column.
 * Format: CREATE INDEX ON table_name (col_name)
 */
class CreateIndexNode : public ASTNode {
public:
    std::string table_name;
    std::string col_name;
    ASTType GetType() const override { return ASTType::CREATE_INDEX; }
};

#endif // AST_H
