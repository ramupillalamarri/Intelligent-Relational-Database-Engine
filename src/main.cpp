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

// Print schema information of all tables
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

// Print storage and row count statistics
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
        
        // Count active rows
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

int main(int argc, char* argv[]) {
    // Filesystem targets
    std::string catalog_file = "data/catalog.meta";
    std::string data_directory = "data";
    std::string index_directory = "indexes";
    
    Catalog catalog(catalog_file);
    catalog.Load();
    
    StorageManager sm(data_directory);
    IndexManager im(index_directory);
    ExecutionEngine engine(catalog, sm, im);
    
    if (argc < 2) {
        std::cout << "{\"success\": false, \"error\": \"Missing arguments. Use --query, --schema, or --stats\"}\n";
        return 1;
    }
    
    std::string arg1 = argv[1];
    
    if (arg1 == "--schema") {
        PrintSchemaJSON(catalog);
        return 0;
    } 
    
    else if (arg1 == "--stats") {
        PrintStatsJSON(catalog, sm);
        return 0;
    } 
    
    else if (arg1 == "--query") {
        if (argc < 3) {
            std::cout << "{\"success\": false, \"error\": \"--query flag requires an SQL string argument\"}\n";
            return 1;
        }
        std::string sql = argv[2];
        
        auto start_time = std::chrono::high_resolution_clock::now();
        
        // 1. Lexer
        Lexer lexer(sql);
        std::vector<Token> tokens = lexer.Tokenize();
        
        // Check for lexer errors
        for (const auto& token : tokens) {
            if (token.type == TokenType::INVALID) {
                std::cout << "{\"success\": false, \"error\": \"Lexical Error: " << token.text << "\"}\n";
                return 0;
            }
        }
        
        // 2. Parser
        Parser parser(tokens);
        std::unique_ptr<ASTNode> ast = parser.Parse();
        if (!ast) {
            std::cout << "{\"success\": false, \"error\": \"SQL Parsing Error: Check syntax structure\"}\n";
            return 0;
        }
        
        // 3. Planner & Optimizer (if SELECT query)
        std::shared_ptr<PlanNode> physical_plan = nullptr;
        if (ast->GetType() == ASTType::SELECT) {
            auto select_node = static_cast<SelectNode*>(ast.get());
            
            // Check table existence before planning
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
        
        // 4. Execution Engine
        std::string output = engine.ExecuteQuery(std::move(ast), physical_plan, duration.count());
        std::cout << output << "\n";
        return 0;
    }
    
    std::cout << "{\"success\": false, \"error\": \"Unknown argument: " << arg1 << "\"}\n";
    return 1;
}
