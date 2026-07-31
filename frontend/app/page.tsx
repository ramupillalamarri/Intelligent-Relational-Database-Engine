'use client';

import { useState, useEffect } from 'react';

interface Column {
  name: string;
  type: string;
  indexed: boolean;
}

interface TableSchema {
  name: string;
  columns: Column[];
}

interface SchemaData {
  tables: TableSchema[];
}

interface TableStats {
  name: string;
  page_count: number;
  file_size_bytes: number;
  row_count: number;
}

interface StatsData {
  tables: TableStats[];
}

interface PlanNode {
  type: string;
  detail: string;
  columns: string[];
  left: PlanNode | null;
  right: PlanNode | null;
}

interface QueryResult {
  success: boolean;
  message?: string;
  error?: string;
  latency_ms?: number;
  columns?: string[];
  records?: string[][];
  count?: number;
  execution_plan?: PlanNode;
}

interface AISuggestion {
  type: 'warning' | 'optimization' | 'info';
  title: string;
  impact: string;
  description: string;
  suggestion: string | null;
  estimated_speedup: string;
}

interface AIResponse {
  success: boolean;
  suggestions: AISuggestion[];
}

interface HistoryItem {
  sql: string;
  latency_ms: number;
  success: boolean;
  timestamp: string;
}

