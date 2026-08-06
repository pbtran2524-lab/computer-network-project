import React from 'react';
import { createRoot } from 'react-dom/client';
import App from './App.jsx';
import { AgentProvider } from './context/AgentContext.jsx';
import './styles.css';

createRoot(document.getElementById('root')).render(
  <React.StrictMode>
    <AgentProvider>
      <App />
    </AgentProvider>
  </React.StrictMode>
);
