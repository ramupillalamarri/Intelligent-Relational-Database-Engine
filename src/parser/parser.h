#ifndef PARSER_H
#define PARSER_H

#include "ast.h"
#include "lexer/lexer.h"
#include <vector>
#include <memory>
#include <stdexcept>
#include <iostream>

/**
 * @brief Parser translates a stream of tokens into an Abstract Syntax Tree (AST).
 * 
 * It is implemented as a **Recursive Descent Parser**, where each SQL grammar rule
 * corresponds to a specific parsing method.
 */
class Parser {
private:
    std::vector<Token> tokens_; // List of tokens produced by the Lexer
    size_t pos_ = 0;            // Current token index pointer
    
    /**
     * @brief Peeks at the current token without consuming it.
     */
    const Token& Peek() const {
        if (pos_ >= tokens_.size()) return tokens_.back();
        return tokens_[pos_];
    }
    
    /**
     * @brief Returns the token immediately preceding the current parser position.
     */
    const Token& Previous() const {
        if (pos_ == 0) return tokens_[0];
        return tokens_[pos_ - 1];
    }
    
    /**
     * @brief Checks if the current token is END_OF_FILE.
     */
    bool IsAtEnd() const {
        return Peek().type == TokenType::END_OF_FILE;
    }
    
    /**
     * @brief Consumes and returns the current token, moving the parser pointer forward.
     */
    Token Advance() {
        if (!IsAtEnd()) pos_++;
        return Previous();
    }
    
    /**
     * @brief Checks if the current token matches the specified type.
     */
    bool Check(TokenType type) const {
        if (IsAtEnd()) return false;
        return Peek().type == type;
    }
    
    /**
     * @brief If the current token matches the type, consumes it and returns true.
     */
    bool Match(TokenType type) {
        if (Check(type)) {
            Advance();
            return true;
        }
        return false;
    }
    
    /**
     * @brief Asserts that the current token matches the type, advances the cursor,
     * or throws a syntax parse error with the given message.
     */
    Token Consume(TokenType type, const std::string& message) {
        if (Check(type)) return Advance();
        throw std::runtime_error("Parser Error: " + message + " at token '" + Peek().text + "'");
    }
    
public:
    /**
     * @brief Construct a new Parser object.
     * @param tokens The vector of tokens to parse.
     */
    Parser(const std::vector<Token>& tokens) : tokens_(tokens) {}
    
    /**
     * @brief Begins statement compilation and builds the AST.
     * Parses optional trailing semicolons and catches syntax exceptions.
     * @return std::unique_ptr<ASTNode> The root node of the AST, or nullptr if syntax error.
     */
    std::unique_ptr<ASTNode> Parse() {
        try {
            std::unique_ptr<ASTNode> stmt = ParseStatement();
            if (!IsAtEnd() && Peek().type == TokenType::SEMICOLON) {
                Advance(); // Consume semicolon if present
            }
            return stmt;
        } catch (const std::runtime_error& e) {
            std::cerr << e.what() << std::endl;
            return nullptr;
        }
    }
    
private:
    /**
     * @brief Routes parsing based on the initial statement keyword.
     */
    std::unique_ptr<ASTNode> ParseStatement() {
        if (Match(TokenType::CREATE)) {
            return ParseCreate();
        }
        if (Match(TokenType::INSERT)) {
            return ParseInsert();
        }
        if (Match(TokenType::SELECT)) {
            return ParseSelect();
        }
        if (Match(TokenType::UPDATE)) {
            return ParseUpdate();
        }
        if (Match(TokenType::DELETE)) {
            return ParseDelete();
        }
        throw std::runtime_error("Unsupported statement starting with: " + Peek().text);
    }
    
