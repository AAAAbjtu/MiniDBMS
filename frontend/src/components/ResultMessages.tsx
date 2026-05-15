import { CheckCircle, XCircle, Info, Trash2, Clock } from 'lucide-react'
import type { ResultMessage } from './Dashboard'

interface Props {
  messages: ResultMessage[]
  isDark: boolean
  onClear: () => void
}

function formatTime(ts: number): string {
  return new Date(ts).toLocaleTimeString('en-US', { hour12: false })
}

function getRowCount(msg: string): string | null {
  const m = msg.match(/\((\d+)\s+rows?\)/)
  if (m) return `${m[1]} rows returned`
  const m2 = msg.match(/(\d+)\s+rows?\s+(inserted|deleted|updated)/i)
  if (m2) return `${m2[1]} rows ${m2[2]}`
  const m3 = msg.match(/(\d+)\s+columns?\)/)
  if (m3) return `${m3[1]} columns`
  return null
}

export default function ResultMessages({ messages, isDark, onClear }: Props) {
  if (messages.length === 0) return null

  const border = isDark ? 'border-white/10' : 'border-slate-200'

  return (
    <div className={`flex-1 min-h-0 flex flex-col overflow-hidden border-b ${border}`}>
      <div className={`flex items-center justify-between px-3 py-1 border-b ${border} ${isDark ? 'bg-slate-900/80' : 'bg-slate-50'}`}>
        <span className="text-xs font-semibold uppercase tracking-wider text-slate-500">Messages</span>
        <button
          onClick={onClear}
          className={`flex items-center gap-1 text-[10px] rounded px-1.5 py-0.5 transition-colors ${
            isDark ? 'hover:bg-white/10 text-slate-500 hover:text-slate-300' : 'hover:bg-slate-100 text-slate-400 hover:text-slate-600'
          }`}
        >
          <Trash2 className="w-3 h-3" />
          Clear
        </button>
      </div>
      <div className="flex-1 overflow-y-auto custom-scrollbar p-2 space-y-1.5">
        {messages.map((msg) => {
          const isError = !msg.success
          const isInfo = msg.success && msg.type === 'info'
          const rowInfo = getRowCount(msg.message)

          const cardBg = isDark
            ? isError ? 'bg-red-500/5 border-red-500/20' : isInfo ? 'bg-blue-500/5 border-blue-500/20' : 'bg-emerald-500/5 border-emerald-500/20'
            : isError ? 'bg-red-50 border-red-200' : isInfo ? 'bg-blue-50 border-blue-200' : 'bg-emerald-50 border-emerald-200'

          const iconColor = isError ? 'text-red-400' : isInfo ? 'text-blue-400' : 'text-emerald-400'
          const title = isError ? 'SQL Error' : isInfo ? 'Info' : 'Query executed'

          return (
            <div key={msg.id} className={`rounded-lg border px-3 py-2 text-xs ${cardBg}`}>
              <div className="flex items-center gap-1.5 mb-1">
                {isError ? <XCircle className={`w-3.5 h-3.5 ${iconColor}`} />
                : isInfo ? <Info className={`w-3.5 h-3.5 ${iconColor}`} />
                : <CheckCircle className={`w-3.5 h-3.5 ${iconColor}`} />}
                <span className={`font-semibold ${isDark ? 'text-slate-200' : 'text-slate-800'}`}>{title}</span>
                <span className={`ml-auto text-[10px] flex items-center gap-1 ${isDark ? 'text-slate-500' : 'text-slate-400'}`}>
                  <Clock className="w-2.5 h-2.5" />
                  {formatTime(msg.timestamp)}
                </span>
              </div>
              <div className={`font-mono text-[11px] mb-1 truncate ${isDark ? 'text-slate-400' : 'text-slate-600'}`}>
                {msg.sql}
              </div>
              <div className={`${isError ? (isDark ? 'text-red-300' : 'text-red-600') : isDark ? 'text-slate-400' : 'text-slate-600'}`}>
                {isError ? msg.message : (rowInfo || msg.message)}
              </div>
            </div>
          )
        })}
      </div>
    </div>
  )
}
