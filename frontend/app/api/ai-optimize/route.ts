import { NextResponse } from 'next/server';

interface Column {
  name: string;
  type: string;
  indexed: boolean;
}

interface Table {
  name: string;
  columns: Column[];
}

export async function POST(request: Request) {
  try {
    const { query, schema } = await request.json();
    if (!query) {
      return NextResponse.json({ success: false, error: 'Query is empty' }, { status: 400 });
    }

    const tables: Table[] = schema?.tables || [];
    const suggestions: any[] = [];
    let queryLower = query.toLowerCase().trim();

    // 1. Analyze "SELECT *"
    if (queryLower.includes('select *') || queryLower.includes('select  *')) {
      // Find table name
      const fromMatch = queryLower.match(/from\s+([a-zA-Z_0-9]+)/);
      const tableName = fromMatch ? fromMatch[1] : 'table';
      const targetTable = tables.find(t => t.name.toLowerCase() === tableName.toLowerCase());
      const columnNames = targetTable ? targetTable.columns.map(c => c.name).join(', ') : 'col1, col2';
      
      suggestions.push({
        type: 'warning',
        title: 'Avoid SELECT * Projections',
        impact: 'Lowers Disk I/O & Memory Footprint',
        description: 'Using "SELECT *" forces the storage engine to read and deserialize all columns from the slotted pages, increasing memory allocations. Restricting projections to explicit columns saves CPU cycles and memory bandwidth.',
        suggestion: `SELECT ${columnNames} FROM ${tableName} ...`,
        estimated_speedup: '15-20%'
      });
    }

    // 2. Analyze WHERE clause and index status
    if (queryLower.includes('where')) {
      const wherePart = queryLower.split('where')[1];
      // Match column name: typically identifier followed by operator
      const colMatch = wherePart.match(/^\s*([a-zA-Z_0-9\.]+)\s*(=|>|<|>=|<=|!=|<>)/);
      if (colMatch) {
        let colName = colMatch[1].trim();
        if (colName.includes('.')) {
          colName = colName.split('.')[1]; // Remove table prefix
        }

        const fromPart = queryLower.split('from')[1]?.split('where')[0] || '';
        const tablesInQuery = tables.filter(t => fromPart.includes(t.name.toLowerCase()));
        
        for (const table of tablesInQuery) {
          const column = table.columns.find(c => c.name.toLowerCase() === colName.toLowerCase());
          if (column) {
            if (!column.indexed) {
              suggestions.push({
                type: 'optimization',
                title: `Missing Index on Filter Column [${table.name}.${column.name}]`,
                impact: 'Reduces search complexity from O(N) to O(log N)',
                description: `The query filters on "${column.name}", but no index is defined. DBForge is forced to run a Sequential Scan, loading every physical page from disk. Creating a B+ Tree index will let the Optimizer perform an Index Scan, fetching records in logarithmic time.`,
                suggestion: `CREATE INDEX ON ${table.name} (${column.name});`,
                estimated_speedup: '60-80%'
              });
            } else if (queryLower.includes('=')) {
              // Indexed and query is equal
              suggestions.push({
                type: 'info',
                title: `Leveraging B+ Tree Index on [${table.name}.${column.name}]`,
                impact: 'Active Index Scan Optimized',
                description: `Excellent! The Optimizer has successfully selected the B+ Tree index for "${column.name}". This query executes in logarithmic time instead of scanning all file segments on disk.`,
                suggestion: null,
                estimated_speedup: '0% (Already Optimized)'
              });
            }
          }
        }
      }
    }

    // 3. Analyze JOIN clauses
    if (queryLower.includes('join') && queryLower.includes('on')) {
      const onPart = queryLower.split('on')[1]?.split('where')[0] || '';
      const joinKeys = onPart.match(/([a-zA-Z_0-9\.]+)\s*=\s*([a-zA-Z_0-9\.]+)/);
      if (joinKeys) {
        const key1 = joinKeys[1].trim();
        const key2 = joinKeys[2].trim();
        
        const resolveColumn = (keyStr: string) => {
          const parts = keyStr.split('.');
          if (parts.length === 2) {
            const tableName = parts[0];
            const colName = parts[1];
            const table = tables.find(t => t.name.toLowerCase() === tableName.toLowerCase());
            const column = table?.columns.find(c => c.name.toLowerCase() === colName.toLowerCase());
            return { table, column };
          }
          return null;
        };

        const colInfo1 = resolveColumn(key1);
        const colInfo2 = resolveColumn(key2);

        [colInfo1, colInfo2].forEach(info => {
          if (info && info.table && info.column && !info.column.indexed) {
            suggestions.push({
              type: 'optimization',
              title: `Missing Index on Join Key [${info.table.name}.${info.column.name}]`,
              impact: 'Speeds up Nested-Loop Joins',
              description: `This query performs an inner join on "${info.column.name}", but this column is not indexed. For every row in the outer table, DBForge must scan the entire inner table file. Indexing the join keys allows fast index point lookups during the Volcano executor join loop.`,
              suggestion: `CREATE INDEX ON ${info.table.name} (${info.column.name});`,
              estimated_speedup: '50-70%'
            });
          }
        });
      }
    }

    // 4. Default suggestion if query is fully optimized
    if (suggestions.length === 0) {
      suggestions.push({
        type: 'info',
        title: 'Query Structure Validated',
        impact: 'Optimal configuration',
        description: 'No obvious performance bottlenecks detected. The current indexes and projected columns are configured optimally for the storage engine.',
        suggestion: null,
        estimated_speedup: 'Optimal'
      });
    }

    return NextResponse.json({
      success: true,
      query,
      suggestions
    });
  } catch (err: any) {
    return NextResponse.json({ success: false, error: err.message }, { status: 500 });
  }
}
