#ifndef PARSER_H
#define PARSER_H

#include "ast.h"
#include "lexer/lexer.h"
#include <vector>
#include <memory>
#include <stdexcept>
#include <iostream>

class Parser {
private:
    std::vector<Token> tokens_;
    size_t pos_ = 0;
    
    const Token& Peek() const {
        if (pos_ >= tokens_.size()) return tokens_.back();
        return tokens_[pos_];
    }
    
    const Token& Previous() const {
        if (pos_ == 0) return tokens_[0];
        return tokens_[pos_ - 1];
    }
    
    bool IsAtEnd() const {
        return Peek().type == TokenType::END_OF_FILE;
    }
    
    Token Advance() {
        if (!IsAtEnd()) pos_++;
        return Previous();
    }
    
    bool Check(TokenType type) const {
        if (IsAtEnd()) return false;
        return Peek().type == type;
    }
    
    bool Match(TokenType type) {
        if (Check(type)) {
            Advance();
            return true;
        }
        return false;
    }
    
    Token Consume(TokenType type, const std::string& message) {
        if (Check(type)) return Advance();
        throw std::runtime_error("Parser Error: " + message + " at token '" + Peek().text + "'");
    }
    
public:
    Parser(const std::vector<Token>& tokens) : tokens_(tokens) {}
    
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
            // CREATE INDEX ON table_name (col_name)
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
    
    std::unique_ptr<ASTNode> ParseSelect() {
        auto node = std::make_unique<SelectNode>();
        
        // Parse Columns
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
        
        // Parse JOIN (optional, e.g. JOIN table2 ON table1.col1 = table2.col2)
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
        
        // Parse WHERE (optional, e.g. WHERE age > 20)
        if (Match(TokenType::WHERE)) {
            node->where.is_valid = true;
            Token col_token = Consume(TokenType::IDENTIFIER, "Expect column name in WHERE");
            node->where.col_name = col_token.text;
            
            Token op_token = Advance(); // Operator token
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
        
        // Parse ORDER BY (optional)
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
                node->order_by.asc = true; // Default
            }
        }
        
        // Parse LIMIT (optional)
        if (Match(TokenType::LIMIT)) {
            node->limit.has_limit = true;
            Token limit_token = Consume(TokenType::NUMBER, "Expect numeric limit value");
            node->limit.limit_count = std::stoi(limit_token.text);
        }
        
        return node;
    }
    
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
        
        // Update must have WHERE clause for safety/filtering
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
    
    std::unique_ptr<ASTNode> ParseDelete() {
        Consume(TokenType::FROM, "Expect 'FROM' after 'DELETE'");
        auto node = std::make_unique<DeleteNode>();
        Token table_token = Consume(TokenType::IDENTIFIER, "Expect table name");
        node->table_name = table_token.text;
        
        // Delete must have WHERE clause
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
