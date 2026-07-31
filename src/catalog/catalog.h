#ifndef CATALOG_H
#define CATALOG_H

#include <string>
#include <vector>
#include <unordered_map>

enum class DataType {
    INT,
    TEXT
};

std::string DataTypeToString(DataType type);
DataType StringToDataType(const std::string& str);

struct Column {
    std::string name;
    DataType type;
    bool has_index = false;
};

struct Schema {
    std::vector<Column> columns;
    int GetColIndex(const std::string& col_name) const {
        for (size_t i = 0; i < columns.size(); ++i) {
            if (columns[i].name == col_name) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }
};

class Catalog {
private:
    std::string meta_path_;
    std::unordered_map<std::string, Schema> tables_;
    
public:
    Catalog(const std::string& meta_path);
    
    bool Load();
    bool Save();
    
    bool CreateTable(const std::string& table_name, const std::vector<Column>& columns);
    bool TableExists(const std::string& table_name) const;
    const Schema& GetSchema(const std::string& table_name) const;
    const std::unordered_map<std::string, Schema>& GetTables() const { return tables_; }
    
    bool CreateIndex(const std::string& table_name, const std::string& col_name);
    bool HasIndex(const std::string& table_name, const std::string& col_name) const;
};

#endif // CATALOG_H
