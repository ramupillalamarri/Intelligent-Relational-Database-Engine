#include "catalog/catalog.h"
#include "storage/storage_manager.h"
#include "index/index_manager.h"
#include "lexer/lexer.h"
#include "parser/parser.h"
#include "planner/planner.h"
#include "executor/execution_engine.h"
#include <iostream>
#include <string>
#include <chrono>
#include <sstream>
#include <fstream>

/**
 * @brief Prints schema information of all catalog tables formatted as JSON to stdout.
 * Used by the Next.js API to populate the sidebar Schema Explorer.
 * @param catalog Catalog containing table and column details.
 */
void PrintSchemaJSON(const Catalog& catalog) {
    std::cout << "{\n";
    std::cout << "  \"tables\": [\n";
    bool first_table = true;
    for (const auto& pair : catalog.GetTables()) {
        if (!first_table) std::cout << ",\n";
        first_table = false;
        
        std::cout << "    {\n";
        std::cout << "      \"name\": \"" << pair.first << "\",\n";
        std::cout << "      \"columns\": [\n";
        bool first_col = true;
        for (const auto& col : pair.second.columns) {
            if (!first_col) std::cout << ",\n";
            first_col = false;
            std::cout << "        {\"name\": \"" << col.name << "\", \"type\": \"" 
                      << DataTypeToString(col.type) << "\", \"indexed\": " 
                      << (col.has_index ? "true" : "false") << "}";
        }
        std::cout << "\n      ]\n";
        std::cout << "    }";
    }
    std::cout << "\n  ]\n";
    std::cout << "}\n";
}

/**
 * @brief Prints storage and row count statistics of all tables as JSON to stdout.
 * Scans all pages in data tables to count non-deleted slot entries.
 * Used by the Next.js API to populate the Storage Metrics dashboard.
 */
void PrintStatsJSON(const Catalog& catalog, StorageManager& sm) {
    std::cout << "{\n";
    std::cout << "  \"tables\": [\n";
    bool first_table = true;
    for (const auto& pair : catalog.GetTables()) {
        if (!first_table) std::cout << ",\n";
        first_table = false;
        
        std::string name = pair.first;
        uint32_t page_count = sm.GetPageCount(name);
        uint64_t file_size = page_count * PAGE_SIZE;
        
        // Count active rows inside the table files
        int row_count = 0;
        Page page;
        for (uint32_t p = 0; p < page_count; ++p) {
            if (sm.ReadPage(name, p, page)) {
                uint16_t record_count = page.GetRecordCount();
                for (uint16_t s = 0; s < record_count; ++s) {
                    std::vector<char> record_data;
                    if (page.GetRecord(s, record_data)) {
                        row_count++;
                    }
                }
            }
        }
        
        std::cout << "    {\n";
        std::cout << "      \"name\": \"" << name << "\",\n";
        std::cout << "      \"page_count\": " << page_count << ",\n";
        std::cout << "      \"file_size_bytes\": " << file_size << ",\n";
        std::cout << "      \"row_count\": " << row_count << "\n";
        std::cout << "    }";
    }
    std::cout << "\n  ]\n";
    std::cout << "}\n";
}

/**
 * @brief Entrypoint of the DBForge database CLI interface.
 * Parses command-line flags and routes queries through the database engine.
 */
int main(int argc, char* argv[]) {
    // Define relative paths for database filesystem targets
    std::string catalog_file = "data/catalog.meta";
    std::string data_directory = "data";
    std::string index_directory = "indexes";
    
    // Boot up database sub-systems
    Catalog catalog(catalog_file);
    catalog.Load();
    
    StorageManager sm(data_directory);
    IndexManager im(index_directory);
    ExecutionEngine engine(catalog, sm, im);
    
    // Enforce argument validations
    if (argc < 2) {
        std::cout << "{\"success\": false, \"error\": \"Missing arguments. Use --query, --schema, or --stats\"}\n";
        return 1;
    }
    
    std::string arg1 = argv[1];
    
    // Flag Route 1: Schema metadata export
    if (arg1 == "--schema") {
        PrintSchemaJSON(catalog);
        return 0;
    } 
    
    // Flag Route 2: Storage metrics export
    else if (arg1 == "--stats") {
        PrintStatsJSON(catalog, sm);
        return 0;
    } 
    
    // Flag Route 3: SQL Query execution
    else if (arg1 == "--query") {
        if (argc < 3) {
            std::cout << "{\"success\": false, \"error\": \"--query flag requires an SQL string argument\"}\n";
            return 1;
        }
        std::string sql = argv[2];
        
        auto start_time = std::chrono::high_resolution_clock::now();
        
        // Step 1: Tokenize query (Lexer)
        Lexer lexer(sql);
        std::vector<Token> tokens = lexer.Tokenize();
        
        // Assert token validations
        for (const auto& token : tokens) {
            if (token.type == TokenType::INVALID) {
                std::cout << "{\"success\": false, \"error\": \"Lexical Error: " << token.text << "\"}\n";
                return 0;
            }
        }
        
        // Step 2: Compile syntax tree (Parser)
        Parser parser(tokens);
        std::unique_ptr<ASTNode> ast = parser.Parse();
        if (!ast) {
            std::cout << "{\"success\": false, \"error\": \"SQL Parsing Error: Check syntax structure\"}\n";
            return 0;
        }
        
        // Step 3: Physical planning and optimization (only for SELECT statements)
        std::shared_ptr<PlanNode> physical_plan = nullptr;
        if (ast->GetType() == ASTType::SELECT) {
            auto select_node = static_cast<SelectNode*>(ast.get());
            
            // Validate table references against Catalog schema
            if (!catalog.TableExists(select_node->table_name)) {
                std::cout << "{\"success\": false, \"error\": \"Table '" << select_node->table_name << "' does not exist\"}\n";
                return 0;
            }
            if (select_node->join.has_join && !catalog.TableExists(select_node->join.table_name)) {
                std::cout << "{\"success\": false, \"error\": \"Joined table '" << select_node->join.table_name << "' does not exist\"}\n";
                return 0;
            }
            
            Planner planner(catalog);
            physical_plan = planner.GeneratePlan(*select_node);
        }
        
        auto end_time = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double, std::milli> duration = end_time - start_time;
        
        // Step 4: Execute query statement and stream output to stdout
        std::string output = engine.ExecuteQuery(std::move(ast), physical_plan, duration.count());
        std::cout << output << "\n";
        return 0;
    }
    
    std::cout << "{\"success\": false, \"error\": \"Unknown argument: " << arg1 << "\"}\n";
    return 1;
}
