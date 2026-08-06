import { useAgent } from './core.jsx';

export default function Dashboard() {
  const { connected, agents, refreshAgents, connectAgent } = useAgent();
  const online = agents.filter((a) => a.status === 'online').length;
  const offline = agents.length - online;

  return (
    <div className="page dashboard">
      <header className="topbar">
        <div className="brand">
          <div className="brand-mark" />
          <div>
            <div className="brand-name">Agent Console</div>
            <div className="brand-sub">Bảng điều khiển quản trị Agent</div>
          </div>
        </div>
        <div className="topbar-right">
          <span className={`server-pill ${connected ? 'ok' : 'bad'}`}>
            <span className="pill-dot" />
            {connected ? 'Đã kết nối server' : 'Mất kết nối server'}
          </span>
          <button className="btn btn-ghost" onClick={refreshAgents} type="button">
            Làm mới
          </button>
        </div>
      </header>

      <div className="stats">
        <div className="stat-card">
          <div className="stat-value">{agents.length}</div>
          <div className="stat-label">Tổng Agent</div>
        </div>
        <div className="stat-card">
          <div className="stat-value blue">{online}</div>
          <div className="stat-label">Online</div>
        </div>
        <div className="stat-card">
          <div className="stat-value gray">{offline}</div>
          <div className="stat-label">Offline</div>
        </div>
      </div>

      <h2 className="section-title">Danh sách Agent đang kết nối</h2>

      {agents.length === 0 ? (
        <div className="empty-state">Chưa có Agent nào gửi trạng thái về.</div>
      ) : (
        <div className="agent-grid">
          {agents.map((agent) => {
            const isOnline = agent.status === 'online';
            return (
              <div key={agent.id} className={`agent-card ${isOnline ? 'online' : 'offline'}`}>
                <div className="agent-head">
                  <span className={`dot ${isOnline ? 'online' : 'offline'}`} title={isOnline ? 'Online' : 'Offline'} />
                  <div className="agent-id">
                    <div className="agent-name">{agent.name}</div>
                    <div className="agent-ip">{agent.ip}</div>
                  </div>
                  <span className={`status-text ${isOnline ? 'online' : 'offline'}`}>{isOnline ? 'Online' : 'Offline'}</span>
                </div>
                <div className="agent-os">{agent.os}</div>
                <button className="btn connect-btn" disabled={!isOnline} onClick={() => connectAgent(agent.id)} type="button">
                  {isOnline ? 'Connect' : 'Không khả dụng'}
                </button>
              </div>
            );
          })}
        </div>
      )}
    </div>
  );
}