    /**
     * @brief Parses CREATE TABLE or CREATE INDEX statements.
     */
    std::unique_ptr<ASTNode> ParseCreate() {
        if (Match(TokenType::TABLE)) {
            auto node = std::make_unique<CreateNode>();
            Token name_token = Consume(TokenType::IDENTIFIER, "Expect table name");
            node->table_name = name_token.text;
            
            Consume(TokenType::LPAREN, "Expect '(' after table name");
            
            do {
                Token col_name = Consume(TokenType::IDENTIFIER, "Expect column name");
                DataType type;
                if (Match(TokenType::INT_TYPE)) {
                    type = DataType::INT;
                } else if (Match(TokenType::TEXT_TYPE)) {
                    type = DataType::TEXT;
                } else {
                    throw std::runtime_error("Expect column type (INT or TEXT)");
                }
                node->columns.push_back(ColumnDef{col_name.text, type});
            } while (Match(TokenType::COMMA));
            
            Consume(TokenType::RPAREN, "Expect ')' after column definitions");
            return node;
        } else if (Match(TokenType::INDEX)) {
            // Format: CREATE INDEX ON table_name (col_name)
            auto node = std::make_unique<CreateIndexNode>();
            Consume(TokenType::ON, "Expect 'ON' after CREATE INDEX");
            Token table_token = Consume(TokenType::IDENTIFIER, "Expect table name");
            node->table_name = table_token.text;
            
            Consume(TokenType::LPAREN, "Expect '(' after table name");
            Token col_token = Consume(TokenType::IDENTIFIER, "Expect column name to index");
            node->col_name = col_token.text;
            Consume(TokenType::RPAREN, "Expect ')' after column name");
            
            return node;
        }
        throw std::runtime_error("Expect 'TABLE' or 'INDEX' after 'CREATE'");
    }
    
    /**
     * @brief Parses INSERT INTO table VALUES (val1, val2, ...) statements.
     */
    std::unique_ptr<ASTNode> ParseInsert() {
        Consume(TokenType::INTO, "Expect 'INTO' after 'INSERT'");
        auto node = std::make_unique<InsertNode>();
        Token name_token = Consume(TokenType::IDENTIFIER, "Expect table name");
        node->table_name = name_token.text;
        
        Consume(TokenType::VALUES, "Expect 'VALUES' after table name");
        Consume(TokenType::LPAREN, "Expect '(' before insert values");
        
        do {
            Token val_token = Peek();
            if (val_token.type == TokenType::NUMBER || val_token.type == TokenType::STRING_LITERAL) {
                node->values.push_back(val_token.text);
                Advance();
            } else {
                throw std::runtime_error("Expect literal value (number or string)");
            }
        } while (Match(TokenType::COMMA));
        
        Consume(TokenType::RPAREN, "Expect ')' after insert values");
        return node;
    }
    
    /**
     * @brief Parses SELECT projection FROM table [JOIN...] [WHERE...] [ORDER BY...] [LIMIT...] statements.
     */
    std::unique_ptr<ASTNode> ParseSelect() {
        auto node = std::make_unique<SelectNode>();
        
        // Parse projected columns list (or wildcard '*')
        if (Match(TokenType::STAR)) {
            node->columns.push_back("*");
        } else {
            do {
                Token col_token = Consume(TokenType::IDENTIFIER, "Expect column name");
                node->columns.push_back(col_token.text);
            } while (Match(TokenType::COMMA));
        }
        
        Consume(TokenType::FROM, "Expect 'FROM' clause");
        Token table_token = Consume(TokenType::IDENTIFIER, "Expect table name");
        node->table_name = table_token.text;
        
        // Parse optional JOIN clause (e.g. JOIN departments ON employees.id = departments.emp_id)
        if (Match(TokenType::JOIN)) {
            node->join.has_join = true;
            Token join_table = Consume(TokenType::IDENTIFIER, "Expect join table name");
            node->join.table_name = join_table.text;
            
            Consume(TokenType::ON, "Expect 'ON' after join table name");
            Token left_col = Consume(TokenType::IDENTIFIER, "Expect join comparison left column");
            Consume(TokenType::EQUAL, "Expect '=' in join condition");
            Token right_col = Consume(TokenType::IDENTIFIER, "Expect join comparison right column");
            
            node->join.left_col = left_col.text;
            node->join.right_col = right_col.text;
        }
        
        // Parse optional WHERE condition (e.g. WHERE age >= 18)
        if (Match(TokenType::WHERE)) {
            node->where.is_valid = true;
            Token col_token = Consume(TokenType::IDENTIFIER, "Expect column name in WHERE");
            node->where.col_name = col_token.text;
            
            Token op_token = Advance(); // Read comparison operator
            if (op_token.type != TokenType::EQUAL &&
                op_token.type != TokenType::GREATER &&
                op_token.type != TokenType::LESS &&
                op_token.type != TokenType::GREATER_EQUAL &&
                op_token.type != TokenType::LESS_EQUAL &&
                op_token.type != TokenType::NOT_EQUAL) {
                throw std::runtime_error("Expect comparison operator in WHERE");
            }
            node->where.op = op_token.type;
            
            Token val_token = Peek();
            if (val_token.type == TokenType::NUMBER || val_token.type == TokenType::STRING_LITERAL) {
                node->where.value = val_token.text;
                Advance();
            } else {
                throw std::runtime_error("Expect comparison value in WHERE");
            }
        }
        
        // Parse optional ORDER BY clause (e.g. ORDER BY id DESC)
        if (Match(TokenType::ORDER)) {
            Consume(TokenType::BY, "Expect 'BY' after 'ORDER'");
            node->order_by.has_order_by = true;
            Token col_token = Consume(TokenType::IDENTIFIER, "Expect column name in ORDER BY");
            node->order_by.col_name = col_token.text;
            
            if (Match(TokenType::ASC)) {
                node->order_by.asc = true;
            } else if (Match(TokenType::DESC)) {
                node->order_by.asc = false;
            } else {
                node->order_by.asc = true; // ASC is default
            }
        }
        
        // Parse optional LIMIT clause (e.g. LIMIT 5)
        if (Match(TokenType::LIMIT)) {
            node->limit.has_limit = true;
            Token limit_token = Consume(TokenType::NUMBER, "Expect numeric limit value");
            node->limit.limit_count = std::stoi(limit_token.text);
        }
        
        return node;
    }
    
