# DBForge Database Engine: Complete Learning Roadmap
## From Basics to Advanced Internals

To fully understand, explain, and defend this project in technical interviews, you need a mix of core programming skills, system design principles, and database engine theory. 

Here is the complete roadmap of **topics and technologies** you should learn, structured from **Basic** to **Advanced**.

---

## Phase 1: The Basics (Prerequisites)

Before diving into the database files, make sure you are comfortable with these core fundamentals.

### 1. C++ Programming Fundamentals
*   **Pointers and Memory Management**: Difference between Stack and Heap memory, manual allocation (`new`/`delete`), and Smart Pointers (`std::unique_ptr`, `std::shared_ptr`) which are used in the parser and planner ([ast.h](file:///c:/ramu/project/MINI_DB/src/parser/ast.h)).
*   **Object-Oriented Programming (OOP)**: Inheritance, virtual functions, and abstract base classes. You will see this in the Volcano Executor pattern ([executor.h](file:///c:/ramu/project/MINI_DB/src/executor/executor.h)) where all operators inherit from `AbstractExecutor`.
*   **Templates**: Writing type-generic code. This is essential for the generic B+ Tree which is templated over the key type: `template <typename KeyType> class BPlusTree` ([bplus_tree.h](file:///c:/ramu/project/MINI_DB/src/index/bplus_tree.h)).
*   **STL Containers**: Dynamic arrays (`std::vector`), lookup maps (`std::unordered_map`), and string parsing streams (`std::stringstream`).

### 2. SQL & Relational Database Basics
*   **SQL Dialect**: Understand standard SQL commands (DDL: `CREATE TABLE`, `CREATE INDEX`; DML: `SELECT`, `INSERT`, `UPDATE`, `DELETE`).
*   **Relational Concepts**: Primary keys, indexes, foreign keys, tables, schemas, and columns.
*   **Join Operations**: What an inner join does and how tables connect via shared keys.

### 3. Build Tools & Command Line
*   **CMake**: How modern C++ projects compile. Review [CMakeLists.txt](file:///c:/ramu/project/MINI_DB/CMakeLists.txt) to understand how `file(GLOB_RECURSE SOURCES)` locates all `.cpp` files to compile the executable.
*   **Standard I/O**: How programs receive input via command-line arguments (`argc`, `argv[]`) and write output to stdout.

---

## Phase 2: Intermediate (Serialization & Compilation)

These topics cover how SQL text gets compiled into code structures and how memory rows get turned into raw files.

### 1. Compilers: Lexing & Parsing
*   **Tokenization (Lexer)**: The process of reading a raw text string char-by-char and grouping characters into "tokens" (e.g. text `"SELECT"` -> token type `TokenType::SELECT`). Review [lexer.h](file:///c:/ramu/project/MINI_DB/src/lexer/lexer.h).
*   **Grammars & Parsers**: How to check if a sequence of tokens is syntactically valid (e.g., verifying that `SELECT` is followed by column names, then `FROM`, etc.).
*   **Recursive Descent Parsing**: A parsing technique where nested helper functions parse specific grammar components (e.g., `ParseSelect()`, `ParseInsert()`). Review [parser.h](file:///c:/ramu/project/MINI_DB/src/parser/parser.h).
*   **Abstract Syntax Trees (AST)**: Representing code commands as a tree structure of objects in memory. Review [ast.h](file:///c:/ramu/project/MINI_DB/src/parser/ast.h).

### 2. Binary File I/O & Serialization
*   **Binary vs. Plaintext**: Why databases save files as binary rather than JSON or CSV (binary is compact, fast, and allows instant random access).
*   **Data Serialization**: Encoding values into raw bytes:
    *   *Fixed-width integers* (4 bytes).
    *   *Variable-length strings* using a length-prefix (e.g., a 2-byte length header followed by string characters).
    *   Review [serialization.h](file:///c:/ramu/project/MINI_DB/src/utils/serialization.h).
*   **Random Access File I/O**: Reading and writing bytes at precise file offsets using C++ file streams (`seekg`, `seekp`). Review [storage_manager.cpp](file:///c:/ramu/project/MINI_DB/src/storage/storage_manager.cpp).

### 3. Next.js & React (Frontend)
*   **TypeScript / React Components**: Building dynamic web pages with React hooks (`useState`, `useEffect`).
*   **Node.js Process Execution**: Running external operating system binaries inside server APIs using `child_process.execFile` ([api/query/route.ts](file:///c:/ramu/project/MINI_DB/frontend/app/api/query/route.ts)).

---

## Phase 3: Advanced (Database Internals)

These are the advanced concepts that will impress database engineers in your interviews.

### 1. Physical Storage Engines
*   **Disk Blocks & Pages**: Why databases divide files into fixed-size units (usually 4KB or 8KB) to align with filesystem blocks, avoiding multiple disk read seek operations.
*   **Slotted-Page Architecture**:
    *   *Slot Array*: Gathers pointers (offsets) to rows, growing forward from the top of the page.
    *   *Row Payload*: Grows backward from the bottom of the page.
    *   *Logical Deletion*: Deleting a record without shifting remaining rows (which would ruin indexing pointers).
    *   Review [page.h](file:///c:/ramu/project/MINI_DB/src/storage/page.h).

### 2. Index Structures (B+ Trees)
*   **B+ Tree Theory**: A self-balancing search tree where:
    *   Internal nodes only store keys (guarantees high fan-out, reducing tree height).
    *   Leaf nodes store actual values (in our case, `RecordID` pointers) and are linked together (`next` and `prev` pointers) for fast range scans.
*   **Node Splitting**: How a node splits when keys exceed its maximum capacity (degree/order).
*   Review [bplus_tree.h](file:///c:/ramu/project/MINI_DB/src/index/bplus_tree.h).

### 3. Query Execution Engine (Volcano Model)
*   **Volcano Iterator Pattern**: The industry-standard query execution framework where each relational operator is an iterator implementing:
    *   `Init()`: Prepare states.
    *   `Next()`: Stream the next row from disk/children.
    *   `Close()`: Cleanup.
*   **Volcano Operators**:
    *   `SeqScan` & `IndexScan`: Iterating table files or reading B+ trees.
    *   `Filter` & `Project`: Restricting rows or columns.
    *   `Nested Loop Join`: The baseline algorithm for matching rows between tables.
    *   Review [executor.h](file:///c:/ramu/project/MINI_DB/src/executor/executor.h).

### 4. Query Planner & Optimizer
*   **Logical vs. Physical Plans**: Generating a plan tree of what the query wants to do, and choosing the most efficient way to physically execute it.
*   **Rule-Based Optimizer (RBO)**: A module that automatically rewrites query plans based on predefined rules (e.g., "If an index exists on the filter column, rewrite SeqScan to IndexScan").
*   Review [planner.h](file:///c:/ramu/project/MINI_DB/src/planner/planner.h).
