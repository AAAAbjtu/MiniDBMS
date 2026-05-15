import { useState, useMemo, useCallback } from 'react'
import { Check, X, Trash2, AlertCircle, Plus, Save } from 'lucide-react'

interface Props {
  result: {
    success: boolean
    type: string
    message: string
  } | null
  token: string
  isDark: boolean
  tableName: string | null
  onExecute: (sql: string) => void
}

interface ParsedTable {
  headers: string[]
  rows: string[][]
}

function parseTableOutput(output: string): ParsedTable | null {
  const lines = output.split('\n')
  const tableLines: string[] = []
  for (const line of lines) {
    if (line.includes('|')) {
      if (line.match(/^[+-]+$/)) continue
      tableLines.push(line)
    }
  }
  if (tableLines.length === 0) return null

  const headers: string[] = []
  const allRows: string[][] = []

  for (let i = 0; i < tableLines.length; i++) {
    const cells = tableLines[i]
      .split('|')
      .map((c) => c.trim())
      .filter((_, idx, arr) => idx > 0 && idx < arr.length - 1)
    const altCells = tableLines[i]
      .split('|')
      .map((c) => c.trim())
      .filter((c) => c !== '')
    const finalCells = cells.length >= altCells.length ? cells : altCells
    if (finalCells.length === 0) continue

    if (headers.length === 0) {
      // Strip table prefix from headers (e.g., "student.id" → "id")
      headers.push(...finalCells.map((h) => { const dot = h.lastIndexOf('.'); return dot >= 0 ? h.slice(dot + 1) : h }))
    } else {
      const isSep = finalCells.every((c) => /^[-=]+$/.test(c))
      if (!isSep && finalCells.length === headers.length) {
        allRows.push(finalCells)
      } else if (!isSep && finalCells.length > 0) {
        const row = finalCells.slice(0, headers.length)
        while (row.length < headers.length) row.push('')
        allRows.push(row)
      }
    }
  }
  if (headers.length === 0) return null
  return { headers, rows: allRows }
}

