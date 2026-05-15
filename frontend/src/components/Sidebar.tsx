import { useState, useEffect, useCallback } from 'react'
import { api } from '../lib/api'
import { ChevronRight, ChevronDown, Database, Table, Columns, Loader2, RefreshCw, AlertCircle, Pencil } from 'lucide-react'

interface Props {
  token: string
  isDark: boolean
  onSelectTable?: (db: string, table: string) => void
}

interface ColumnInfo {
  name: string
  type: string
}

interface TableInfo {
  name: string
  columns: ColumnInfo[] | null
}

interface DbInfo {
  name: string
  tables: TableInfo[] | null
}

function parseTableList(output: string): string[] {
  // MiniDB format: header line, then one name per line, then "(N databases/tables)"
  // Example: "Databases\ndefault\ntest\n(2 databases)\n"
  const lines = output.split('\n').map((l) => l.trim()).filter((l) => l)
  const items: string[] = []
  let first = true
  for (const line of lines) {
    if (first) { first = false; continue } // skip header
    if (/^\(\d+/.test(line)) continue // skip summary like "(1 databases)"
    items.push(line)
  }
  return items
}

function parseDescribe(output: string): ColumnInfo[] {
  // MiniDB format: tab-separated
  // Example: "Field\tType\tConstraints\nid\tINT\t\nname\tTEXT\t\n(2 columns)\n"
  const lines = output.split('\n').map((l) => l.trim()).filter((l) => l)
  const cols: ColumnInfo[] = []
  let first = true
  for (const line of lines) {
    if (first) { first = false; continue } // skip header
    if (/^\(\d+/.test(line)) continue // skip summary
    const parts = line.split('\t')
    if (parts.length >= 2 && parts[0]) {
      cols.push({ name: parts[0], type: parts[1] || 'UNKNOWN' })
    }
  }
  return cols
}

export default function Sidebar({ token, isDark, onSelectTable }: Props) {
  const [dbs, setDbs] = useState<DbInfo[]>([])
  const [expandedDbs, setExpandedDbs] = useState<Set<string>>(new Set())
  const [expandedTables, setExpandedTables] = useState<Set<string>>(new Set())
  const [loading, setLoading] = useState(true)
  const [error, setError] = useState('')

  const fetchDatabases = useCallback(async () => {
    setLoading(true)
    setError('')
    try {
      const { data } = await api.post('/execute', { token, sql: 'SHOW DATABASES;' })
      if (data.success) {
        const dbNames = parseTableList(data.message)
        setDbs(dbNames.map((name) => ({ name, tables: null })))
      } else {
        setError(data.message)
      }
    } catch {
      setError('Failed to fetch databases')
    } finally {
      setLoading(false)
    }
  }, [token])

  useEffect(() => {
    fetchDatabases()
  }, [fetchDatabases])

  const fetchTables = async (dbName: string) => {
    setDbs((prev) =>
      prev.map((d) => {
        if (d.name !== dbName) return d
        return { ...d, tables: (d.tables ?? []) as TableInfo[] }
      })
    )
    try {
      const { data } = await api.post('/execute', { token, sql: `SHOW TABLES IN ${dbName};` })
      if (data.success) {
        const tableNames = parseTableList(data.message)
        setDbs((prev) =>
          prev.map((d) =>
            d.name === dbName
              ? { ...d, tables: tableNames.map((name) => ({ name, columns: null })) }
              : d
          )
        )
      }
    } catch {
      // silently fail
    }
  }

  const fetchColumns = async (dbName: string, tableName: string) => {
    setDbs((prev) =>
      prev.map((d) => {
        if (d.name !== dbName) return d
        return {
          ...d,
          tables: (d.tables ?? []).map((t) =>
            t.name === tableName ? { ...t, columns: t.columns ?? [] } : t
          ),
        }
      })
    )
    try {
      const { data } = await api.post('/execute', { token, sql: `DESCRIBE ${tableName};` })
      if (data.success) {
        const cols = parseDescribe(data.message)
        setDbs((prev) =>
          prev.map((d) => {
            if (d.name !== dbName) return d
            return {
              ...d,
              tables: (d.tables ?? []).map((t) =>
                t.name === tableName ? { ...t, columns: cols } : t
              ),
            }
          })
        )
      }
    } catch {
      // silently fail
    }
  }

  const toggleDb = (dbName: string) => {
    const next = new Set(expandedDbs)
    if (next.has(dbName)) {
      next.delete(dbName)
    } else {
      next.add(dbName)
      const db = dbs.find((d) => d.name === dbName)
      if (db?.tables === null) fetchTables(dbName)
    }
    setExpandedDbs(next)
  }

  const toggleTable = (dbName: string, tableName: string) => {
    const key = `${dbName}.${tableName}`
    const next = new Set(expandedTables)
    if (next.has(key)) {
      next.delete(key)
    } else {
      next.add(key)
      const db = dbs.find((d) => d.name === dbName)
      const table = db?.tables?.find((t) => t.name === tableName)
      if (table?.columns === null) fetchColumns(dbName, tableName)
    }
    setExpandedTables(next)
  }

  const muted = isDark ? 'text-slate-500' : 'text-slate-400'
  const hover = isDark ? 'hover:bg-white/5' : 'hover:bg-slate-100'
  const border = isDark ? 'border-white/10' : 'border-slate-200'

  return (
    <div className="h-full flex flex-col">
      <div className={`flex items-center justify-between px-3 py-2 border-b ${border}`}>
        <span className="text-xs font-semibold uppercase tracking-wider text-slate-500">Explorer</span>
        <button onClick={fetchDatabases} className={`p-1 rounded transition-colors ${hover}`} title="Refresh">
          <RefreshCw className={`w-3.5 h-3.5 ${loading ? 'animate-spin' : ''} ${muted}`} />
        </button>
      </div>

      <div className="flex-1 overflow-y-auto py-1">
        {loading ? (
          <div className="flex items-center justify-center py-8">
            <Loader2 className={`w-5 h-5 animate-spin ${muted}`} />
          </div>
        ) : error ? (
          <div className={`flex items-center gap-2 px-3 py-4 text-xs ${isDark ? 'text-red-400' : 'text-red-600'}`}>
            <AlertCircle className="w-3.5 h-3.5 shrink-0" />
            {error}
          </div>
        ) : dbs.length === 0 ? (
          <p className={`text-xs text-center py-8 ${muted}`}>No databases found</p>
        ) : (
          dbs.map((db) => (
            <div key={db.name}>
              <button
                onClick={() => toggleDb(db.name)}
                className={`w-full flex items-center gap-1.5 px-3 py-1.5 text-sm transition-colors ${hover}`}
              >
                {expandedDbs.has(db.name) ? (
                  <ChevronDown className={`w-3.5 h-3.5 ${muted}`} />
                ) : (
                  <ChevronRight className={`w-3.5 h-3.5 ${muted}`} />
                )}
                <Database className={`w-3.5 h-3.5 ${isDark ? 'text-indigo-400' : 'text-indigo-600'}`} />
                <span className="truncate">{db.name}</span>
              </button>

              {expandedDbs.has(db.name) && (
                <div className="ml-4">
                  {db.tables === null ? (
                    <div className="px-3 py-1">
                      <Loader2 className={`w-3.5 h-3.5 animate-spin ${muted}`} />
                    </div>
                  ) : db.tables.length === 0 ? (
                    <p className={`px-3 py-1 text-xs ${muted}`}>No tables</p>
                  ) : (
                    db.tables.map((table) => (
                      <div key={table.name}>
                        <div className="group flex items-center">
                          <button
                            onClick={() => toggleTable(db.name, table.name)}
                            className={`flex-1 flex items-center gap-1.5 px-3 py-1.5 text-sm transition-colors ${hover}`}
                          >
                            {expandedTables.has(`${db.name}.${table.name}`) ? (
                              <ChevronDown className={`w-3 h-3 ${muted}`} />
                            ) : (
                              <ChevronRight className={`w-3 h-3 ${muted}`} />
                            )}
                            <Table className={`w-3.5 h-3.5 ${isDark ? 'text-emerald-400' : 'text-emerald-600'}`} />
                            <span className="truncate text-xs">{table.name}</span>
                          </button>
                          {onSelectTable && (
                            <button
                              onClick={() => onSelectTable(db.name, table.name)}
                              className={`shrink-0 p-1 mr-1 rounded opacity-0 group-hover:opacity-100 transition-opacity ${
                                isDark ? 'hover:bg-white/10 text-slate-600 hover:text-indigo-400' : 'hover:bg-slate-100 text-slate-300 hover:text-indigo-600'
                              }`}
                              title="Manage schema"
                            >
                              <Pencil className="w-3 h-3" />
                            </button>
                          )}
                        </div>

                        {expandedTables.has(`${db.name}.${table.name}`) && (
                          <div className="ml-6">
                            {table.columns === null ? (
                              <div className="px-3 py-1">
                                <Loader2 className={`w-3 h-3 animate-spin ${muted}`} />
                              </div>
                            ) : table.columns.length === 0 ? (
                              <p className={`px-3 py-1 text-xs ${muted}`}>No columns</p>
                            ) : (
                              table.columns.map((col) => (
                                <div
                                  key={col.name}
                                  className={`flex items-center gap-1.5 px-3 py-1 text-xs transition-colors ${hover}`}
                                >
                                  <Columns className={`w-3 h-3 ${muted}`} />
                                  <span className="truncate">{col.name}</span>
                                  <span className={`ml-auto text-[10px] ${muted}`}>{col.type}</span>
                                </div>
                              ))
                            )}
                          </div>
                        )}
                      </div>
                    ))
                  )}
                </div>
              )}
            </div>
          ))
        )}
      </div>
    </div>
  )
}
