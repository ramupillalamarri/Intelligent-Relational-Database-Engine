#ifndef SERIALIZATION_H
#define SERIALIZATION_H

#include "catalog/catalog.h"
#include <vector>
#include <string>
#include <cstring>
#include <stdexcept>

/**
 * @brief Serializes a vector of string values into a compact raw binary byte buffer.
 * 
 * Formatting:
 * - `INT` Type: Serialized as a raw 4-byte block.
 * - `TEXT` Type: Serialized as a 2-byte length prefix (uint16_t) followed by the raw characters.
 * 
 * @param schema The table column structure schema.
 * @param row_vals Vector of string values corresponding to the row attributes.
 * @return std::vector<char> Binary byte buffer containing the serialized row.
 */
inline std::vector<char> SerializeRow(const Schema& schema, const std::vector<std::string>& row_vals) {
    std::vector<char> buffer;
    
    for (size_t i = 0; i < schema.columns.size(); ++i) {
        const auto& col = schema.columns[i];
        std::string val_str = "";
        if (i < row_vals.size()) {
            val_str = row_vals[i];
        }
        
        if (col.type == DataType::INT) {
            int val = 0;
            if (!val_str.empty()) {
                try {
                    val = std::stoi(val_str);
                } catch (...) {}
            }
            char bytes[sizeof(int)];
            std::memcpy(bytes, &val, sizeof(int));
            buffer.insert(buffer.end(), bytes, bytes + sizeof(int));
        } else {
            uint16_t len = static_cast<uint16_t>(val_str.size());
            char len_bytes[sizeof(uint16_t)];
            std::memcpy(len_bytes, &len, sizeof(uint16_t));
            buffer.insert(buffer.end(), len_bytes, len_bytes + sizeof(uint16_t));
            buffer.insert(buffer.end(), val_str.begin(), val_str.end());
        }
    }
    
    return buffer;
}

/**
 * @brief Deserializes a binary byte buffer back into a vector of human-readable strings.
 * Reads the schema definition to know how many bytes to read and parse for each attribute.
 * 
 * @param schema The table column structure schema.
 * @param data The raw binary record payload read from a page.
 * @return std::vector<std::string> Deserialized row values.
 */
inline std::vector<std::string> DeserializeRow(const Schema& schema, const std::vector<char>& data) {
    std::vector<std::string> row_vals;
    size_t offset = 0; // Running byte offset pointer
    
    for (const auto& col : schema.columns) {
        if (offset > data.size()) {
            row_vals.push_back("");
            continue;
        }
        
        if (col.type == DataType::INT) {
            if (offset + sizeof(int) > data.size()) {
                row_vals.push_back("0");
                continue;
            }
            int val;
            std::memcpy(&val, data.data() + offset, sizeof(int));
            row_vals.push_back(std::to_string(val));
            offset += sizeof(int);
        } else {
            if (offset + sizeof(uint16_t) > data.size()) {
                row_vals.push_back("");
                continue;
            }
            uint16_t len;
            std::memcpy(&len, data.data() + offset, sizeof(uint16_t));
            offset += sizeof(uint16_t);
            
            if (offset + len > data.size()) {
                row_vals.push_back("");
                continue;
            }
            std::string val_str(data.data() + offset, len);
            row_vals.push_back(val_str);
            offset += len;
        }
    }
    
    return row_vals;
}

#endif // SERIALIZATION_H
