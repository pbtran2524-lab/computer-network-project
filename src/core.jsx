import { createContext, useCallback, useContext, useEffect, useMemo, useRef, useState } from 'react';
import { io } from 'socket.io-client';

// ===== Socket =====

export const socket = io(import.meta.env.VITE_SERVER_URL || 'http://localhost:4000', {
  autoConnect: true,
  transports: ['websocket', 'polling'],
});

// ===== Utils =====

export function genId() {
  return `${Date.now().toString(36)}-${Math.random().toString(36).slice(2, 9)}`;
}

export const MODULE_LABELS = {
  processes: 'Tiến trình',
  live: 'Live View',
  files: 'File Download',
  keylogger: 'Keylogger',
  power: 'Power Control',
  shell: 'Shell',
  message: 'Nhắn tin',
};

export function formatBytes(n) {
  if (!Number.isFinite(n)) return '-';
  if (n < 1024) return `${n} B`;
  if (n < 1024 * 1024) return `${(n / 1024).toFixed(1)} KB`;
  if (n < 1024 * 1024 * 1024) return `${(n / 1024 / 1024).toFixed(1)} MB`;
  return `${(n / 1024 / 1024 / 1024).toFixed(2)} GB`;
}

export function formatRam(mb) {
  if (mb < 1024) return `${Math.round(mb)} MB`;
  return `${(mb / 1024).toFixed(2)} GB`;
}

function base64ToBytes(b64) {
  const bin = atob(b64);
  const bytes = new Uint8Array(bin.length);
  for (let i = 0; i < bin.length; i++) bytes[i] = bin.charCodeAt(i);
  return bytes;
}

export function downloadBlob(name, base64, mime = 'application/octet-stream') {
  const blob = new Blob([base64ToBytes(base64)], { type: mime });
  const url = URL.createObjectURL(blob);
  const a = document.createElement('a');
  a.href = url;
  a.download = name;
  document.body.appendChild(a);
  a.click();
  a.remove();
  setTimeout(() => URL.revokeObjectURL(url), 1000);
}

// ===== AgentContext =====

const AgentContext = createContext(null);

export const useAgent = () => useContext(AgentContext);

export function AgentProvider({ children }) {
  const [connected, setConnected] = useState(socket.connected);
  const [agents, setAgents] = useState([]);
  const [activeSession, setActiveSession] = useState(null);
  const [toasts, setToasts] = useState([]);
  const handlersRef = useRef(new Map());
  const agentsRef = useRef([]);
  const activeRef = useRef(null);

  agentsRef.current = agents;
  activeRef.current = activeSession;

  const dismissToast = useCallback((id) => {
    setToasts((cur) => cur.filter((t) => t.id !== id));
  }, []);

  const showToast = useCallback((type, message) => {
    const id = Date.now() + Math.random();
    setToasts((cur) => [...cur, { id, type, message }]);
    setTimeout(() => dismissToast(id), 5200);
  }, [dismissToast]);

  useEffect(() => {
    const on = (ev, fn) => socket.on(ev, fn);
    on('connect', () => setConnected(true));
    on('disconnect', () => setConnected(false));
    on('agents:list', (list) => setAgents(list));
    on('agent:status', (a) => {
      setAgents((cur) => cur.map((x) => (x.id === a.id ? { ...x, status: a.status } : x)));
      const agent = agentsRef.current.find((x) => x.id === a.id);
      if (activeRef.current === a.id) {
        showToast(a.status === 'offline' ? 'warn' : 'success', `Agent ${agent ? agent.name : a.id}: ${a.status === 'online' ? 'trở lại trực tuyến' : 'đã ngắt kết nối'}`);
      }
    });
    on('session:started', ({ agentId }) => setActiveSession(agentId));
    on('error:message', ({ message }) => showToast('error', message));

    const fire = (ev, msg) => {
      const handler = handlersRef.current.get(msg.requestId);
      const fn = handler && handler[ev];
      if (fn) fn(msg);
    };

    on('command:consent', (msg) => fire('onConsent', msg));
    on('command:accepted', (msg) => {
      fire('onAccepted', msg);
      const agent = agentsRef.current.find((x) => x.id === msg.agentId);
      showToast('success', `${agent ? agent.name : msg.agentId}: đã đồng ý cấp quyền (${MODULE_LABELS[msg.module] || msg.module})`);
    });
    on('command:rejected', (msg) => {
      fire('onRejected', msg);
      const agent = agentsRef.current.find((x) => x.id === msg.agentId);
      const label = MODULE_LABELS[msg.module] || msg.module;
      showToast('error', msg.reason === 'CANCELLED'
        ? `Đã hủy yêu cầu ${label}`
        : `${agent ? agent.name : msg.agentId}: người dùng đã từ chối (${label})`);
    });
    on('command:timeout', (msg) => {
      fire('onTimeout', msg);
      const agent = agentsRef.current.find((x) => x.id === msg.agentId);
      showToast('error', `${agent ? agent.name : msg.agentId}: hết thời gian chờ phản hồi — yêu cầu bị hủy`);
    });
    on('command:data', (msg) => fire('onData', msg));
    on('command:end', (msg) => fire('onEnd', msg));
    on('command:error', (msg) => {
      fire('onError', msg);
      showToast('error', msg.message || 'Lỗi thực thi lệnh');
    });

    return () => {
      socket.off();
    };
  }, [showToast]);

  const sendCommand = useCallback((agentId, moduleName, action, payload, requestId) => {
    socket.emit('command', { requestId, agentId, module: moduleName, action, payload });
    return requestId;
  }, []);

  const registerHandler = useCallback((requestId, handlers) => {
    handlersRef.current.set(requestId, handlers);
    return () => handlersRef.current.delete(requestId);
  }, []);

  const connectAgent = useCallback((agentId) => {
    socket.emit('session:start', { agentId });
  }, []);

  const disconnectSession = useCallback(() => {
    if (activeRef.current) socket.emit('session:end', { agentId: activeRef.current });
    setActiveSession(null);
  }, []);

  const refreshAgents = useCallback(() => {
    socket.emit('agents:get');
  }, []);

  const value = useMemo(
    () => ({
      connected,
      agents,
      activeSession,
      toasts,
      showToast,
      dismissToast,
      sendCommand,
      registerHandler,
      connectAgent,
      disconnectSession,
      refreshAgents,
    }),
    [connected, agents, activeSession, toasts, showToast, dismissToast, sendCommand, registerHandler, connectAgent, disconnectSession, refreshAgents]
  );

  return <AgentContext.Provider value={value}>{children}</AgentContext.Provider>;
}