export default function DataGrid({ result, token: _token, isDark, tableName, onExecute }: Props) {
  const [editingCell, setEditingCell] = useState<{ row: number; col: number } | null>(null)
  const [editValue, setEditValue] = useState('')
  const [statusMsg, setStatusMsg] = useState('')
  const [addingRow, setAddingRow] = useState(false)
  const [newRowValues, setNewRowValues] = useState<string[]>([])

  const table = useMemo(() => (result?.message ? parseTableOutput(result.message) : null), [result])
  const canEdit = !!tableName

  const handleDoubleClick = useCallback(
    (rowIdx: number, colIdx: number) => {
      if (!table || !canEdit) return
      setEditingCell({ row: rowIdx, col: colIdx })
      setEditValue(table.rows[rowIdx][colIdx])
      setStatusMsg('')
    },
    [table, canEdit]
  )

  const handleSaveCell = useCallback(() => {
    if (!editingCell || !table || !tableName || !table.headers.length) return
    const { row, col } = editingCell
    const colName = table.headers[col]
    const oldVal = table.rows[row]?.[col] || ''
    const newVal = editValue
    if (oldVal === newVal) { setEditingCell(null); return }

    const pkIdx = table.headers.findIndex((h) => h.toLowerCase() === 'id' || h.toLowerCase().endsWith('_id'))
    let whereClause: string
    if (pkIdx >= 0) {
      whereClause = `${table.headers[pkIdx]} = '${(table.rows[row][pkIdx] || '').replace(/'/g, "''")}'`
    } else {
      whereClause = table.headers.map((h, i) => `${h} = '${(table.rows[row][i] || '').replace(/'/g, "''")}'`).join(' AND ')
    }
    onExecute(`UPDATE ${tableName} SET ${colName} = '${newVal.replace(/'/g, "''")}' WHERE ${whereClause};`)
    setEditingCell(null)
  }, [editingCell, editValue, table, tableName, onExecute])

  const handleCancelEdit = () => { setEditingCell(null); setStatusMsg('') }

  const handleDeleteRow = useCallback(
    (rowIdx: number) => {
      if (!table || !tableName || !table.headers.length || !table.rows[rowIdx]) return
      const row = table.rows[rowIdx]
      const pkIdx = table.headers.findIndex((h) => h.toLowerCase() === 'id' || h.toLowerCase().endsWith('_id'))
      let whereClause: string
      if (pkIdx >= 0) {
        whereClause = `${table.headers[pkIdx]} = '${(row[pkIdx] || '').replace(/'/g, "''")}'`
      } else {
        whereClause = table.headers.map((h, i) => `${h} = '${(row[i] || '').replace(/'/g, "''")}'`).join(' AND ')
      }
      onExecute(`DELETE FROM ${tableName} WHERE ${whereClause};`)
    },
    [table, tableName, onExecute]
  )

  const startAddRow = () => {
    if (!table) return
    setNewRowValues(table.headers.map(() => ''))
    setAddingRow(true)
    setStatusMsg('')
  }

  const cancelAddRow = () => {
    setAddingRow(false)
    setNewRowValues([])
  }

  const saveNewRow = () => {
    if (!table || !tableName) return
    const vals = newRowValues.map((v) => `'${v.replace(/'/g, "''")}'`).join(', ')
    onExecute(`INSERT INTO ${tableName} VALUES (${vals});`)
    setAddingRow(false)
    setNewRowValues([])
  }

  const border = isDark ? 'border-white/10' : 'border-slate-200'
  const headerBg = isDark ? 'bg-slate-800' : 'bg-slate-100'
  const rowHover = isDark ? 'hover:bg-white/5' : 'hover:bg-slate-50'
  const muted = isDark ? 'text-slate-500' : 'text-slate-400'
  const cellBorder = isDark ? 'border-white/5' : 'border-slate-200'

  if (!result) {
    return (
      <div className={`h-52 shrink-0 flex items-center justify-center ${isDark ? 'text-slate-600' : 'text-slate-400'}`}>
        <p className="text-sm">Execute a query to see results</p>
      </div>
    )
  }

  return (
    <div className="h-52 shrink-0 flex flex-col overflow-hidden">
      {/* Status bar */}
      <div className={`flex items-center gap-2 px-3 py-1.5 border-b text-xs ${border} ${
        result.success
          ? isDark ? 'bg-emerald-500/10 text-emerald-400' : 'bg-emerald-50 text-emerald-700'
          : isDark ? 'bg-red-500/10 text-red-400' : 'bg-red-50 text-red-700'
      }`}>
        <div className={`w-1.5 h-1.5 rounded-full ${result.success ? 'bg-emerald-400' : 'bg-red-400'}`} />
        <span className="font-medium">{result.type.toUpperCase()}</span>
        {tableName && (
          <span className={`ml-2 px-1.5 py-0.5 rounded text-[10px] ${isDark ? 'bg-indigo-500/20 text-indigo-300' : 'bg-indigo-100 text-indigo-700'}`}>
            {tableName}
          </span>
        )}
        {!canEdit && table && (
          <span className={`ml-2 text-[10px] ${muted}`}>
            (inline editing disabled — run SELECT * FROM table to enable)
          </span>
        )}
        {!result.success && (
          <span className={`truncate ${muted}`}>— {result.message}</span>
        )}
      </div>

      {statusMsg && (
        <div className={`flex items-center gap-2 px-3 py-2 text-xs border-b ${border} ${
          isDark ? 'bg-yellow-500/10 text-yellow-400' : 'bg-yellow-50 text-yellow-700'
        }`}>
          <AlertCircle className="w-3.5 h-3.5 shrink-0" />
          {statusMsg}
          <button onClick={() => setStatusMsg('')} className="ml-auto"><X className="w-3 h-3" /></button>
        </div>
      )}

      <div className="flex-1 overflow-auto">
        {table ? (
          <>
            <table className="w-full text-sm border-collapse">
              <thead className={`sticky top-0 z-10 ${headerBg}`}>
                <tr>
                  <th className={`w-10 px-2 py-2 text-left text-xs font-medium ${muted} border-b ${cellBorder}`}>#</th>
                  {table.headers.map((h) => (
                    <th key={h} className={`px-3 py-2 text-left text-xs font-semibold uppercase tracking-wider border-b ${cellBorder}`}>
                      {h}
                    </th>
                  ))}
                  <th className={`w-16 px-2 py-2 border-b ${cellBorder}`} />
                </tr>
              </thead>
              <tbody>
                {table.rows.length === 0 && !addingRow ? (
                  <tr>
                    <td colSpan={table.headers.length + 2} className={`text-center py-8 text-xs ${muted}`}>
                      No rows returned
                    </td>
                  </tr>
                ) : (
                  table.rows.map((row, rowIdx) => (
                    <tr key={rowIdx} className={`transition-colors ${rowHover}`}>
                      <td className={`px-2 py-1.5 text-xs ${muted} border-b ${cellBorder}`}>{rowIdx + 1}</td>
                      {row.map((cell, colIdx) => {
                        const isEditing = editingCell?.row === rowIdx && editingCell?.col === colIdx
                        return (
                          <td
                            key={colIdx}
                            className={`px-3 py-1.5 border-b ${cellBorder} ${isDark ? 'text-slate-200' : 'text-slate-700'}`}
                            onDoubleClick={() => handleDoubleClick(rowIdx, colIdx)}
                          >
                            {isEditing ? (
                              <div className="flex items-center gap-1">
                                <input
                                  type="text"
                                  value={editValue}
                                  onChange={(e) => setEditValue(e.target.value)}
                                  onKeyDown={(e) => { if (e.key === 'Enter') handleSaveCell(); if (e.key === 'Escape') handleCancelEdit() }}
                                  className={`w-full px-1.5 py-0.5 text-xs rounded border focus:outline-none focus:ring-1 ${
                                    isDark ? 'bg-slate-800 border-indigo-500/50 focus:ring-indigo-500 text-white' : 'bg-white border-indigo-400 focus:ring-indigo-400 text-slate-900'
                                  }`}
                                  autoFocus
                                />
                                <button onClick={handleSaveCell} className="p-0.5 rounded text-emerald-400 hover:bg-emerald-500/10"><Check className="w-3 h-3" /></button>
                                <button onClick={handleCancelEdit} className="p-0.5 rounded text-red-400 hover:bg-red-500/10"><X className="w-3 h-3" /></button>
                              </div>
                            ) : (
                              <span className={`${canEdit ? 'cursor-pointer hover:bg-white/5 rounded px-1 -mx-1' : 'cursor-default'} select-text`}>
                                {cell || <span className={muted}>NULL</span>}
                              </span>
                            )}
                          </td>
                        )
                      })}
                      <td className={`px-2 py-1.5 border-b ${cellBorder}`}>
                        {canEdit && (
                          <button
                            onClick={() => handleDeleteRow(rowIdx)}
                            className={`p-1 rounded transition-colors ${isDark ? 'hover:bg-red-500/10 text-slate-600 hover:text-red-400' : 'hover:bg-red-50 text-slate-300 hover:text-red-500'}`}
                            title="Delete row"
                          >
                            <Trash2 className="w-3.5 h-3.5" />
                          </button>
                        )}
                      </td>
                    </tr>
                  ))
                )}

                {/* Add new row */}
                {addingRow && (
                  <tr className={isDark ? 'bg-indigo-500/5' : 'bg-indigo-50'}>
                    <td className={`px-2 py-1.5 text-xs ${muted} border-b ${cellBorder}`}>+</td>
                    {table.headers.map((h, colIdx) => (
                      <td key={h} className={`px-3 py-1.5 border-b ${cellBorder}`}>
                        <input
                          type="text"
                          value={newRowValues[colIdx] ?? ''}
                          onChange={(e) => setNewRowValues((prev) => { const n = [...prev]; n[colIdx] = e.target.value; return n })}
                          placeholder="value"
                          onKeyDown={(e) => { if (e.key === 'Enter') saveNewRow(); if (e.key === 'Escape') cancelAddRow() }}
                          className={`w-full px-1.5 py-0.5 text-xs rounded border focus:outline-none focus:ring-1 ${
                            isDark ? 'bg-slate-800 border-indigo-500/50 focus:ring-indigo-500 text-white placeholder-slate-600' : 'bg-white border-indigo-400 focus:ring-indigo-400 text-slate-900 placeholder-slate-300'
                          }`}
                          autoFocus={colIdx === 0}
                        />
                      </td>
                    ))}
                    <td className={`px-2 py-1.5 border-b ${cellBorder}`}>
                      <div className="flex items-center gap-0.5">
                        <button onClick={saveNewRow} className="p-0.5 rounded text-emerald-400 hover:bg-emerald-500/10" title="Save new row">
                          <Save className="w-3.5 h-3.5" />
                        </button>
                        <button onClick={cancelAddRow} className="p-0.5 rounded text-red-400 hover:bg-red-500/10" title="Cancel">
                          <X className="w-3.5 h-3.5" />
                        </button>
                      </div>
                    </td>
                  </tr>
                )}
              </tbody>
            </table>

            {/* Add row button */}
            {canEdit && !addingRow && (
              <button
                onClick={startAddRow}
                className={`w-full flex items-center justify-center gap-1.5 py-2 text-xs transition-colors ${
                  isDark ? 'text-slate-500 hover:text-white hover:bg-white/5' : 'text-slate-400 hover:text-slate-700 hover:bg-slate-50'
                }`}
              >
                <Plus className="w-3.5 h-3.5" />
                Add Row
              </button>
            )}
          </>
        ) : (
          <div className="p-4">
            <pre className={`text-sm font-mono whitespace-pre-wrap break-words ${
              result.success ? isDark ? 'text-slate-300' : 'text-slate-700' : isDark ? 'text-red-300' : 'text-red-600'
            }`}>
              {result.message}
            </pre>
          </div>
        )}
      </div>
    </div>
  )
}
