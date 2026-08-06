import { useCallback, useEffect, useRef, useState } from 'react';
import { useModule, useCommand, downloadBlob, formatBytes, formatRam } from './core.jsx';
import { ConsentOverlay, IdleState, RefusedState, TimeoutState } from './ui.jsx';

// ===== 1. Tiến trình =====

export function ProcessesModule({ agent }) {
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

// ===== 2. Live Screen / Webcam =====

export function LiveViewModule({ agent }) {
  const [source, setSource] = useState('screen');
  const [frame, setFrame] = useState(null);
  const [paused, setPaused] = useState(false);
  const [fps, setFps] = useState(0);
  const canvasRef = useRef(null);
  const frameCountRef = useRef(0);
  const pausedRef = useRef(false);
  pausedRef.current = paused;
  const online = agent.status === 'online';

  const { status, send, cancel } = useModule(agent.id, 'live', {
    onData: (msg) => {
      if (msg.type === 'frame') {
        if (pausedRef.current) return;
        frameCountRef.current += 1;
        setFrame(msg);
      }
    },
  });

  useEffect(() => {
    const timer = setInterval(() => {
      setFps(frameCountRef.current);
      frameCountRef.current = 0;
    }, 1000);
    return () => clearInterval(timer);
  }, []);

  useEffect(() => {
    if (!frame) return;
    const canvas = canvasRef.current;
    if (!canvas) return;
    const ctx = canvas.getContext('2d');
    const imgData = new ImageData(new Uint8ClampedArray(frame.data), frame.width, frame.height);
    if (canvas.width !== frame.width) canvas.width = frame.width;
    if (canvas.height !== frame.height) canvas.height = frame.height;
    ctx.putImageData(imgData, 0, 0);
  }, [frame]);

  const start = useCallback((src) => send('start', { source: src }), [send]);
  const stop = () => send('stop', {}, { consent: false });

  const switchSource = (src) => {
    if (src === source) return;
    setSource(src);
    setFrame(null);
    if (status === 'active') {
      send('stop', {}, { consent: false });
      send('start', { source: src });
    }
  };

  if (status === 'idle') {
    return (
      <div className="module">
        <IdleState
          title="Live Screen / Webcam"
          description="Phát trực tiếp màn hình hoặc camera của máy đích. Video được vẽ liên tục lên canvas từ dữ liệu khung hình gửi về."
          actionLabel="Bắt đầu xem màn hình"
          onAction={() => start('screen')}
          disabled={!online}
        />
      </div>
    );
  }

  return (
    <div className="module">
      <ConsentOverlay
        visible={status === 'consent'}
        subtitle={`Yêu cầu quyền bật ${source === 'screen' ? 'màn hình' : 'webcam'} trên ${agent.name}`}
        onCancel={cancel}
      />
      {status === 'rejected' && (
        <div className="module">
          <RefusedState onRetry={() => start(source)} />
        </div>
      )}
      {status === 'timeout' && (
        <div className="module">
          <TimeoutState onRetry={() => start(source)} />
        </div>
      )}
      {status === 'active' && (
        <>
          <div className="toolbar">
            <div className="seg">
              <button className={`seg-btn ${source === 'screen' ? 'active' : ''}`} onClick={() => switchSource('screen')} type="button">
                Màn hình
              </button>
              <button className={`seg-btn ${source === 'webcam' ? 'active' : ''}`} onClick={() => switchSource('webcam')} type="button">
                Webcam
              </button>
            </div>
            <span className="live-badge">LIVE {fps} fps</span>
            <span className="toolbar-spacer" />
            <button className="btn btn-ghost" onClick={() => setPaused((p) => !p)} type="button">
              {paused ? 'Tiếp tục' : 'Tạm dừng'}
            </button>
            <button className="btn btn-ghost" onClick={stop} type="button">
              Dừng
            </button>
          </div>
          <div className={`video-frame ${paused ? 'paused' : ''}`}>
            <canvas ref={canvasRef} className="video-canvas" />
            {!frame && <div className="video-loading">Đang nhận khung hình đầu tiên...</div>}
            <div className="video-overlay-label">{source === 'screen' ? 'SCREEN' : 'WEBCAM'} · {agent.name}</div>
          </div>
        </>
      )}
    </div>
  );
}

// ===== 3. File Download =====

function TreeNode({ node, depth, expanded, onToggle, selectedPath, onSelect }) {
  const isDir = node.type === 'dir';
  const isOpen = expanded.has(node.path);
  return (
    <div>
      <div
        className={`tree-row ${selectedPath === node.path ? 'selected' : ''}`}
        style={{ paddingLeft: `${12 + depth * 18}px` }}
        onClick={() => (isDir ? onToggle(node.path) : onSelect(node))}
      >
        <span className="tree-twist">{isDir ? (isOpen ? 'v' : '>') : ''}</span>
        <span className={`tree-icon ${isDir ? 'dir' : 'file'}`} />
        <span className="tree-name">{node.name}</span>
        {!isDir && <span className="tree-size">{formatBytes(node.size)}</span>}
      </div>
      {isDir && isOpen && node.children
        ? node.children.map((c) => (
            <TreeNode
              key={c.path}
              node={c}
              depth={depth + 1}
              expanded={expanded}
              onToggle={onToggle}
              selectedPath={selectedPath}
              onSelect={onSelect}
            />
          ))
        : null}
    </div>
  );
}

export function FileManagerModule({ agent }) {
  const [tree, setTree] = useState(null);
  const [expanded, setExpanded] = useState(new Set());
  const [selected, setSelected] = useState(null);
  const [downloading, setDownloading] = useState(false);
  const online = agent.status === 'online';

  const { status, send, cancel } = useModule(agent.id, 'files', {
    onData: (msg) => {
      if (msg.type === 'tree') {
        setTree(msg.tree);
        setExpanded((cur) => new Set(cur).add(msg.tree.path));
      }
      if (msg.type === 'file') {
        setDownloading(false);
        downloadBlob(msg.name, msg.data, msg.mime);
      }
    },
  });

  const load = () => send('list', {});
  const stop = () => send('stop', {}, { consent: false });

  const toggle = (p) => {
    setExpanded((cur) => {
      const next = new Set(cur);
      if (next.has(p)) next.delete(p);
      else next.add(p);
      return next;
    });
  };

  const download = useCallback(() => {
    if (!selected || downloading) return;
    setDownloading(true);
    send('download', { path: selected.path });
  }, [selected, downloading, send]);

  if (status === 'idle') {
    return (
      <div className="module">
        <IdleState
          title="File Download"
          description="Duyệt cây thư mục của máy đích giống Windows Explorer. Chọn một file rồi bấm Download để tải về máy của bạn."
          actionLabel="Tải cây thư mục"
          onAction={load}
          disabled={!online}
        />
      </div>
    );
  }

  return (
    <div className="module">
      <ConsentOverlay
        visible={status === 'consent'}
        subtitle={`Yêu cầu quyền truy cập file trên ${agent.name}`}
        onCancel={cancel}
      />
      {status === 'rejected' && (
        <div className="module">
          <RefusedState onRetry={load} />
        </div>
      )}
      {status === 'timeout' && (
        <div className="module">
          <TimeoutState onRetry={load} />
        </div>
      )}
      {status === 'active' && tree && (
        <>
          <div className="toolbar">
            <span className="live-badge">Cây thư mục của {agent.name}</span>
            <span className="toolbar-spacer" />
            <button className="btn btn-ghost" onClick={stop} type="button">
              Đóng
            </button>
          </div>
          <div className="file-panel">
            <div className="tree-pane">
              <TreeNode node={tree} depth={0} expanded={expanded} onToggle={toggle} selectedPath={selected?.path} onSelect={setSelected} />
            </div>
            <div className="file-detail">
              {selected ? (
                <>
                  <div className="file-detail-name">{selected.name}</div>
                  <div className="file-detail-path mono">{selected.path}</div>
                  <div className="file-detail-meta">Kích thước: {formatBytes(selected.size)}</div>
                  <button className="btn btn-primary" onClick={download} disabled={downloading} type="button">
                    {downloading ? 'Đang xin quyền tải...' : 'Download file'}
                  </button>
                </>
              ) : (
                <div className="file-detail-empty">Chọn một file trong cây bên trái để tải về.</div>
              )}
            </div>
          </div>
        </>
      )}
    </div>
  );
}

// ===== 4. Keylogger =====

export function KeyloggerModule({ agent }) {
  const [text, setText] = useState('');
  const [captured, setCaptured] = useState(false);
  const textareaRef = useRef(null);
  const online = agent.status === 'online';

  const { status, send, cancel } = useModule(agent.id, 'keylogger', {
    onData: (msg) => {
      if (msg.type === 'keys') {
        setCaptured(true);
        setText((t) => t + msg.keys);
      }
    },
  });

  useEffect(() => {
    const el = textareaRef.current;
    if (el) el.scrollTop = el.scrollHeight;
  }, [text]);

  const start = () => {
    setText('');
    setCaptured(false);
    send('start', {});
  };
  const stop = () => send('stop', {}, { consent: false });

  if (status === 'idle') {
    return (
      <div className="module">
        <IdleState
          title="Keylogger"
          description="Ghi lại các phím được gõ trên máy đích sau khi được cấp quyền. Các phím mới sẽ tự động nối vào cuối khung text."
          actionLabel="Bắt đầu ghi phím"
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
        subtitle={`Yêu cầu quyền ghi phím trên ${agent.name}`}
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
            <span className="live-badge">Đang ghi phím · {captured ? 'đã bắt được phím' : 'chưa có phím mới'}</span>
            <span className="toolbar-spacer" />
            <button className="btn btn-ghost" onClick={stop} type="button">
              Dừng ghi
            </button>
          </div>
          <textarea
            ref={textareaRef}
            className="keylog-area mono"
            value={text}
            readOnly
            placeholder="Các phím gõ được sẽ hiện tại đây..."
          />
          <div className="keylog-count mono">Số ký tự đã ghi: {text.length}</div>
        </>
      )}
    </div>
  );
}

