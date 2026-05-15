import { useState, useCallback } from 'react'
import { api } from '../lib/api'
import Sidebar from './Sidebar'
import SqlEditor from './SqlEditor'
import DataGrid from './DataGrid'
import TableWorkspace from './TableWorkspace'
import QuickActionModal from './QuickActionModal'
import ResultMessages from './ResultMessages'
import {
  Database, LogOut, Moon, Sun, Clock, Zap, ChevronLeft,
  Menu, X, Terminal, Table2, Trash2
} from 'lucide-react'

interface Props {
  token: string
  onLogout: () => void
}

export interface ResultMessage {
  id: number
  sql: string
  success: boolean
  type: string
  message: string
  timestamp: number
}

interface HistoryEntry {
  id: number
  sql: string
  timestamp: number
  success: boolean
}

type ModalAction = 'create_table' | 'insert' | 'create_user' | 'grant_priv' | null

let historyId = 0
let msgId = 0

function extractTableName(sql: string): string | null {
  const m = sql.match(/FROM\s+(\w+)(?:\s|;|$)/i)
  if (!m) return null
  if (/JOIN|,|\bIN\b/i.test(sql)) return null
  return m[1]
}

export default function Dashboard({ token, onLogout }: Props) {
  const [theme, setTheme] = useState<'dark' | 'light'>(() => {
    const saved = localStorage.getItem('minidb_theme')
    return (saved === 'dark' || saved === 'light') ? saved : 'light'
  })
  const [currentUser, setCurrentUser] = useState('')
  const [currentDb, setCurrentDb] = useState('')
  const [result, setResult] = useState<ResultMessage | null>(null)
  const [resultHistory, setResultHistory] = useState<ResultMessage[]>([])
  const [history, setHistory] = useState<HistoryEntry[]>([])
  const [executing, setExecuting] = useState(false)
  const [leftOpen, setLeftOpen] = useState(true)
  const [rightOpen, setRightOpen] = useState(true)
  const [activeTab, setActiveTab] = useState<'query' | 'table'>('query')
  const [lastSql, setLastSql] = useState('')
  const [schemaDb, setSchemaDb] = useState('')
  const [schemaTable, setSchemaTable] = useState('')
  const [selectedTable, setSelectedTable] = useState('')
  const [modalAction, setModalAction] = useState<ModalAction>(null)

  const executeSql = useCallback(async (sql: string) => {
    setExecuting(true)
    setLastSql(sql)
    // Don't auto-switch to query tab — let user stay on current tab
    try {
      const { data } = await api.post('/execute', { token, sql })
      const entry: HistoryEntry = { id: ++historyId, sql, timestamp: Date.now(), success: data.success }
      setHistory((prev) => [entry, ...prev].slice(0, 50))
      const msg: ResultMessage = {
        id: ++msgId,
        sql,
        success: data.success,
        type: data.type,
        message: data.message,
        timestamp: Date.now(),
      }
      setResult(msg)
      setResultHistory((prev) => [msg, ...prev].slice(0, 30))
      if (data.currentUser) setCurrentUser(data.currentUser)
      if (data.currentDb) setCurrentDb(data.currentDb)
      // Track table name from successful SELECT
      if (data.success) {
        const t = extractTableName(sql)
        if (t) setSelectedTable(t)
      }
    } catch {
      const msg: ResultMessage = {
        id: ++msgId,
        sql,
        success: false,
        type: 'error',
        message: 'Network error. Is the server running?',
        timestamp: Date.now(),
      }
      setResult(msg)
      setResultHistory((prev) => [msg, ...prev].slice(0, 30))
    } finally {
      setExecuting(false)
    }
  }, [token])

  const deleteHistory = useCallback((id: number) => {
    setHistory((prev) => prev.filter((h) => h.id !== id))
  }, [])
  const clearHistory = useCallback(() => setHistory([]), [])
  const clearResultHistory = useCallback(() => setResultHistory([]), [])

  const handleLogout = async () => {
    try { await api.post('/logout', { token }) } catch { /* ignore */ }
    onLogout()
  }

  const toggleTheme = () => setTheme((t) => {
    const next = t === 'dark' ? 'light' : 'dark'
    localStorage.setItem('minidb_theme', next)
    return next
  })
  const isDark = theme === 'dark'

  const tableName = extractTableName(lastSql)

  // Fix: Quick action "Select All" — use selectedTable when available, disable otherwise
  const selectAllAction: string | (() => void) = selectedTable
    ? `SELECT * FROM ${selectedTable};`
    : 'SHOW TABLES;' // fallback when no table selected yet

  const selectAllLabel = selectedTable
    ? `Select * from ${selectedTable}`
    : 'Select * (click table in Explorer)'

  const quickActions: { label: string; action: (() => void) | string }[] = [
    { label: 'Show Databases', action: 'SHOW DATABASES;' },
    { label: 'Show Tables', action: 'SHOW TABLES;' },
    { label: 'Show Users', action: 'SHOW USERS;' },
    { label: selectAllLabel, action: selectAllAction },
    { label: 'Create Table', action: () => setModalAction('create_table') },
    { label: 'Insert Row', action: () => setModalAction('insert') },
    { label: 'Create User', action: () => setModalAction('create_user') },
    { label: 'Grant Privilege', action: () => setModalAction('grant_priv') },
    { label: 'Show Grants', action: (currentUser ? `SHOW GRANTS FOR ${currentUser};` : 'SHOW GRANTS FOR admin;') },
  ]

  return (
    <div className={`h-screen flex flex-col overflow-hidden ${isDark ? 'bg-slate-950 text-slate-100' : 'bg-slate-50 text-slate-900'}`}>
      {/* Navbar */}
      <nav className={`h-14 flex items-center gap-3 px-4 border-b shrink-0 z-30 ${
        isDark ? 'border-white/10 bg-slate-900/80 backdrop-blur' : 'border-slate-200 bg-white/80 backdrop-blur'
      }`}>
        <button onClick={() => setLeftOpen(!leftOpen)} className={`p-1.5 rounded-lg transition-colors ${isDark ? 'hover:bg-white/10' : 'hover:bg-slate-100'}`}>
          <Menu className="w-5 h-5" />
        </button>
        <div className="flex items-center gap-2 font-semibold text-lg">
          <Database className="w-5 h-5 text-indigo-500" />
          <span className="hidden sm:inline">MiniDBMS</span>
        </div>
        <div className="flex-1" />

        {/* Tabs */}
        <div className={`flex rounded-lg border p-0.5 ${isDark ? 'border-white/10 bg-slate-800' : 'border-slate-200 bg-slate-100'}`}>
          <button onClick={() => setActiveTab('query')} className={`flex items-center gap-1.5 px-3 py-1 rounded-md text-xs font-medium transition-colors ${
            activeTab === 'query' ? isDark ? 'bg-slate-700 text-white' : 'bg-white text-slate-900 shadow-sm' : 'text-slate-500'
          }`}>
            <Terminal className="w-3.5 h-3.5" />Query
          </button>
          <button onClick={() => setActiveTab('table')} className={`flex items-center gap-1.5 px-3 py-1 rounded-md text-xs font-medium transition-colors ${
            activeTab === 'table' ? isDark ? 'bg-slate-700 text-white' : 'bg-white text-slate-900 shadow-sm' : 'text-slate-500'
          }`}>
            <Table2 className="w-3.5 h-3.5" />Table
          </button>
        </div>

        {currentDb && (
          <span className={`text-xs px-2 py-1 rounded-full ${isDark ? 'bg-indigo-500/20 text-indigo-300' : 'bg-indigo-100 text-indigo-700'}`}>
            {currentDb}
          </span>
        )}
        {currentUser && (
          <span className={`text-sm ${isDark ? 'text-slate-400' : 'text-slate-600'}`}>{currentUser}</span>
        )}

        <button onClick={toggleTheme} className={`p-2 rounded-lg transition-colors ${isDark ? 'hover:bg-white/10' : 'hover:bg-slate-100'}`}>
          {isDark ? <Sun className="w-4 h-4" /> : <Moon className="w-4 h-4" />}
        </button>
        <button onClick={handleLogout} className={`p-2 rounded-lg transition-colors hover:text-red-400 ${isDark ? 'hover:bg-white/10' : 'hover:bg-slate-100'}`} title="Logout">
          <LogOut className="w-4 h-4" />
        </button>
      </nav>

      {/* Main content */}
      <div className="flex flex-1 min-h-0 overflow-hidden">
        {/* Left Sidebar */}
        <aside className={`${leftOpen ? 'w-60' : 'w-0'} transition-[width] duration-200 overflow-hidden shrink-0 border-r ${
          isDark ? 'border-white/10 bg-slate-900/50' : 'border-slate-200 bg-white'
        }`}>
          <div className="w-60 h-full">
            <Sidebar
              token={token}
              isDark={isDark}
              onSelectTable={(db, table) => { setSchemaDb(db); setSchemaTable(table); setSelectedTable(table); setActiveTab('table') }}
            />
          </div>
        </aside>

        {/* Center workspace */}
        <main className="flex-1 min-h-0 flex flex-col overflow-hidden min-w-0">
          {activeTab === 'query' ? (
            <>
              <SqlEditor onExecute={executeSql} executing={executing} isDark={isDark} />
              <ResultMessages messages={resultHistory} isDark={isDark} onClear={clearResultHistory} />
              <DataGrid result={result} token={token} isDark={isDark} tableName={tableName} onExecute={executeSql} />
            </>
          ) : (
            <>
              <TableWorkspace token={token} isDark={isDark} currentDb={currentDb} initialDb={schemaDb} initialTable={schemaTable} onExecute={executeSql} />
              <ResultMessages messages={resultHistory} isDark={isDark} onClear={clearResultHistory} />
            </>
          )}
        </main>

        {/* Right Sidebar */}
        <aside className={`${rightOpen ? 'w-64' : 'w-0'} transition-[width] duration-200 overflow-hidden shrink-0 border-l ${
          isDark ? 'border-white/10 bg-slate-900/50' : 'bg-white'
        }`}>
          <div className="w-64 h-full flex flex-col">
            <div className={`flex items-center justify-between px-3 py-2 border-b ${isDark ? 'border-white/10' : 'border-slate-200'}`}>
              <span className="text-xs font-semibold uppercase tracking-wider text-slate-500">Quick Actions</span>
              <button onClick={() => setRightOpen(false)} className={`p-1 rounded ${isDark ? 'hover:bg-white/10' : 'hover:bg-slate-100'}`}>
                <X className="w-3.5 h-3.5" />
              </button>
            </div>

            <div className={`p-3 space-y-1 max-h-72 overflow-y-auto custom-scrollbar`}>
              {quickActions.map((item) => (
                <button
                  key={item.label}
                  onClick={() => {
                    if (typeof item.action === 'function') item.action()
                    else executeSql(item.action)
                  }}
                  disabled={executing}
                  className={`w-full flex items-center gap-2 px-3 py-1.5 text-xs rounded-lg transition-colors text-left ${
                    isDark ? 'hover:bg-white/5 text-slate-400 hover:text-white' : 'hover:bg-slate-100 text-slate-600 hover:text-slate-900'
                  }`}
                >
                  <Zap className="w-3 h-3 shrink-0" />
                  {item.label}
                </button>
              ))}
            </div>

            <div className={`flex items-center justify-between px-3 py-2 border-y ${isDark ? 'border-white/10' : 'border-slate-200'}`}>
              <span className="text-xs font-semibold uppercase tracking-wider text-slate-500">History</span>
              <div className="flex items-center gap-1">
                {history.length > 0 && (
                  <button onClick={clearHistory} className={`p-0.5 rounded text-xs ${isDark ? 'hover:bg-white/10 text-slate-500 hover:text-red-400' : 'hover:bg-slate-100 text-slate-400 hover:text-red-500'}`} title="Clear all">
                    <Trash2 className="w-3 h-3" />
                  </button>
                )}
                <Clock className="w-3.5 h-3.5 text-slate-500" />
              </div>
            </div>

            <div className={`flex-1 overflow-y-auto p-2 space-y-0.5 custom-scrollbar`}>
              {history.length === 0 ? (
                <p className={`text-xs text-center py-8 ${isDark ? 'text-slate-600' : 'text-slate-400'}`}>No queries yet</p>
              ) : (
                history.map((entry) => (
                  <div key={entry.id} className={`group flex items-center rounded transition-colors ${isDark ? 'hover:bg-white/5' : 'hover:bg-slate-100'}`}>
                    <button onClick={() => executeSql(entry.sql)} disabled={executing} className={`flex-1 text-left px-2 py-1.5 text-xs truncate ${entry.success ? isDark ? 'text-slate-400' : 'text-slate-600' : 'text-red-400'}`} title={entry.sql}>
                      {entry.sql}
                    </button>
                    <button onClick={() => deleteHistory(entry.id)} className={`shrink-0 p-1 rounded opacity-0 group-hover:opacity-100 transition-opacity ${isDark ? 'hover:bg-red-500/10 text-slate-600 hover:text-red-400' : 'hover:bg-red-50 text-slate-300 hover:text-red-500'}`} title="Delete entry">
                      <X className="w-3 h-3" />
                    </button>
                  </div>
                ))
              )}
            </div>

            <button onClick={() => setRightOpen(true)} className={`flex items-center justify-center py-1 border-t text-xs text-slate-500 hover:text-slate-400 transition-colors ${isDark ? 'border-white/10' : 'border-slate-200'}`}>
              <ChevronLeft className="w-3.5 h-3.5" />
            </button>
          </div>
        </aside>
      </div>

      {/* Quick Action Modal */}
      <QuickActionModal
        action={modalAction}
        isDark={isDark}
        selectedTable={selectedTable}
        onExecute={(sql) => { executeSql(sql); setModalAction(null) }}
        onClose={() => setModalAction(null)}
      />
    </div>
  )
}
