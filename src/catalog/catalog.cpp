#include "catalog.h"
#include <fstream>
#include <sstream>
#include <iostream>

std::string DataTypeToString(DataType type) {
    return (type == DataType::INT) ? "INT" : "TEXT";
}

DataType StringToDataType(const std::string& str) {
    if (str == "INT" || str == "INTEGER") {
        return DataType::INT;
    }
    return DataType::TEXT;
}

Catalog::Catalog(const std::string& meta_path) : meta_path_(meta_path) {}

bool Catalog::Load() {
    std::ifstream file(meta_path_);
    if (!file.is_open()) {
        // If file doesn't exist, it's fine, we start fresh.
        return true;
    }
    
    tables_.clear();
    std::string line;
    std::string current_table = "";
    std::vector<Column> current_columns;
    
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        std::stringstream ss(line);
        std::string tag;
        ss >> tag;
        
        if (tag == "TABLE") {
            if (!current_table.empty()) {
                tables_[current_table] = Schema{current_columns};
                current_columns.clear();
            }
            ss >> current_table;
        } else if (tag == "COLUMN") {
            std::string col_name, type_str;
            int has_idx = 0;
            ss >> col_name >> type_str >> has_idx;
            DataType type = StringToDataType(type_str);
            current_columns.push_back(Column{col_name, type, has_idx == 1});
        }
    }
    if (!current_table.empty()) {
        tables_[current_table] = Schema{current_columns};
    }
    file.close();
    return true;
}

bool Catalog::Save() {
    std::ofstream file(meta_path_);
    if (!file.is_open()) {
        std::cerr << "Failed to open catalog file for writing: " << meta_path_ << std::endl;
        return false;
    }
    for (const auto& pair : tables_) {
        file << "TABLE " << pair.first << "\n";
        for (const auto& col : pair.second.columns) {
            file << "COLUMN " << col.name << " " << DataTypeToString(col.type) << " " << (col.has_index ? 1 : 0) << "\n";
        }
        file << "\n";
    }
    file.close();
    return true;
}

bool Catalog::CreateTable(const std::string& table_name, const std::vector<Column>& columns) {
    if (TableExists(table_name)) {
        return false;
    }
    tables_[table_name] = Schema{columns};
    return Save();
}

bool Catalog::TableExists(const std::string& table_name) const {
    return tables_.find(table_name) != tables_.end();
}

const Schema& Catalog::GetSchema(const std::string& table_name) const {
    auto it = tables_.find(table_name);
    if (it == tables_.end()) {
        static Schema empty_schema;
        return empty_schema;
    }
    return it->second;
}

bool Catalog::CreateIndex(const std::string& table_name, const std::string& col_name) {
    auto it = tables_.find(table_name);
    if (it == tables_.end()) return false;
    
    for (auto& col : it->second.columns) {
        if (col.name == col_name) {
            col.has_index = true;
            return Save();
        }
    }
    return false;
}

bool Catalog::HasIndex(const std::string& table_name, const std::string& col_name) const {
    auto it = tables_.find(table_name);
    if (it == tables_.end()) return false;
    
    for (const auto& col : it->second.columns) {
        if (col.name == col_name) {
            return col.has_index;
        }
    }
    return false;
}
