import { useState } from 'react';
import { useModule } from '../hooks/useModule.js';
import ConsentOverlay from '../components/ConsentOverlay.jsx';

const ACTIONS = [
  { action: 'sleep', label: 'Sleep', desc: 'Đưa máy đích về trạng thái ngủ', danger: false },
  { action: 'restart', label: 'Restart', desc: 'Khởi động lại máy đích', danger: true },
  { action: 'shutdown', label: 'Shutdown', desc: 'Tắt máy đích hoàn toàn', danger: true },
];

export default function PowerModule({ agent }) {
  const [confirming, setConfirming] = useState(null);
  const [executed, setExecuted] = useState(null);
  const online = agent.status === 'online';

  const { status, send, cancel } = useModule(agent.id, 'power', {
    onData: (msg) => {
      if (msg.type === 'status' && msg.status === 'executed') setExecuted(msg.message);
    },
  });

  const confirm = (a) => {
    setConfirming(a);
    setExecuted(null);
  };

  const runAction = (a) => {
    setConfirming(null);
    send('exec', { action: a.action });
  };

  return (
    <div className="module">
      <ConsentOverlay
        visible={status === 'consent'}
        subtitle={`Yêu cầu quyền điều khiển nguồn trên ${agent.name}`}
        onCancel={cancel}
      />
      {executed && <div className="module-flag success">{executed} — Agent sẽ rơi vào trạng thái offline trong vài giây.</div>}
      <div className="power-grid">
        {ACTIONS.map((a) => (
          <div key={a.action} className={`power-card ${a.danger ? 'danger' : ''}`}>
            <div className="power-label">{a.label}</div>
            <div className="power-desc">{a.desc}</div>
            <button
              className={`btn ${a.danger ? 'btn-danger' : 'btn-primary'}`}
              onClick={() => confirm(a)}
              disabled={!online}
              type="button"
            >
              Thực hiện
            </button>
          </div>
        ))}
      </div>

      {confirming && (
        <div className="confirm-modal">
          <div className="confirm-box">
            <div className="confirm-title">Xác nhận {confirming.label}</div>
            <p>
              Bạn có chắc muốn {confirming.desc} (<span className="mono">{agent.name}</span>)? Thao tác này cần người dùng
              trên máy đích đồng ý.
            </p>
            <div className="confirm-actions">
              <button className="btn" onClick={() => setConfirming(null)} type="button">
                Hủy
              </button>
              <button className="btn btn-danger" onClick={() => runAction(confirming)} type="button">
                Xác nhận {confirming.label}
              </button>
            </div>
          </div>
        </div>
      )}
    </div>
  );
}
