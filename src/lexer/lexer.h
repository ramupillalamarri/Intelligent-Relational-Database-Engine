#ifndef LEXER_H
#define LEXER_H

#include <string>
#include <vector>

/**
 * @brief Token types representing all keywords, identifiers, symbols, and operators supported by the parser.
 */
enum class TokenType {
    // SQL Keywords
    SELECT, FROM, WHERE, ORDER, BY, JOIN, ON, INSERT, INTO, VALUES, CREATE, TABLE, INDEX, INT_TYPE, TEXT_TYPE, UPDATE, SET, DELETE, LIMIT, ASC, DESC,
    
    // Identifiers & Literal Payloads
    IDENTIFIER, NUMBER, STRING_LITERAL,
    
    // Comparison Operators
    EQUAL, GREATER, LESS, GREATER_EQUAL, LESS_EQUAL, NOT_EQUAL,
    
    // SQL Statement Syntax Symbols
    COMMA, SEMICOLON, LPAREN, RPAREN, STAR,
    
    // Control Types
    END_OF_FILE, INVALID
};

/**
 * @brief Represents a single token extracted from the raw SQL string.
 */
struct Token {
    TokenType type;    // Typification of this token
    std::string text;  // Raw lexeme text slice
    int line;          // The line number inside SQL script (for error diagnostics)
};

/**
 * @brief The Lexer (Tokenizer) partitions raw SQL text into semantic tokens.
 */
class Lexer {
private:
    std::string src_;  // Source SQL statement text
    size_t pos_ = 0;   // Character scanner pointer index
    int line_ = 1;     // Tracks parser line numbers
    
    /**
     * @brief Peeks at the current character without advancing the cursor.
     */
    char Peek() const {
        if (pos_ >= src_.size()) return '\0';
        return src_[pos_];
    }
    
    /**
     * @brief Consumes and returns the current character, advancing the position.
     */
    char Advance() {
        if (pos_ >= src_.size()) return '\0';
        char c = src_[pos_++];
        if (c == '\n') line_++;
        return c;
    }
    
public:
    /**
     * @brief Construct a new Lexer object.
     * @param src The SQL query string.
     */
    Lexer(const std::string& src) : src_(src) {}
    
    /**
     * @brief Loops and extracts all tokens until END_OF_FILE is encountered.
     * @return std::vector<Token> Token list representing the program.
     */
    std::vector<Token> Tokenize() {
        std::vector<Token> tokens;
        while (true) {
            Token t = NextToken();
            tokens.push_back(t);
            if (t.type == TokenType::END_OF_FILE) break;
        }
        return tokens;
    }
    
