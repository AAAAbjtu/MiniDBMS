import { useState, useCallback, useEffect, useRef, type MouseEvent as ReactMouseEvent } from 'react'
import { api } from '../lib/api'
import { Plus, Trash2, Edit3, Check, X, Loader2, AlertCircle, RefreshCw, Table2, Save } from 'lucide-react'

interface Props {
  token: string
  isDark: boolean
  currentDb: string
  initialDb?: string
  initialTable?: string
  onExecute: (sql: string) => void
}

interface ColumnInfo {
  name: string
  type: string
  constraints: string
}

interface ContextMenu {
  x: number
  y: number
  colIndex: number
}

const TYPES = ['INT', 'TEXT', 'FLOAT', 'DOUBLE', 'BOOL', 'DATE', 'STRING']

function parseDescribe(output: string): ColumnInfo[] {
  const lines = output.split('\n').map((l) => l.trim()).filter((l) => l)
  const cols: ColumnInfo[] = []
  let first = true
  for (const line of lines) {
    if (first) { first = false; continue }
    if (/^\(\d+/.test(line)) continue
    const parts = line.split('\t')
    if (parts.length >= 2 && parts[0]) {
      cols.push({ name: parts[0], type: parts[1] || 'UNKNOWN', constraints: parts[2] || '' })
    }
  }
  return cols
}

function parseSelectRows(output: string): { headers: string[]; rows: string[][] } {
  const lines = output.split('\n')
  const tableLines: string[] = []
  for (const line of lines) {
    if (line.includes('|')) {
      if (line.match(/^[+-]+$/)) continue
      tableLines.push(line)
    }
  }
  const headers: string[] = []
  const rows: string[][] = []

  for (let i = 0; i < tableLines.length; i++) {
    const cells = tableLines[i].split('|').map((c) => c.trim()).filter((c) => c !== '')
    if (cells.length === 0) continue
    // Skip summary lines like "(1 rows)"
    if (cells.length === 1 && /^\(\d+/.test(cells[0])) continue
    if (headers.length === 0) {
      // Strip table prefix from headers (e.g., "student.id" → "id")
      headers.push(...cells.map((h) => { const dot = h.lastIndexOf('.'); return dot >= 0 ? h.slice(dot + 1) : h }))
    } else {
      if (cells.length === headers.length) rows.push(cells)
    }
  }
  return { headers, rows }
}

function parseTableList(output: string): string[] {
  const lines = output.split('\n').map((l) => l.trim()).filter((l) => l)
  const items: string[] = []
  let first = true
  for (const line of lines) {
    if (first) { first = false; continue }
    if (/^\(\d+/.test(line)) continue
    items.push(line)
  }
  return items
}

export default function TableWorkspace({ token, isDark, currentDb, initialDb, initialTable, onExecute }: Props) {
  const [dbName, setDbName] = useState(initialDb || currentDb || 'default')
  const [tableName, setTableName] = useState(initialTable || '')
  const [tables, setTables] = useState<string[]>([])
  const [columns, setColumns] = useState<ColumnInfo[]>([])
  const [rows, setRows] = useState<string[][]>([])
  const [headers, setHeaders] = useState<string[]>([])
  const [loading, setLoading] = useState(false)
  const [error, setError] = useState('')

  // Inline editing
  const [editCell, setEditCell] = useState<{ row: number; col: number } | null>(null)
  const [editValue, setEditValue] = useState('')

  // Add column
  const [showAddCol, setShowAddCol] = useState(false)
  const [newColName, setNewColName] = useState('')
  const [newColType, setNewColType] = useState('INT')

  // Modify column
  const [modifyCol, setModifyCol] = useState<{ index: number; name: string; type: string } | null>(null)

  // Add row
  const [addingRow, setAddingRow] = useState(false)
  const [newRow, setNewRow] = useState<string[]>([])

  // Context menu
  const [ctxMenu, setCtxMenu] = useState<ContextMenu | null>(null)
  const ctxRef = useRef<HTMLDivElement>(null)

  // Close context menu on outside click
  useEffect(() => {
    const handler = () => setCtxMenu(null)
    if (ctxMenu) document.addEventListener('click', handler)
    return () => document.removeEventListener('click', handler)
  }, [ctxMenu])

  // Fetch tables
  const fetchTables = useCallback(async () => {
    try {
      const { data } = await api.post('/execute', { token, sql: `SHOW TABLES IN ${dbName};` })
      if (data.success) setTables(parseTableList(data.message))
    } catch { /* ignore */ }
  }, [token, dbName])

  // Fetch data
  const fetchData = useCallback(async (tbl: string) => {
    if (!tbl) return
    setLoading(true)
    setError('')
    try {
      const [descRes, selRes] = await Promise.all([
        api.post('/execute', { token, sql: `DESCRIBE ${tbl};` }),
        api.post('/execute', { token, sql: `SELECT * FROM ${tbl};` }),
      ])
      if (descRes.data.success) {
        setColumns(parseDescribe(descRes.data.message))
      } else {
        setError(descRes.data.message)
        setColumns([])
      }
      if (selRes.data.success) {
        const parsed = parseSelectRows(selRes.data.message)
        setHeaders(parsed.headers)
        setRows(parsed.rows)
      } else {
        setHeaders([])
        setRows([])
      }
    } catch {
      setError('Failed to fetch table data')
    } finally {
      setLoading(false)
    }
  }, [token])

  useEffect(() => { fetchTables() }, [fetchTables])

  useEffect(() => {
    if (tableName) {
      fetchData(tableName)
      setShowAddCol(false)
      setModifyCol(null)
      setAddingRow(false)
    } else {
      setColumns([]); setRows([]); setHeaders([])
    }
  }, [tableName, fetchData])

  // Column context menu
  const handleCtxMenu = (e: ReactMouseEvent, colIdx: number) => {
    e.preventDefault()
    setCtxMenu({ x: e.clientX, y: e.clientY, colIndex: colIdx })
  }

  // Cell editing
  const startEdit = (row: number, col: number) => {
    if (rows[row]) {
      setEditCell({ row, col })
      setEditValue(rows[row][col] || '')
    }
  }
  const saveEdit = () => {
    if (!editCell || !tableName || !headers.length) return
    const { row, col } = editCell
    const srcRow = rows[row]
    if (!srcRow) return
    const colName = headers[col]
    const oldVal = srcRow[col] || ''
    const newVal = editValue
    if (oldVal === newVal) { setEditCell(null); return }

    const pkIdx = headers.findIndex((h) => h.toLowerCase() === 'id')
    let whereClause: string
    if (pkIdx >= 0) {
      whereClause = `${headers[pkIdx]} = '${(srcRow[pkIdx] || '').replace(/'/g, "''")}'`
    } else {
      whereClause = headers.map((h, i) => `${h} = '${(srcRow[i] || '').replace(/'/g, "''")}'`).join(' AND ')
    }
    onExecute(`UPDATE ${tableName} SET ${colName} = '${newVal.replace(/'/g, "''")}' WHERE ${whereClause};`)
    setEditCell(null)
    setTimeout(() => fetchData(tableName), 200)
  }
  const cancelEdit = () => setEditCell(null)

  // Add row
  const startAddRow = () => {
    setNewRow(headers.map(() => ''))
    setAddingRow(true)
  }
  const saveNewRow = () => {
    if (!tableName) return
    const vals = newRow.map((v) => `'${v.replace(/'/g, "''")}'`).join(', ')
    onExecute(`INSERT INTO ${tableName} VALUES (${vals});`)
    setAddingRow(false)
    setTimeout(() => fetchData(tableName), 200)
  }
  const cancelAddRow = () => setAddingRow(false)

  // Delete row
  const deleteRow = (rowIdx: number) => {
    if (!tableName || !headers.length || !rows[rowIdx]) return
    const row = rows[rowIdx]
    // Prefer 'id' column as primary key; fall back to matching all columns
    const pkIdx = headers.findIndex((h) => h.toLowerCase() === 'id')
    let whereClause: string
    if (pkIdx >= 0) {
      whereClause = `${headers[pkIdx]} = '${(row[pkIdx] || '').replace(/'/g, "''")}'`
    } else {
      whereClause = headers.map((h, i) => `${h} = '${(row[i] || '').replace(/'/g, "''")}'`).join(' AND ')
    }
    onExecute(`DELETE FROM ${tableName} WHERE ${whereClause};`)
    setTimeout(() => fetchData(tableName), 200)
  }

  // Add column
  const addColumn = () => {
    if (!tableName || !newColName.trim()) return
    onExecute(`ALTER TABLE ${tableName} ADD ${newColName.trim()} ${newColType};`)
    setShowAddCol(false)
    setNewColName('')
    setNewColType('INT')
    setTimeout(() => fetchData(tableName), 300)
  }

  // Drop column
  const dropColumn = (colName: string) => {
    if (!tableName) return
    onExecute(`ALTER TABLE ${tableName} DROP COLUMN ${colName};`)
    setCtxMenu(null)
    setTimeout(() => fetchData(tableName), 300)
  }

  // Modify column
  const saveModifyCol = () => {
    if (!tableName || !modifyCol) return
    onExecute(`ALTER TABLE ${tableName} MODIFY COLUMN ${modifyCol.name} ${modifyCol.type};`)
    setModifyCol(null)
    setTimeout(() => fetchData(tableName), 300)
  }

  const cardBg = isDark ? 'bg-slate-900' : 'bg-white'
  const border = isDark ? 'border-white/10' : 'border-slate-200'
  const inputClass = `px-2 py-1 text-xs rounded border focus:outline-none focus:ring-1 ${
    isDark ? 'bg-slate-800 border-white/10 focus:ring-indigo-500 text-white placeholder-slate-600' : 'bg-white border-slate-200 focus:ring-indigo-400 text-slate-900 placeholder-slate-300'
  }`
  const muted = isDark ? 'text-slate-500' : 'text-slate-400'

  return (
    <div className="flex-1 flex flex-col overflow-hidden">
      {/* Top bar: DB + Table selector + Refresh */}
      <div className={`flex items-center gap-3 px-4 py-2 border-b shrink-0 ${border}`}>
        <Table2 className={`w-4 h-4 ${isDark ? 'text-indigo-400' : 'text-indigo-600'}`} />
        <input
          type="text" value={dbName}
          onChange={(e) => { setDbName(e.target.value); setTableName('') }}
          className={`w-28 ${inputClass}`} placeholder="database"
        />
        <span className={muted}>/</span>
        <select
          value={tableName}
          onChange={(e) => setTableName(e.target.value)}
          className={`flex-1 max-w-xs ${inputClass}`}
        >
          <option value="">-- Select a table --</option>
          {tables.map((t) => <option key={t} value={t}>{t}</option>)}
        </select>
        {tableName && (
          <button onClick={() => fetchData(tableName)} className={`p-1.5 rounded-lg ${isDark ? 'hover:bg-white/10' : 'hover:bg-slate-100'}`} title="Refresh">
            <RefreshCw className={`w-3.5 h-3.5 ${loading ? 'animate-spin' : muted}`} />
          </button>
        )}
      </div>

      {/* Loading */}
      {loading && (
        <div className="flex items-center justify-center py-12">
          <Loader2 className={`w-5 h-5 animate-spin ${muted}`} />
        </div>
      )}

      {/* Error */}
      {error && !loading && (
        <div className={`flex items-center gap-2 m-4 px-4 py-3 rounded-xl border text-xs ${
          isDark ? 'bg-red-500/10 border-red-500/20 text-red-400' : 'bg-red-50 border-red-200 text-red-600'
        }`}>
          <AlertCircle className="w-3.5 h-3.5 shrink-0" />
          {error}
          <button onClick={() => setError('')} className="ml-auto"><X className="w-3 h-3" /></button>
        </div>
      )}

      {/* Table content */}
      {!loading && tableName && (
        <div className="flex-1 flex flex-col overflow-hidden">
          {/* Column management toolbar */}
          <div className={`flex items-center gap-2 px-4 py-1.5 border-b shrink-0 ${border} ${isDark ? 'bg-slate-900/80' : 'bg-slate-50'}`}>
            <span className={`text-[10px] font-semibold uppercase tracking-wider ${muted}`}>
              {columns.length} columns
            </span>
            <div className="flex-1" />
            {!showAddCol && (
              <button
                onClick={() => setShowAddCol(true)}
                className="flex items-center gap-1 px-2.5 py-1 text-xs font-medium rounded-lg bg-indigo-500 hover:bg-indigo-400 text-white transition-colors shadow-sm"
              >
                <Plus className="w-3 h-3" /> Add Column
              </button>
            )}
          </div>

          {/* Add row action bar */}
          {addingRow && (
            <div className={`flex items-center gap-2 px-4 py-1.5 border-b shrink-0 ${border} ${isDark ? 'bg-indigo-500/10' : 'bg-indigo-50'}`}>
              <span className={`text-xs ${muted}`}>New row</span>
              <div className="flex-1" />
              <button onClick={saveNewRow} className="flex items-center gap-1 px-3 py-1 text-xs font-medium rounded-lg bg-emerald-500 hover:bg-emerald-400 text-white transition-colors shadow-sm">
                <Save className="w-3 h-3" /> Save
              </button>
              <button onClick={cancelAddRow} className="flex items-center gap-1 px-3 py-1 text-xs font-medium rounded-lg hover:bg-white/10 transition-colors">
                <X className="w-3 h-3" /> Cancel
              </button>
            </div>
          )}

          {/* Table */}
          <div className="flex-1 overflow-auto">
            <table className="w-full border-collapse table-fixed" style={{ borderCollapse: 'collapse' }}>
              <thead className={`sticky top-0 z-10 ${isDark ? 'bg-slate-800/50' : 'bg-slate-100'}`}>
                <tr>
                  <th className={`w-10 px-2 py-2 border-r border-b text-left text-[10px] font-medium ${muted} ${border}`}></th>
                  {columns.map((col, idx) => (
                    <th
                      key={`${col.name}-${idx}`}
                      className={`group relative px-3 py-2 border-r border-b ${border} transition-colors cursor-context-menu select-none ${isDark ? 'hover:bg-slate-700' : 'hover:bg-slate-200'}`}
                      onContextMenu={(e) => handleCtxMenu(e, idx)}
                    >
                      {modifyCol?.index === idx ? (
                        <div className="flex flex-col gap-1">
                          <input type="text" value={modifyCol.name} onChange={(e) => setModifyCol({ ...modifyCol, name: e.target.value })} className={inputClass} autoFocus onKeyDown={(e) => { if (e.key === 'Enter') saveModifyCol(); if (e.key === 'Escape') setModifyCol(null) }} />
                          <select value={modifyCol.type} onChange={(e) => setModifyCol({ ...modifyCol, type: e.target.value })} className={inputClass}>
                            {TYPES.map((t) => <option key={t} value={t}>{t}</option>)}
                          </select>
                          <div className="flex gap-1">
                            <button onClick={saveModifyCol} className="p-0.5 rounded text-emerald-400 hover:bg-emerald-500/10"><Check className="w-3 h-3" /></button>
                            <button onClick={() => setModifyCol(null)} className="p-0.5 rounded text-red-400 hover:bg-red-500/10"><X className="w-3 h-3" /></button>
                          </div>
                        </div>
                      ) : (
                        <>
                          <div className={`text-xs font-semibold ${isDark ? 'text-slate-200' : 'text-slate-700'}`}>{col.name}</div>
                          <div className={`text-[10px] ${muted}`}>{col.type}</div>
                          {col.constraints && <div className={`text-[10px] ${isDark ? 'text-amber-400' : 'text-amber-600'}`}>{col.constraints}</div>}
                          <button
                            onClick={() => setModifyCol({ index: idx, name: col.name, type: col.type })}
                            className={`absolute top-1 right-1 p-0.5 rounded opacity-0 group-hover:opacity-100 transition-opacity ${isDark ? 'hover:bg-white/10 text-slate-600 hover:text-slate-400' : 'hover:bg-slate-100 text-slate-300 hover:text-slate-500'}`}
                            title="Edit column"
                          >
                            <Edit3 className="w-3 h-3" />
                          </button>
                        </>
                      )}
                    </th>
                  ))}
                  {showAddCol && (
                    <th className={`px-2 py-2 border-r border-b min-w-[180px] ${border} ${isDark ? 'bg-indigo-500/5' : 'bg-indigo-50'}`}>
                      <div className="flex items-start gap-1">
                        <div className="flex flex-col gap-1 flex-1">
                          <input type="text" value={newColName} onChange={(e) => setNewColName(e.target.value)} placeholder="name" className={inputClass} autoFocus onKeyDown={(e) => { if (e.key === 'Enter') addColumn(); if (e.key === 'Escape') setShowAddCol(false) }} />
                          <select value={newColType} onChange={(e) => setNewColType(e.target.value)} className={inputClass}>
                            {TYPES.map((t) => <option key={t} value={t}>{t}</option>)}
                          </select>
                        </div>
                        <button onClick={addColumn} className="p-0.5 rounded text-emerald-400 hover:bg-emerald-500/10"><Check className="w-3 h-3" /></button>
                        <button onClick={() => setShowAddCol(false)} className="p-0.5 rounded text-red-400 hover:bg-red-500/10"><X className="w-3 h-3" /></button>
                      </div>
                    </th>
                  )}
                  <th className="w-8 border-r border-b border-transparent" />
                </tr>
              </thead>
              <tbody>
                {rows.length === 0 && !addingRow ? (
                  <tr>
                    <td colSpan={columns.length + (showAddCol ? 3 : 2)} className={`text-center py-16 text-xs ${muted}`}>No data rows</td>
                  </tr>
                ) : (
                  rows.map((row, rowIdx) => (
                    <tr key={rowIdx} className={`transition-colors group ${isDark ? 'hover:bg-white/[0.03]' : 'hover:bg-slate-50'}`}>
                      <td className={`w-10 px-2 py-1.5 text-right text-[10px] ${muted} border-r border-b ${border}`}>{rowIdx + 1}</td>
                      {row.map((cell, colIdx) => {
                        const isEdit = editCell?.row === rowIdx && editCell?.col === colIdx
                        return (
                          <td key={colIdx} className={`px-3 py-1.5 border-r border-b ${border} text-xs ${isDark ? 'text-slate-300' : 'text-slate-700'}`} onDoubleClick={() => startEdit(rowIdx, colIdx)}>
                            {isEdit ? (
                              <div className="flex items-center gap-1">
                                <input type="text" value={editValue} onChange={(e) => setEditValue(e.target.value)} onKeyDown={(e) => { if (e.key === 'Enter') saveEdit(); if (e.key === 'Escape') cancelEdit() }} className={`flex-1 ${inputClass}`} autoFocus />
                                <button onClick={saveEdit} className="p-0.5 rounded text-emerald-400 hover:bg-emerald-500/10"><Check className="w-3 h-3" /></button>
                                <button onClick={cancelEdit} className="p-0.5 rounded text-red-400 hover:bg-red-500/10"><X className="w-3 h-3" /></button>
                              </div>
                            ) : (
                              <span className="cursor-pointer block truncate">{cell || <span className={muted}>NULL</span>}</span>
                            )}
                          </td>
                        )
                      })}
                      <td className="w-8 px-1 py-1.5 border-b border-transparent">
                        <button onClick={() => deleteRow(rowIdx)} className={`p-0.5 rounded opacity-0 group-hover:opacity-100 transition-opacity ${isDark ? 'hover:bg-red-500/10 text-slate-600 hover:text-red-400' : 'hover:bg-red-50 text-slate-300 hover:text-red-500'}`} title="Delete row"><Trash2 className="w-3 h-3" /></button>
                      </td>
                    </tr>
                  ))
                )}
                {addingRow && (
                  <tr className={`${isDark ? 'bg-indigo-500/5' : 'bg-indigo-50'}`}>
                    <td className={`w-10 px-2 py-1.5 text-right text-[10px] ${muted} border-r border-b ${border}`}>+</td>
                    {headers.map((_, colIdx) => (
                      <td key={colIdx} className={`px-3 py-1.5 border-r border-b ${border}`}>
                        <input type="text" value={newRow[colIdx] ?? ''} onChange={(e) => setNewRow((prev) => { const n = [...prev]; n[colIdx] = e.target.value; return n })} placeholder="value" className={`w-full ${inputClass}`} autoFocus={colIdx === 0} onKeyDown={(e) => { if (e.key === 'Enter') saveNewRow(); if (e.key === 'Escape') cancelAddRow() }} />
                      </td>
                    ))}
                    <td className="w-8 px-1 py-1.5 border-b border-transparent"></td>
                  </tr>
                )}
              </tbody>
            </table>

            {/* Add row button */}
            {!addingRow && tableName && (
              <button
                onClick={startAddRow}
                className={`w-full flex items-center justify-center gap-1.5 py-2 text-xs transition-colors ${isDark ? 'text-slate-500 hover:text-white hover:bg-white/5' : 'text-slate-400 hover:text-slate-700 hover:bg-slate-50'}`}
              >
                <Plus className="w-3 h-3" /> Add Row
              </button>
            )}
          </div>
        </div>
      )}

      {/* Empty state */}
      {!loading && !tableName && (
        <div className="flex-1 flex flex-col items-center justify-center">
          <Table2 className={`w-12 h-12 mb-3 opacity-20 ${muted}`} />
          <p className={`text-sm ${muted}`}>Select a table to manage its schema and data</p>
          <p className={`text-xs mt-1 ${muted}`}>Click a table in the Explorer or select one above</p>
        </div>
      )}

      {/* Context menu */}
      {ctxMenu && (
        <div
          ref={ctxRef}
          className={`fixed z-50 rounded-xl border shadow-2xl py-1 min-w-[160px] ${cardBg} ${border}`}
          style={{ left: ctxMenu.x, top: ctxMenu.y }}
        >
          <button
            onClick={() => {
              const col = columns[ctxMenu.colIndex]
              if (col) setModifyCol({ index: ctxMenu.colIndex, name: col.name, type: col.type })
              setCtxMenu(null)
            }}
            className={`w-full flex items-center gap-2 px-3 py-1.5 text-xs transition-colors ${
              isDark ? 'hover:bg-white/5 text-slate-400' : 'hover:bg-slate-100 text-slate-600'
            }`}
          >
            <Edit3 className="w-3 h-3" /> Modify Column
          </button>
          <button
            onClick={() => {
              const col = columns[ctxMenu.colIndex]
              if (col) dropColumn(col.name)
            }}
            className={`w-full flex items-center gap-2 px-3 py-1.5 text-xs transition-colors ${
              isDark ? 'hover:bg-red-500/10 text-red-400' : 'hover:bg-red-50 text-red-500'
            }`}
          >
            <Trash2 className="w-3 h-3" /> Delete Column
          </button>
        </div>
      )}
    </div>
  )
}
