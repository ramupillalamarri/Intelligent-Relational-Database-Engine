#ifndef AST_H
#define AST_H

#include "catalog/catalog.h"
#include "lexer/lexer.h"
#include <string>
#include <vector>
#include <memory>

enum class ASTType {
    CREATE_TABLE,
    INSERT,
    SELECT,
    UPDATE,
    DELETE,
    CREATE_INDEX
};

class ASTNode {
public:
    virtual ~ASTNode() = default;
    virtual ASTType GetType() const = 0;
};

struct ColumnDef {
    std::string name;
    DataType type;
};

class CreateNode : public ASTNode {
public:
    std::string table_name;
    std::vector<ColumnDef> columns;
    ASTType GetType() const override { return ASTType::CREATE_TABLE; }
};

class InsertNode : public ASTNode {
public:
    std::string table_name;
    std::vector<std::string> values;
    ASTType GetType() const override { return ASTType::INSERT; }
};

struct WhereCondition {
    std::string col_name;
    TokenType op;
    std::string value;
    bool is_valid = false;
};

struct JoinClause {
    std::string table_name;
    std::string left_col;
    std::string right_col;
    bool has_join = false;
};

struct OrderByClause {
    std::string col_name;
    bool asc = true;
    bool has_order_by = false;
};

struct LimitClause {
    int limit_count = -1;
    bool has_limit = false;
};

class SelectNode : public ASTNode {
public:
    std::vector<std::string> columns;
    std::string table_name;
    JoinClause join;
    WhereCondition where;
    OrderByClause order_by;
    LimitClause limit;
    ASTType GetType() const override { return ASTType::SELECT; }
};

class UpdateNode : public ASTNode {
public:
    std::string table_name;
    std::string set_column;
    std::string set_value;
    WhereCondition where;
    ASTType GetType() const override { return ASTType::UPDATE; }
};

class DeleteNode : public ASTNode {
public:
    std::string table_name;
    WhereCondition where;
    ASTType GetType() const override { return ASTType::DELETE; }
};

class CreateIndexNode : public ASTNode {
public:
    std::string table_name;
    std::string col_name;
    ASTType GetType() const override { return ASTType::CREATE_INDEX; }
};

#endif // AST_H
