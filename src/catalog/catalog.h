#ifndef CATALOG_H
#define CATALOG_H

#include <string>
#include <vector>
#include <unordered_map>

/**
 * @brief Represents the data types supported by the database engine columns.
 */
enum class DataType {
    INT,  // 4-byte signed binary integer
    TEXT  // Length-prefixed variable-width string
};

// Conversions between DataType enums and their human-readable strings
std::string DataTypeToString(DataType type);
DataType StringToDataType(const std::string& str);

/**
 * @brief Represents a single column definition in a table.
 */
struct Column {
    std::string name;    // Name of the column
    DataType type;       // Data type of the column
    bool has_index = false; // Flag indicating if a B+ Tree index is built on this column
};

/**
 * @brief Represents the structural schema layout of a table (collection of columns).
 */
struct Schema {
    std::vector<Column> columns;
    
    /**
     * @brief Gets the index position of a column by its name.
     * @param col_name Name of the column.
     * @return int Index (0-based) or -1 if the column is not found.
     */
    int GetColIndex(const std::string& col_name) const {
        for (size_t i = 0; i < columns.size(); ++i) {
            if (columns[i].name == col_name) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }
};

/**
 * @brief The Catalog system manages metadata for all tables, columns, and index allocations.
 * It persists schemas to a metadata text file (e.g. data/catalog.meta) so schemas survive restarts.
 */
class Catalog {
private:
    std::string meta_path_; // Path to metadata metafile
    std::unordered_map<std::string, Schema> tables_; // Maps table_name -> Schema structure
    
public:
    /**
     * @brief Construct a new Catalog manager.
     * @param meta_path Path to the metadata metafile on disk.
     */
    Catalog(const std::string& meta_path);
    
    /**
     * @brief Loads all table schemas and metadata from the meta file into memory.
     * @return true If loaded successfully (or file does not exist yet).
     * @return false If read failed.
     */
    bool Load();
    
    /**
     * @brief Saves all table schemas and metadata from memory back to the disk meta file.
     * @return true If save succeeded.
     * @return false If write failed.
     */
    bool Save();
    
    /**
     * @brief Registers a new table in the system catalog.
     * @param table_name Name of the table.
     * @param columns Vector of columns defining the table.
     * @return true If table was created successfully.
     * @return false If the table already exists.
     */
    bool CreateTable(const std::string& table_name, const std::vector<Column>& columns);
    
    /**
     * @brief Checks if a table exists in the system.
     * @param table_name Name of the table.
     * @return true If the table exists.
     */
    bool TableExists(const std::string& table_name) const;
    
    /**
     * @brief Gets the schema definition for a table.
     * @param table_name Name of the table.
     * @return const Schema& Reference to the schema.
     */
    const Schema& GetSchema(const std::string& table_name) const;
    
    /**
     * @brief Returns a reference to all registered tables map.
     */
    const std::unordered_map<std::string, Schema>& GetTables() const { return tables_; }
    
    /**
     * @brief Registers that an index has been built on a column in the catalog.
     * @param table_name Table name.
     * @param col_name Column name.
     * @return true If successfully updated.
     */
    bool CreateIndex(const std::string& table_name, const std::string& col_name);
    
    /**
     * @brief Checks if a column has an index active.
     * @param table_name Table name.
     * @param col_name Column name.
     * @return true If an index is active.
     */
    bool HasIndex(const std::string& table_name, const std::string& col_name) const;
};

#endif // CATALOG_H