// ===== 5. Power Control =====

const POWER_ACTIONS = [
  { action: 'sleep', label: 'Sleep', desc: 'Đưa máy đích về trạng thái ngủ', danger: false },
  { action: 'restart', label: 'Restart', desc: 'Khởi động lại máy đích', danger: true },
  { action: 'shutdown', label: 'Shutdown', desc: 'Tắt máy đích hoàn toàn', danger: true },
];

export function PowerModule({ agent }) {
  const [confirming, setConfirming] = useState(null);
  const [executed, setExecuted] = useState(null);
  const online = agent.status === 'online';

  const { status, send, cancel } = useModule(agent.id, 'power', {
    onData: (msg) => {
      if (msg.type === 'status' && msg.status === 'executed') setExecuted(msg.message);
    },
  });

  const runAction = (a) => {
    setConfirming(null);
    setExecuted(null);
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
        {POWER_ACTIONS.map((a) => (
          <div key={a.action} className={`power-card ${a.danger ? 'danger' : ''}`}>
            <div className="power-label">{a.label}</div>
            <div className="power-desc">{a.desc}</div>
            <button
              className={`btn ${a.danger ? 'btn-danger' : 'btn-primary'}`}
              onClick={() => setConfirming(a)}
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
              Bạn có chắc muốn {confirming.desc} (<span className="mono">{agent.name}</span>)? Thao tác này cần người dùng trên
              máy đích đồng ý.
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

// ===== 6. Shell =====

export function ShellModule({ agent }) {
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

// ===== 7. Nhắn tin =====

export function MessageModule({ agent }) {
  const [message, setMessage] = useState('');
  const [sentText, setSentText] = useState('');
  const online = agent.status === 'online';

  const { status, send, cancel } = useModule(agent.id, 'message', {
    onData: (msg) => {
      if (msg.type === 'status' && msg.status === 'sent') {
        setSentText(msg.message);
        setMessage('');
      }
    },
  });

  const sendMessage = () => {
    const text = message.trim();
    if (!text || !online) return;
    setSentText('');
    send('send', { message: text });
  };

  const sending = status === 'consent' || status === 'active';

  return (
    <div className="module">
      <ConsentOverlay
        visible={status === 'consent'}
        subtitle={`Yêu cầu quyền hiển thị tin nhắn trên ${agent.name}`}
        onCancel={cancel}
      />
      {status === 'rejected' && (
        <div className="module">
          <RefusedState message="Người dùng máy đích đã từ chối hiển thị tin nhắn." onRetry={() => send('send', { message })} />
        </div>
      )}
      {status === 'timeout' && (
        <div className="module">
          <TimeoutState onRetry={() => send('send', { message })} />
        </div>
      )}
      <div className="message-panel">
        {sentText && <div className="module-flag success">{sentText}</div>}
        {!online && <div className="module-flag error">Agent đang offline — không thể gửi tin nhắn.</div>}
        <label className="field-label">Nội dung tin nhắn</label>
        <textarea
          className="message-input"
          rows={4}
          value={message}
          onChange={(e) => setMessage(e.target.value)}
          placeholder="Nhập tin nhắn muốn hiển thị trên máy đích..."
        />
        <button className="btn btn-primary" onClick={sendMessage} disabled={!message.trim() || sending || !online} type="button">
          {sending ? 'Đang gửi...' : 'Gửi tin nhắn'}
        </button>
      </div>
    </div>
  );
}
