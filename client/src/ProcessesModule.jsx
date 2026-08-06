import { useCallback, useEffect, useRef, useState } from 'react';
import { useModule } from '../hooks/useModule.js';
import { useCommand } from '../hooks/useCommand.js';
import { formatRam } from '../utils.js';
import ConsentOverlay from '../components/ConsentOverlay.jsx';
import { IdleState, RefusedState, TimeoutState } from '../components/ModuleStatus.jsx';

export default function ProcessesModule({ agent }) {
  const [rows, setRows] = useState([]);
  const [killing, setKilling] = useState(new Set());
  const killCleanups = useRef([]);
  const online = agent.status === 'online';

  const { status, send, cancel } = useModule(agent.id, 'processes', {
    onData: (msg) => {
      if (msg.type === 'processes' && msg.action === 'kill') {
        setRows((cur) => cur.filter((r) => r.pid !== msg.pid));
        setKilling((cur) => {
          const next = new Set(cur);
          next.delete(msg.pid);
          return next;
        });
        return;
      }
      if (msg.type === 'processes' && Array.isArray(msg.rows)) setRows(msg.rows);
    },
  });

  const { run } = useCommand();

  const kill = useCallback((pid) => {
    setKilling((cur) => new Set(cur).add(pid));
    const { cleanup } = run(
      agent.id,
      'processes',
      'kill',
      { pid },
      {
        onRejected: () => setKilling((cur) => { const n = new Set(cur); n.delete(pid); return n; }),
        onTimeout: () => setKilling((cur) => { const n = new Set(cur); n.delete(pid); return n; }),
        onError: () => setKilling((cur) => { const n = new Set(cur); n.delete(pid); return n; }),
      }
    );
    killCleanups.current.push(cleanup);
  }, [run, agent.id]);

  useEffect(() => () => {
    killCleanups.current.forEach((c) => c());
  }, []);

  const start = () => send('list', {});
  const stop = () => send('stop', {}, { consent: false });

  if (status === 'idle') {
    return (
      <div className="module">
        <IdleState
          title="Quản lý tiến trình"
          description="Liệt kê các tiến trình đang chạy trên máy đích kèm CPU, RAM. Bạn có thể đóng từng tiến trình bằng nút Kill."
          actionLabel="Bắt đầu liệt kê tiến trình"
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
        subtitle={`Yêu cầu quyền đọc danh sách tiến trình trên ${agent.name}`}
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
      {status === 'active' && (
        <>
          <div className="toolbar">
            <span className="live-badge">Đang cập nhật mỗi 2 giây</span>
            <span className="toolbar-spacer" />
            <button className="btn btn-ghost" onClick={stop} type="button">
              Dừng cập nhật
            </button>
          </div>
          <div className="table-wrap">
            <table className="data-table">
              <thead>
                <tr>
                  <th>#</th>
                  <th>Tên tiến trình</th>
                  <th>PID</th>
                  <th>CPU</th>
                  <th>RAM</th>
                  <th></th>
                </tr>
              </thead>
              <tbody>
                {rows.map((r, i) => (
                  <tr key={`${r.pid}-${r.name}-${i}`}>
                    <td className="muted">{i + 1}</td>
                    <td className="mono">{r.name}</td>
                    <td className="mono">{r.pid}</td>
                    <td>
                      <div className="meter">
                        <div className="meter-fill cpu" style={{ width: `${Math.min(100, r.cpu * 2)}%` }} />
                        <span>{r.cpu.toFixed(1)}%</span>
                      </div>
                    </td>
                    <td>
                      <div className="meter">
                        <div className="meter-fill ram" style={{ width: `${Math.min(100, (r.ram / 1400) * 100)}%` }} />
                        <span>{formatRam(r.ram)}</span>
                      </div>
                    </td>
                    <td>
                      <button className="btn btn-danger btn-sm" onClick={() => kill(r.pid)} disabled={killing.has(r.pid)} type="button">
                        {killing.has(r.pid) ? 'Đang đóng...' : 'Kill'}
                      </button>
                    </td>
                  </tr>
                ))}
              </tbody>
            </table>
          </div>
        </>
      )}
    </div>
  );
}
