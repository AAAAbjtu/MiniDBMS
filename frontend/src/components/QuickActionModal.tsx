import { useState, useEffect } from 'react'
import { X, Plus, Trash2, Zap } from 'lucide-react'

type ActionType = 'create_table' | 'insert' | 'create_user' | 'grant_priv' | null

interface Props {
  action: ActionType
  isDark: boolean
  selectedTable: string
  onExecute: (sql: string) => void
  onClose: () => void
}

interface ColDef {
  name: string
  type: string
}

const PRIVILEGES = [
  'SELECT', 'INSERT', 'UPDATE', 'DELETE', 'CREATE_TABLE', 'DROP_TABLE',
  'ALTER_TABLE', 'SHOW_TABLES', 'DESCRIBE', 'CREATE_DB', 'DROP_DB',
  'SHOW_DATABASES', 'CONNECT', 'CREATE_USER', 'DROP_USER', 'SHOW_USERS',
  'CREATE_ROLE', 'DROP_ROLE', 'GRANT', 'REVOKE', 'SHOW_GRANTS', 'SHOW_AUDIT'
]

const ROLES = ['USER', 'DBA', 'ADMIN']
const TYPES = ['INT', 'TEXT', 'FLOAT', 'DOUBLE', 'BOOL', 'DATE', 'STRING']

