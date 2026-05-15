import { useState } from 'react'
import AuthView from './components/AuthView'
import Dashboard from './components/Dashboard'

export default function App() {
  const [token, setToken] = useState<string | null>(localStorage.getItem('minidb_token'))

  const handleLogin = (newToken: string, _user: string, _db: string) => {
    localStorage.setItem('minidb_token', newToken)
    setToken(newToken)
  }

  const handleLogout = () => {
    localStorage.removeItem('minidb_token')
    setToken(null)
  }

  return (
    <div className="min-h-screen bg-slate-950 text-slate-100 font-sans">
      {token ? (
        <Dashboard token={token} onLogout={handleLogout} />
      ) : (
        <AuthView onLogin={handleLogin} />
      )}
    </div>
  )
}