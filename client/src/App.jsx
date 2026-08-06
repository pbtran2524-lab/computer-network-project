import { useAgent } from './context/AgentContext.jsx';
import Dashboard from './components/Dashboard.jsx';
import SessionView from './components/SessionView.jsx';
import Toaster from './components/Toaster.jsx';

export default function App() {
  const { activeSession } = useAgent();
  return (
    <div className="app">
      {activeSession ? <SessionView /> : <Dashboard />}
      <Toaster />
    </div>
  );
}