    /**
     * @brief Parses UPDATE table SET col = val WHERE condition statements.
     */
    std::unique_ptr<ASTNode> ParseUpdate() {
        auto node = std::make_unique<UpdateNode>();
        Token table_token = Consume(TokenType::IDENTIFIER, "Expect table name to update");
        node->table_name = table_token.text;
        
        Consume(TokenType::SET, "Expect 'SET' clause");
        Token col_token = Consume(TokenType::IDENTIFIER, "Expect column name to set");
        node->set_column = col_token.text;
        
        Consume(TokenType::EQUAL, "Expect '=' in SET clause");
        
        Token val_token = Peek();
        if (val_token.type == TokenType::NUMBER || val_token.type == TokenType::STRING_LITERAL) {
            node->set_value = val_token.text;
            Advance();
        } else {
            throw std::runtime_error("Expect literal value in SET clause");
        }
        
        // Safety restriction: UPDATE queries must enforce a WHERE clause to avoid mass corruption
        Consume(TokenType::WHERE, "Expect 'WHERE' clause for UPDATE statement");
        node->where.is_valid = true;
        Token where_col = Consume(TokenType::IDENTIFIER, "Expect column name in WHERE");
        node->where.col_name = where_col.text;
        
        Consume(TokenType::EQUAL, "Expect '=' in WHERE clause for UPDATE");
        node->where.op = TokenType::EQUAL;
        
        Token where_val = Peek();
        if (where_val.type == TokenType::NUMBER || where_val.type == TokenType::STRING_LITERAL) {
            node->where.value = where_val.text;
            Advance();
        } else {
            throw std::runtime_error("Expect comparison value in WHERE");
        }
        
        return node;
    }
    
    /**
     * @brief Parses DELETE FROM table WHERE condition statements.
     */
    std::unique_ptr<ASTNode> ParseDelete() {
        Consume(TokenType::FROM, "Expect 'FROM' after 'DELETE'");
        auto node = std::make_unique<DeleteNode>();
        Token table_token = Consume(TokenType::IDENTIFIER, "Expect table name");
        node->table_name = table_token.text;
        
        // Safety restriction: DELETE queries must enforce a WHERE clause to avoid clearing all rows
        Consume(TokenType::WHERE, "Expect 'WHERE' clause for DELETE statement");
        node->where.is_valid = true;
        Token where_col = Consume(TokenType::IDENTIFIER, "Expect column name in WHERE");
        node->where.col_name = where_col.text;
        
        Consume(TokenType::EQUAL, "Expect '=' in WHERE clause for DELETE");
        node->where.op = TokenType::EQUAL;
        
        Token where_val = Peek();
        if (where_val.type == TokenType::NUMBER || where_val.type == TokenType::STRING_LITERAL) {
            node->where.value = where_val.text;
            Advance();
        } else {
            throw std::runtime_error("Expect comparison value in WHERE");
        }
        
        return node;
    }
};

#endif // PARSER_H
