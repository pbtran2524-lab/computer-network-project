import React from 'react';
import { createRoot } from 'react-dom/client';
import { AgentProvider, useAgent } from './core.jsx';
import Dashboard from './Dashboard.jsx';
import SessionView from './SessionView.jsx';
import Toaster from './ui.jsx';
import './styles.css';

function App() {
  const { activeSession } = useAgent();
  return (
    <div className="app">
      {activeSession ? <SessionView /> : <Dashboard />}
      <Toaster />
    </div>
  );
}

createRoot(document.getElementById('root')).render(
  <React.StrictMode>
    <AgentProvider>
      <App />
    </AgentProvider>
  </React.StrictMode>
);
