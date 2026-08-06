import { useState } from 'react';
import { useModule } from '../hooks/useModule.js';
import ConsentOverlay from '../components/ConsentOverlay.jsx';
import { IdleState, RefusedState, TimeoutState } from '../components/ModuleStatus.jsx';

export default function ShellModule({ agent }) {
  const [lines, setLines] = useState([]);
  const [input, setInput] = useState('');
  const [sessionReady, setSessionReady] = useState(false);
  const online = agent.status === 'online';

  const { status, send, cancel } = useModule(agent.id, 'shell', {
    onAccepted: () => setSessionReady(true),
    onData: (msg) => {
      if (msg.type === 'output') {
        setLines((cur) => [...cur, { type: 'in', text: msg.cmd }, { type: 'out', text: msg.text }]);
      }
    },
  });

  const start = () => {
    setLines([]);
    setSessionReady(false);
    send('exec', { cmd: 'help' });
  };

  const runCommand = () => {
    const cmd = input.trim();
    if (!cmd) return;
    setInput('');
    if (sessionReady) {
      setLines((cur) => [...cur, { type: 'in', text: cmd }]);
      send('exec', { cmd }, { consent: false });
    } else {
      send('exec', { cmd });
    }
  };

  if (status === 'idle' && !sessionReady) {
    return (
      <div className="module">
        <IdleState
          title="Shell / Remote Command"
          description="Mở phiên dòng lệnh trên máy đích. Lệnh đầu tiên cần quyền của người dùng; sau khi được đồng ý, các lệnh tiếp theo chạy trực tiếp."
          actionLabel="Mở phiên Shell"
          onAction={start}
          disabled={!online}
        />
      </div>
    );
  }

  return (
    <div className="module">
      <ConsentOverlay
        visible={status === 'consent'}
        subtitle={`Yêu cầu quyền mở phiên Shell trên ${agent.name}`}
        onCancel={cancel}
      />
      {status === 'rejected' && (
        <div className="module">
          <RefusedState onRetry={start} />
        </div>
      )}
      {status === 'timeout' && (
        <div className="module">
          <TimeoutState onRetry={start} />
        </div>
      )}
      {(sessionReady || status === 'active') && (
        <>
          <div className="toolbar">
            <span className="live-badge">Shell · {agent.name}</span>
            <span className="toolbar-spacer" />
            {sessionReady && <span className="session-ready">Phiên đã sẵn sàng</span>}
          </div>
          <div className="shell-term mono">
            {lines.map((l, i) => (
              <div key={i} className={`shell-line ${l.type}`}>
                {l.type === 'in' ? <span className="shell-prompt">C:\&gt;</span> : null}
                <pre className="shell-text">{l.text}</pre>
              </div>
            ))}
            <div className="shell-input-row">
              <span className="shell-prompt">C:\&gt;</span>
              <input
                className="shell-input"
                value={input}
                onChange={(e) => setInput(e.target.value)}
                onKeyDown={(e) => e.key === 'Enter' && runCommand()}
                placeholder="Gõ lệnh (dir, ipconfig, whoami, echo ...) rồi Enter"
                autoFocus
              />
            </div>
          </div>
        </>
      )}
    </div>
  );
}