export default function QuickActionModal({ action, isDark, selectedTable, onExecute, onClose }: Props) {
  // Create Table
  const [tableName, setTableName] = useState('')
  const [cols, setCols] = useState<ColDef[]>([{ name: '', type: 'INT' }])

  // Insert
  const [insTable, setInsTable] = useState(selectedTable || '')
  const [insValues, setInsValues] = useState('')

  // Create User
  const [cuName, setCuName] = useState('')
  const [cuPass, setCuPass] = useState('')
  const [cuRole, setCuRole] = useState('USER')

  // Grant
  const [gpPrivs, setGpPrivs] = useState<string[]>([])
  const [gpUser, setGpUser] = useState('')

  useEffect(() => {
    if (action === 'insert' && selectedTable) setInsTable(selectedTable)
    setTableName('')
    setCols([{ name: '', type: 'INT' }])
    setInsValues('')
    setCuName(''); setCuPass(''); setCuRole('USER')
    setGpPrivs([]); setGpUser('')
  }, [action, selectedTable])

  if (!action) return null

  const addCol = () => setCols((prev) => [...prev, { name: '', type: 'INT' }])
  const removeCol = (i: number) => setCols((prev) => prev.filter((_, idx) => idx !== i))
  const updateCol = (i: number, field: keyof ColDef, val: string) => {
    setCols((prev) => prev.map((c, idx) => (idx === i ? { ...c, [field]: val } : c)))
  }

  const handleCreateTable = () => {
    if (!tableName.trim() || cols.some((c) => !c.name.trim())) return
    const defs = cols.map((c) => `${c.name.trim()} ${c.type}`).join(', ')
    onExecute(`CREATE TABLE ${tableName.trim()} (${defs});`)
    onClose()
  }

  const handleInsert = () => {
    if (!insTable.trim() || !insValues.trim()) return
    onExecute(`INSERT INTO ${insTable.trim()} VALUES (${insValues.trim()});`)
    onClose()
  }

  const handleCreateUser = () => {
    if (!cuName.trim() || !cuPass.trim()) return
    onExecute(`CREATE USER ${cuName.trim()} IDENTIFIED BY '${cuPass.trim()}' ROLE ${cuRole};`)
    onClose()
  }

  const handleGrant = () => {
    if (gpPrivs.length === 0 || !gpUser.trim()) return
    onExecute(`GRANT ${gpPrivs.join(', ')} TO ${gpUser.trim()};`)
    onClose()
  }

  const overlay = 'fixed inset-0 z-50 flex items-center justify-center bg-black/50 backdrop-blur-sm'
  const cardBg = isDark ? 'bg-slate-900 border-white/10' : 'bg-white border-slate-200'
  const inputClass = `w-full px-3 py-2 text-sm rounded-lg border focus:outline-none focus:ring-1 transition-all ${
    isDark ? 'bg-slate-800 border-white/10 focus:ring-indigo-500 focus:border-indigo-500 text-white placeholder-slate-600' : 'bg-white border-slate-200 focus:ring-indigo-400 focus:border-indigo-400 text-slate-900 placeholder-slate-300'
  }`
  const btn = `flex items-center gap-1.5 px-4 py-2 text-sm font-medium rounded-lg transition-colors`
  const muted = isDark ? 'text-slate-400' : 'text-slate-500'

  return (
    <div className={overlay} onClick={onClose}>
      <div className={`w-full max-w-2xl mx-4 rounded-2xl border shadow-2xl ${cardBg} p-6`} onClick={(e) => e.stopPropagation()}>
        <div className="flex items-center justify-between mb-5">
          <div className="flex items-center gap-2">
            <Zap className={`w-4 h-4 ${isDark ? 'text-indigo-400' : 'text-indigo-600'}`} />
            <h2 className="text-lg font-semibold">
              {action === 'create_table' && 'Create Table'}
              {action === 'insert' && 'Insert Row'}
              {action === 'create_user' && 'Create User'}
              {action === 'grant_priv' && 'Grant Privilege'}
            </h2>
          </div>
          <button onClick={onClose} className={`p-1 rounded-lg ${isDark ? 'hover:bg-white/10' : 'hover:bg-slate-100'}`}>
            <X className="w-5 h-5" />
          </button>
        </div>

        {/* Create Table */}
        {action === 'create_table' && (
          <div className="space-y-4">
            <div>
              <label className={`block text-xs font-medium mb-1 ${muted}`}>Table Name</label>
              <input type="text" value={tableName} onChange={(e) => setTableName(e.target.value)} placeholder="my_table" className={inputClass} autoFocus onKeyDown={(e) => { if (e.key === 'Enter') handleCreateTable() }} />
            </div>
            <div>
              <label className={`block text-xs font-medium mb-2 ${muted}`}>Columns</label>
              <div className="space-y-2">
                {cols.map((col, i) => (
                  <div key={i} className="flex gap-2">
                    <input type="text" value={col.name} onChange={(e) => updateCol(i, 'name', e.target.value)} placeholder="column_name" className={`flex-[4] min-w-0 ${inputClass}`} onKeyDown={(e) => { if (e.key === 'Enter') handleCreateTable() }} />
                    <select value={col.type} onChange={(e) => updateCol(i, 'type', e.target.value)} className={`flex-[1] min-w-0 ${inputClass}`}>
                      {TYPES.map((t) => <option key={t} value={t}>{t}</option>)}
                    </select>
                    {cols.length > 1 && (
                      <button onClick={() => removeCol(i)} className={`p-2 rounded-lg ${isDark ? 'hover:bg-red-500/10 text-slate-600 hover:text-red-400' : 'hover:bg-red-50 text-slate-300 hover:text-red-500'}`}>
                        <Trash2 className="w-4 h-4" />
                      </button>
                    )}
                  </div>
                ))}
              </div>
              <button onClick={addCol} className={`mt-2 flex items-center gap-1 text-xs ${isDark ? 'text-indigo-400 hover:text-indigo-300' : 'text-indigo-600 hover:text-indigo-500'}`}>
                <Plus className="w-3 h-3" /> Add column
              </button>
            </div>
            <button onClick={handleCreateTable} className={`${btn} w-full justify-center bg-indigo-500 hover:bg-indigo-400 text-white`}>Create Table</button>
          </div>
        )}

        {/* Insert Row */}
        {action === 'insert' && (
          <div className="space-y-4">
            <div>
              <label className={`block text-xs font-medium mb-1 ${muted}`}>Table</label>
              <input type="text" value={insTable} onChange={(e) => setInsTable(e.target.value)} placeholder="table_name" className={inputClass} autoFocus />
            </div>
            <div>
              <label className={`block text-xs font-medium mb-1 ${muted}`}>Values (comma-separated)</label>
              <input type="text" value={insValues} onChange={(e) => setInsValues(e.target.value)} placeholder="1, 'hello', 42" className={inputClass} onKeyDown={(e) => { if (e.key === 'Enter') handleInsert() }} />
              <p className={`text-[10px] mt-1 ${muted}`}>Strings must be quoted with single quotes</p>
            </div>
            <button onClick={handleInsert} className={`${btn} w-full justify-center bg-indigo-500 hover:bg-indigo-400 text-white`}>Insert Row</button>
          </div>
        )}

        {/* Create User */}
        {action === 'create_user' && (
          <div className="space-y-4">
            <div>
              <label className={`block text-xs font-medium mb-1 ${muted}`}>Username</label>
              <input type="text" value={cuName} onChange={(e) => setCuName(e.target.value)} placeholder="username" className={inputClass} autoFocus />
            </div>
            <div>
              <label className={`block text-xs font-medium mb-1 ${muted}`}>Password</label>
              <input type="password" value={cuPass} onChange={(e) => setCuPass(e.target.value)} placeholder="password" className={inputClass} />
            </div>
            <div>
              <label className={`block text-xs font-medium mb-1 ${muted}`}>Role</label>
              <select value={cuRole} onChange={(e) => setCuRole(e.target.value)} className={inputClass}>
                {ROLES.map((r) => <option key={r} value={r}>{r}</option>)}
              </select>
            </div>
            <button onClick={handleCreateUser} className={`${btn} w-full justify-center bg-indigo-500 hover:bg-indigo-400 text-white`}>Create User</button>
          </div>
        )}

        {/* Grant Privilege */}
        {action === 'grant_priv' && (
          <div className="space-y-4">
            <div>
              <label className={`block text-xs font-medium mb-1 ${muted}`}>Privilege</label>
              <select
                value={gpPrivs[0] || ''}
                onChange={(e) => setGpPrivs(e.target.value ? [e.target.value] : [])}
                className={inputClass}
              >
                <option value="">-- Select privilege --</option>
                {PRIVILEGES.map((p) => <option key={p} value={p}>{p}</option>)}
              </select>
              <p className={`text-[10px] mt-1 ${muted}`}>MiniDB supports one privilege per GRANT statement</p>
            </div>
            <div>
              <label className={`block text-xs font-medium mb-1 ${muted}`}>Target User</label>
              <input type="text" value={gpUser} onChange={(e) => setGpUser(e.target.value)} placeholder="username" className={inputClass} onKeyDown={(e) => { if (e.key === 'Enter') handleGrant() }} />
            </div>
            <button onClick={handleGrant} className={`${btn} w-full justify-center bg-indigo-500 hover:bg-indigo-400 text-white`}>Grant</button>
          </div>
        )}
      </div>
    </div>
  )
}