export default function Home() {
  const [sql, setSql] = useState<string>('SELECT * FROM employees WHERE id = 2');
  const [executing, setExecuting] = useState<boolean>(false);
  const [activeTab, setActiveTab] = useState<'results' | 'plan' | 'ai' | 'stats'>('results');
  
  // Data states
  const [schema, setSchema] = useState<SchemaData>({ tables: [] });
  const [stats, setStats] = useState<StatsData>({ tables: [] });
  const [result, setResult] = useState<QueryResult | null>(null);
  const [aiSuggestions, setAiSuggestions] = useState<AISuggestion[]>([]);
  const [history, setHistory] = useState<HistoryItem[]>([]);

  // Demo queries
  const presets = [
    { label: '1. Create Employees Table', sql: 'CREATE TABLE employees (id INT, name TEXT, age INT)' },
    { label: '2. Create Departments Table', sql: 'CREATE TABLE departments (emp_id INT, dept_name TEXT)' },
    { label: '3. Insert Employee John', sql: "INSERT INTO employees VALUES (1, 'John', 25)" },
    { label: '4. Insert Employee Alice', sql: "INSERT INTO employees VALUES (2, 'Alice', 31)" },
    { label: '5. Insert Employee Bob', sql: "INSERT INTO employees VALUES (3, 'Bob', 28)" },
    { label: '6. Insert Departments', sql: "INSERT INTO departments VALUES (1, 'Engineering');\nINSERT INTO departments VALUES (2, 'HR');\nINSERT INTO departments VALUES (3, 'Sales')" },
    { label: '7. Select All Scan', sql: 'SELECT * FROM employees' },
    { label: '8. Filter Scan (WHERE)', sql: 'SELECT * FROM employees WHERE age > 26' },
    { label: '9. Build B+ Tree Index', sql: 'CREATE INDEX ON employees (id)' },
    { label: '10. Optimized Point Scan', sql: 'SELECT * FROM employees WHERE id = 2' },
    { label: '11. Relational Inner JOIN', sql: 'SELECT name, dept_name FROM employees JOIN departments ON employees.id = departments.emp_id' },
    { label: '12. Sorted Limit Scan', sql: 'SELECT * FROM employees ORDER BY age DESC LIMIT 2' },
    { label: '13. Update Employee Data', sql: 'UPDATE employees SET age = 42 WHERE id = 1' },
    { label: '14. Delete Employee Data', sql: 'DELETE FROM employees WHERE id = 3' }
  ];

  const fetchSchema = async () => {
    try {
      const res = await fetch('/api/schema');
      const data = await res.json();
      if (data.tables) setSchema(data);
    } catch (err) {
      console.error('Failed to load schema', err);
    }
  };

  const fetchStats = async () => {
    try {
      const res = await fetch('/api/stats');
      const data = await res.json();
      if (data.tables) setStats(data);
    } catch (err) {
      console.error('Failed to load stats', err);
    }
  };

  const runQuery = async (queryToRun?: string) => {
    const targetSql = queryToRun || sql;
    if (!targetSql.trim()) return;

    setExecuting(true);
    // Switch to results tab when running query
    setActiveTab('results');
    
    // Split queries by semicolon to run multiple in sequence if provided (e.g. inserts)
    const queries = targetSql
      .split(';')
      .map(q => q.trim())
      .filter(q => q.length > 0);

    let lastResult: QueryResult = { success: false, error: 'No query executed' };
    const timestamp = new Date().toLocaleTimeString();

    try {
      for (const q of queries) {
        const res = await fetch('/api/query', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ query: q })
        });
        lastResult = await res.json();
        
        // Add to history
        setHistory(prev => [
          {
            sql: q,
            latency_ms: lastResult.latency_ms || 0,
            success: lastResult.success,
            timestamp
          },
          ...prev
        ]);
      }

      setResult(lastResult);

      // Trigger AI Optimization suggestions if it is a SELECT query
      if (lastResult.success && targetSql.toLowerCase().trim().startsWith('select')) {
        const aiRes = await fetch('/api/ai-optimize', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ query: targetSql, schema })
        });
        const aiData: AIResponse = await aiRes.json();
        if (aiData.success) {
          setAiSuggestions(aiData.suggestions);
        }
      } else {
        setAiSuggestions([]);
      }

      // Refresh database metadata explorer
      await fetchSchema();
      await fetchStats();

    } catch (err: any) {
      setResult({ success: false, error: err.message || 'Network communication error' });
    } finally {
      setExecuting(false);
    }
  };

  useEffect(() => {
    fetchSchema();
    fetchStats();
  }, []);

  // Recursive plan tree renderer
  const PlanTreeNodeComponent = ({ node }: { node: PlanNode }) => {
    if (!node) return null;
    
    let typeClass = 'projection';
    if (node.type.includes('Scan')) typeClass = 'scan';
    if (node.type.includes('Filter')) typeClass = 'filter';
    if (node.type.includes('Join')) typeClass = 'join';
    if (node.type.includes('Sort')) typeClass = 'sort';
    if (node.type.includes('Limit')) typeClass = 'limit';
    
    const isLeaf = !node.left && !node.right;
    
    return (
      <div className={`tree-node ${isLeaf ? 'leaf' : ''}`}>
        <div className={`tree-node-card ${typeClass}`}>
          <div className="tree-node-type">
            <span>{node.type}</span>
            <span>
              {node.type === 'IndexScan' ? '⚡' : node.type === 'SeqScan' ? '🔍' : '⚙️'}
            </span>
          </div>
          {node.detail && <div className="tree-node-detail">{node.detail}</div>}
          {node.columns && node.columns.length > 0 && (
            <div className="tree-node-cols">
              Output: {node.columns.join(', ')}
            </div>
          )}
        </div>
        
        {(node.left || node.right) && (
          <div className="tree-children">
            {node.left && <PlanTreeNodeComponent node={node.left} />}
            {node.right && <PlanTreeNodeComponent node={node.right} />}
          </div>
        )}
      </div>
    );
  };

  return (
    <div>
      <header className="glass-panel">
        <div className="logo-container">
          <div className="logo-icon">DB</div>
          <div className="logo-text">
            <h1>DBForge Engine</h1>
            <p>Intelligent Custom RDBMS</p>
          </div>
        </div>
        <div className="system-status">
          <div className="status-indicator"></div>
          <span>Active Session Engine</span>
        </div>
      </header>

      <main className="dashboard-container">
        {/* Left column: Schema & History */}
        <div className="sidebar">
          <div className="sidebar-section glass-panel">
            <h2>📁 Schema Explorer</h2>
            <div className="table-schema-list">
              {schema.tables.length === 0 ? (
                <div style={{ fontSize: '13px', color: 'var(--text-secondary)' }}>
                  No tables created yet. Run a <code>CREATE TABLE</code> statement.
                </div>
              ) : (
                schema.tables.map(table => (
                  <div key={table.name} className="table-schema-item">
                    <div className="table-schema-header">
                      <span>{table.name}</span>
                      <span style={{ fontSize: '10px', color: 'var(--accent-teal)' }}>tbl</span>
                    </div>
                    <div className="table-schema-body">
                      {table.columns.map(col => (
                        <div key={col.name} className="col-def-row">
                          <span className="col-name">
                            {col.name}
                            {col.indexed && <span className="col-index-tag">IDX</span>}
                          </span>
                          <span className="col-type">{col.type}</span>
                        </div>
                      ))}
                    </div>
                  </div>
                ))
              )}
            </div>
          </div>

          <div className="sidebar-section glass-panel">
            <h2>🕒 Query History</h2>
            <div className="history-list">
              {history.length === 0 ? (
                <div style={{ fontSize: '13px', color: 'var(--text-secondary)' }}>
                  No queries run in this session.
                </div>
              ) : (
                history.map((item, idx) => (
                  <div 
                    key={idx} 
                    className="history-item" 
                    onClick={() => { setSql(item.sql); runQuery(item.sql); }}
                  >
                    <div className="history-sql" title={item.sql}>{item.sql}</div>
                    <div className="history-meta">
                      <span className={`history-status ${item.success ? 'success' : 'failed'}`}>
                        {item.success ? 'Success' : 'Failed'}
                      </span>
                      <span>{item.latency_ms.toFixed(1)}ms</span>
                    </div>
                  </div>
                ))
              )}
            </div>
          </div>
        </div>

        {/* Right column: Console & Results */}
        <div className="main-content">
          <div className="console-panel glass-panel">
            <div className="console-header">
              <div className="console-title">
                <span>💻 SQL Terminal Console</span>
              </div>
              <div className="console-actions">
                <select 
                  onChange={(e) => {
                    if (e.target.value) setSql(e.target.value);
                  }}
                  className="btn btn-secondary"
                  style={{ background: 'rgba(20, 22, 38, 0.95)', border: '1px solid var(--border-glass)' }}
                >
                  <option value="">-- Load Preset Queries --</option>
                  {presets.map((preset, index) => (
                    <option key={index} value={preset.sql}>{preset.label}</option>
                  ))}
                </select>
                <button 
                  onClick={() => runQuery()} 
                  disabled={executing}
                  className="btn btn-primary"
                >
                  {executing ? 'Executing...' : '⚡ Run Query'}
                </button>
              </div>
            </div>

            <div className="console-input-area">
              <textarea
                value={sql}
                onChange={(e) => setSql(e.target.value)}
                placeholder="Enter SQL statements (e.g. SELECT * FROM employees)..."
                className="console-textarea"
              />
            </div>
          </div>

          <div className="glass-panel" style={{ flexGrow: 1, display: 'flex', flexDirection: 'column' }}>
            <div className="tabs-container">
              <button 
                onClick={() => setActiveTab('results')}
                className={`tab-btn ${activeTab === 'results' ? 'active' : ''}`}
              >
                📊 Query Output
              </button>
              <button 
                onClick={() => setActiveTab('plan')}
                className={`tab-btn ${activeTab === 'plan' ? 'active' : ''}`}
              >
                🌲 Execution Plan Tree
              </button>
              <button 
                onClick={() => setActiveTab('ai')}
                className={`tab-btn ${activeTab === 'ai' ? 'active' : ''}`}
              >
                🤖 AI Advisor
              </button>
              <button 
                onClick={() => setActiveTab('stats')}
                className={`tab-btn ${activeTab === 'stats' ? 'active' : ''}`}
              >
                ⚙️ Storage Metrics
              </button>
            </div>

            <div className="results-panel">
              {/* Query Output Tab */}
              {activeTab === 'results' && (
                <div>
                  {!result ? (
                    <div style={{ textAlign: 'center', padding: '40px 0', color: 'var(--text-secondary)' }}>
                      Enter SQL and execute. Output will appear here.
                    </div>
                  ) : !result.success ? (
                    <div style={{ 
                      padding: '16px', 
                      borderRadius: '8px', 
                      background: 'rgba(244, 63, 94, 0.1)', 
                      border: '1px solid var(--accent-rose)', 
                      color: 'var(--accent-rose)',
                      fontFamily: 'var(--font-mono)',
                      fontSize: '13px'
                    }}>
                      <strong>❌ Error:</strong> {result.error}
                    </div>
                  ) : result.message ? (
                    <div style={{ 
                      padding: '16px', 
                      borderRadius: '8px', 
                      background: 'rgba(16, 185, 129, 0.1)', 
                      border: '1px solid var(--accent-teal)', 
                      color: '#fff',
                      fontSize: '14px'
                    }}>
                      <div style={{ color: 'var(--accent-teal)', fontWeight: 600, marginBottom: '4px' }}>✅ Statement Success</div>
                      <div>{result.message}</div>
                      <div className="query-meta" style={{ marginTop: '12px' }}>
                        <div className="query-meta-item">Latency: <span>{result.latency_ms?.toFixed(3)} ms</span></div>
                      </div>
                    </div>
                  ) : (
                    <div>
                      <div className="results-table-container">
                        <table className="results-table">
                          <thead>
                            <tr>
                              {result.columns?.map(col => (
                                <th key={col}>{col}</th>
                              ))}
                            </tr>
                          </thead>
                          <tbody>
                            {result.records && result.records.length > 0 ? (
                              result.records.map((row, idx) => (
                                <tr key={idx}>
                                  {row.map((val, cellIdx) => (
                                    <td key={cellIdx}>{val}</td>
                                  ))}
                                </tr>
                              ))
                            ) : (
                              <tr>
                                <td colSpan={result.columns?.length || 1} style={{ textAlign: 'center', color: 'var(--text-muted)' }}>
                                  No rows returned.
                                </td>
                              </tr>
                            )}
                          </tbody>
                        </table>
                      </div>
                      
                      <div className="query-meta">
                        <div className="query-meta-item">Rows Fetched: <span>{result.count}</span></div>
                        <div className="query-meta-item">Latency: <span>{result.latency_ms?.toFixed(3)} ms</span></div>
                        <div className="query-meta-item">Storage Read Mode: <span>{result.execution_plan?.left?.type === 'IndexScan' ? 'Index IndexScan (Logarithmic)' : 'Sequential Table FileScan (Linear)'}</span></div>
                      </div>
                    </div>
                  )}
                </div>
              )}

              {/* Execution Plan Tree Tab */}
              {activeTab === 'plan' && (
                <div>
                  {!result || !result.execution_plan ? (
                    <div style={{ textAlign: 'center', padding: '40px 0', color: 'var(--text-secondary)' }}>
                      No physical execution plan available. Run a <code>SELECT</code> query to build the plan tree.
                    </div>
                  ) : (
                    <div>
                      <h4 style={{ marginBottom: '16px', fontSize: '14px', color: 'var(--text-secondary)' }}>
                        Physical Execution Plan Iterator (Volcano Model)
                      </h4>
                      <div className="plan-tree-container">
                        <PlanTreeNodeComponent node={result.execution_plan} />
                      </div>
                    </div>
                  )}
                </div>
              )}

              {/* AI Advisor Tab */}
              {activeTab === 'ai' && (
                <div className="ai-advisor-panel">
                  {aiSuggestions.length === 0 ? (
                    <div style={{ textAlign: 'center', padding: '40px 0', color: 'var(--text-secondary)' }}>
                      No diagnostics run. Run a <code>SELECT</code> query to see optimization recommendations.
                    </div>
                  ) : (
                    aiSuggestions.map((sug, index) => (
                      <div key={index} className="ai-card">
                        <div className={`ai-icon-container ${sug.type}`}>
                          {sug.type === 'warning' ? '⚠️' : sug.type === 'optimization' ? '🚀' : 'ℹ️'}
                        </div>
                        <div className="ai-content">
                          <h3>{sug.title}</h3>
                          <div className="ai-impact-row">
                            <span className={`ai-impact-tag ${sug.type}`}>Impact: {sug.impact}</span>
                            <span className="ai-speedup-tag">Speedup: {sug.estimated_speedup}</span>
                          </div>
                          <p className="ai-description">{sug.description}</p>
                          {sug.suggestion && (
                            <div className="ai-code-block">
                              <span>{sug.suggestion}</span>
                              <button onClick={() => {
                                setSql(sug.suggestion || '');
                                runQuery(sug.suggestion || '');
                              }}>
                                Run Suggestion
                              </button>
                            </div>
                          )}
                        </div>
                      </div>
                    ))
                  )}
                </div>
              )}

              {/* Storage Stats Tab */}
              {activeTab === 'stats' && (
                <div>
                  {stats.tables.length === 0 ? (
                    <div style={{ textAlign: 'center', padding: '40px 0', color: 'var(--text-secondary)' }}>
                      No stats available. Create tables and insert records.
                    </div>
                  ) : (
                    <div>
                      <div className="stats-grid">
                        {stats.tables.map(table => (
                          <div key={table.name} className="stats-card glass-panel" style={{ background: 'rgba(255, 255, 255, 0.01)' }}>
                            <h3 style={{ fontSize: '16px', color: 'var(--accent-purple)' }}>{table.name}.tbl</h3>
                            <div className="stats-card-val">{table.row_count}</div>
                            <div className="stats-card-label">Active Rows</div>
                            <div style={{ borderTop: '1px solid rgba(255, 255, 255, 0.05)', marginTop: '12px', paddingTop: '12px', fontSize: '12px', color: 'var(--text-secondary)' }}>
                              <div>File Size: <strong style={{ color: '#fff' }}>{table.file_size_bytes} Bytes</strong></div>
                              <div>Allocated Pages: <strong style={{ color: '#fff' }}>{table.page_count} Pages</strong></div>
                            </div>
                          </div>
                        ))}
                      </div>
                      
                      <div style={{ marginTop: '24px', padding: '16px', background: 'rgba(139, 92, 246, 0.05)', border: '1px solid rgba(139, 92, 246, 0.1)', borderRadius: '8px', fontSize: '13px', lineHeight: '1.6' }}>
                        <h4 style={{ color: 'var(--accent-purple)', fontWeight: 600, marginBottom: '6px' }}>Database Disk Layout Architecture</h4>
                        <p>
                          DBForge implements a physical <strong>Slotted-Page Storage Engine</strong> where tables are written directly as binary <code>.tbl</code> files. 
                          Each page has a fixed budget of <strong>4096 Bytes</strong>. Records grow from the bottom of the page upwards, while the slot offsets array grows from the top downwards. 
                          This prevents page fragmentation and handles variable-length columns (like <code>TEXT</code>) in place. Indices are structured as balanced <strong>B+ Trees</strong> and written as binary <code>.idx</code> maps.
                        </p>
                      </div>
                    </div>
                  )}
                </div>
              )}
            </div>
          </div>
        </div>
      </main>
    </div>
  );
}