// ===== useCommand =====

export function useCommand() {
  const { sendCommand, registerHandler } = useAgent();
  const run = useCallback((agentId, moduleName, action, payload, handlers, opts) => {
    const requestId = (opts && opts.requestId) || genId();
    const cleanup = registerHandler(requestId, handlers || {});
    sendCommand(agentId, moduleName, action, payload, requestId);
    return { requestId, cleanup };
  }, [registerHandler, sendCommand]);
  return { run };
}

// ===== useModule =====

export function useModule(agentId, moduleName, { onData, onAccepted, onEnd } = {}) {
  const { run } = useCommand();
  const [status, setStatus] = useState('idle');
  const activeRef = useRef(null);
  const cleanupsRef = useRef(new Map());
  const cbRef = useRef({ onData, onAccepted, onEnd });
  cbRef.current = { onData, onAccepted, onEnd };

  const clearActive = useCallback(() => {
    const cleanup = cleanupsRef.current.get(activeRef.current);
    if (cleanup) {
      cleanup();
      cleanupsRef.current.delete(activeRef.current);
    }
    activeRef.current = null;
  }, []);

  const send = useCallback((action, payload, opts) => {
    clearActive();
    const requestId = genId();
    activeRef.current = requestId;
    const cleanup = run(
      agentId,
      moduleName,
      action,
      payload,
      {
        onConsent: () => setStatus('consent'),
        onAccepted: (msg) => {
          setStatus('active');
          cbRef.current.onAccepted?.(msg);
        },
        onRejected: () => {
          if (activeRef.current === requestId) {
            setStatus('rejected');
            activeRef.current = null;
          }
        },
        onTimeout: () => {
          if (activeRef.current === requestId) {
            setStatus('timeout');
            activeRef.current = null;
          }
        },
        onError: () => {
          if (activeRef.current === requestId) {
            setStatus('idle');
            activeRef.current = null;
          }
        },
        onData: (msg) => cbRef.current.onData?.(msg),
        onEnd: (msg) => {
          if (activeRef.current === requestId) {
            setStatus('idle');
            activeRef.current = null;
          }
        },
      },
      { requestId }
    );
    cleanupsRef.current.set(requestId, cleanup);
    if (!(opts && opts.consent === false)) setStatus('consent');
    return { requestId };
  }, [run, agentId, moduleName, clearActive]);

  const cancel = useCallback(() => {
    const rid = activeRef.current;
    if (!rid) return;
    run(agentId, moduleName, 'cancel', { requestId: rid }, {}, { requestId: genId() });
    clearActive();
    setStatus('idle');
  }, [run, agentId, moduleName, clearActive]);

  useEffect(() => () => {
    cleanupsRef.current.forEach((c) => c());
    cleanupsRef.current.clear();
  }, []);

  return { status, send, cancel, reset: () => setStatus('idle') };
}
