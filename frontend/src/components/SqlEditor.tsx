import { useState, useRef, useCallback } from 'react'
import CodeMirror, { type ReactCodeMirrorRef } from '@uiw/react-codemirror'
import { sql } from '@codemirror/lang-sql'
import { Play, Loader2 } from 'lucide-react'

interface Props {
  onExecute: (sql: string) => void
  executing: boolean
  isDark: boolean
}

export default function SqlEditor({ onExecute, executing, isDark }: Props) {
  const [value, setValue] = useState('SELECT 1;')
  const editorRef = useRef<ReactCodeMirrorRef>(null)

  const handleExecute = useCallback(() => {
    const sql = value.trim()
    if (sql) onExecute(sql)
  }, [value, onExecute])

  const handleKeyDown = useCallback(
    (e: React.KeyboardEvent) => {
      if (e.ctrlKey && e.key === 'Enter') {
        e.preventDefault()
        handleExecute()
      }
    },
    [handleExecute]
  )

  const theme = isDark ? 'dark' : 'light'

  return (
    <div className={`flex flex-col border-b ${isDark ? 'border-white/10' : 'border-slate-200'}`}>
      {/* Toolbar */}
      <div className={`flex items-center justify-between px-3 py-1.5 ${isDark ? 'bg-slate-900/80' : 'bg-slate-50'}`}>
        <span className="text-xs font-semibold uppercase tracking-wider text-slate-500">SQL Editor</span>
        <button
          onClick={handleExecute}
          disabled={executing}
          className="flex items-center gap-1.5 px-3 py-1 text-xs font-medium rounded-lg
                     bg-indigo-500 hover:bg-indigo-400 disabled:opacity-50 disabled:cursor-not-allowed
                     text-white transition-colors shadow-sm"
          title="Ctrl+Enter"
        >
          {executing ? (
            <Loader2 className="w-3.5 h-3.5 animate-spin" />
          ) : (
            <Play className="w-3.5 h-3.5" />
          )}
          Run
        </button>
      </div>

      {/* Editor */}
      <div onKeyDown={handleKeyDown} className="flex-1 min-h-[180px]">
        <CodeMirror
          ref={editorRef}
          value={value}
          onChange={setValue}
          extensions={[sql()]}
          theme={theme}
          height="180px"
          basicSetup={{
            lineNumbers: true,
            foldGutter: true,
            highlightActiveLine: true,
            autocompletion: true,
          }}
          style={{
            fontSize: '14px',
            fontFamily: "'JetBrains Mono', 'Fira Code', 'Cascadia Code', monospace",
          }}
        />
      </div>
    </div>
  )
}