    /**
     * @brief Matches and returns the next Token in the stream.
     * Skips whitespace and SQL single-line comments (`--`).
     */
    Token NextToken() {
        while (Peek() != '\0') {
            char c = Peek();
            
            // Skip Whitespace
            if (std::isspace(c)) {
                Advance();
                continue;
            }
            
            // Comment Stripping: Ignore everything after `--` until a newline is reached
            if (c == '-' && pos_ + 1 < src_.size() && src_[pos_ + 1] == '-') {
                while (Peek() != '\n' && Peek() != '\0') {
                    Advance();
                }
                continue;
            }
            
            // Symbols mapping
            if (c == ',') { Advance(); return Token{TokenType::COMMA, ",", line_}; }
            if (c == ';') { Advance(); return Token{TokenType::SEMICOLON, ";", line_}; }
            if (c == '(') { Advance(); return Token{TokenType::LPAREN, "(", line_}; }
            if (c == ')') { Advance(); return Token{TokenType::RPAREN, ")", line_}; }
            if (c == '*') { Advance(); return Token{TokenType::STAR, "*", line_}; }
            
            // Operators mapping
            if (c == '=') { Advance(); return Token{TokenType::EQUAL, "=", line_}; }
            if (c == '>') {
                Advance();
                if (Peek() == '=') { Advance(); return Token{TokenType::GREATER_EQUAL, ">=", line_}; }
                return Token{TokenType::GREATER, ">", line_};
            }
            if (c == '<') {
                Advance();
                if (Peek() == '=') { Advance(); return Token{TokenType::LESS_EQUAL, "<=", line_}; }
                if (Peek() == '>') { Advance(); return Token{TokenType::NOT_EQUAL, "<>", line_}; }
                return Token{TokenType::LESS, "<", line_};
            }
            if (c == '!') {
                Advance();
                if (Peek() == '=') { Advance(); return Token{TokenType::NOT_EQUAL, "!=", line_}; }
                return Token{TokenType::INVALID, "!", line_};
            }
            
            // String Literals: Parse single-quoted string values ('like this')
            if (c == '\'') {
                Advance(); // Consume opening quote
                std::string str = "";
                while (Peek() != '\'' && Peek() != '\0') {
                    str += Advance();
                }
                if (Peek() == '\'') {
                    Advance(); // Consume closing quote
                    return Token{TokenType::STRING_LITERAL, str, line_};
                }
                return Token{TokenType::INVALID, "Unterminated string literal: " + str, line_};
            }
            
            // Numeric Literals: Parse integers or decimals
            if (std::isdigit(c)) {
                std::string num = "";
                while (std::isdigit(Peek()) || Peek() == '.') {
                    num += Advance();
                }
                return Token{TokenType::NUMBER, num, line_};
            }
            
            // Identifiers & Keywords: Match words and check against known SQL keywords
            if (std::isalpha(c) || c == '_') {
                std::string ident = "";
                while (std::isalnum(Peek()) || Peek() == '_' || Peek() == '.') {
                    ident += Advance();
                }
                
                // Case-insensitive keywords comparison
                std::string upper_ident = "";
                for (char ch : ident) upper_ident += std::toupper(ch);
                
                if (upper_ident == "SELECT") return Token{TokenType::SELECT, ident, line_};
                if (upper_ident == "FROM") return Token{TokenType::FROM, ident, line_};
                if (upper_ident == "WHERE") return Token{TokenType::WHERE, ident, line_};
                if (upper_ident == "ORDER") return Token{TokenType::ORDER, ident, line_};
                if (upper_ident == "BY") return Token{TokenType::BY, ident, line_};
                if (upper_ident == "JOIN") return Token{TokenType::JOIN, ident, line_};
                if (upper_ident == "ON") return Token{TokenType::ON, ident, line_};
                if (upper_ident == "INSERT") return Token{TokenType::INSERT, ident, line_};
                if (upper_ident == "INTO") return Token{TokenType::INTO, ident, line_};
                if (upper_ident == "VALUES") return Token{TokenType::VALUES, ident, line_};
                if (upper_ident == "CREATE") return Token{TokenType::CREATE, ident, line_};
                if (upper_ident == "TABLE") return Token{TokenType::TABLE, ident, line_};
                if (upper_ident == "INDEX") return Token{TokenType::INDEX, ident, line_};
                if (upper_ident == "INT" || upper_ident == "INTEGER") return Token{TokenType::INT_TYPE, ident, line_};
                if (upper_ident == "TEXT" || upper_ident == "VARCHAR") return Token{TokenType::TEXT_TYPE, ident, line_};
                if (upper_ident == "UPDATE") return Token{TokenType::UPDATE, ident, line_};
                if (upper_ident == "SET") return Token{TokenType::SET, ident, line_};
                if (upper_ident == "DELETE") return Token{TokenType::DELETE, ident, line_};
                if (upper_ident == "LIMIT") return Token{TokenType::LIMIT, ident, line_};
                if (upper_ident == "ASC") return Token{TokenType::ASC, ident, line_};
                if (upper_ident == "DESC") return Token{TokenType::DESC, ident, line_};
                
                return Token{TokenType::IDENTIFIER, ident, line_};
            }
            
            // Unknown characters are returned as invalid tokens to be caught during parsing
            std::string err_text(1, Advance());
            return Token{TokenType::INVALID, err_text, line_};
        }
        
        return Token{TokenType::END_OF_FILE, "", line_};
    }
};

#endif // LEXER_H
