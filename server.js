import path from 'node:path';
import fs from 'node:fs';
import http from 'node:http';
import { fileURLToPath } from 'node:url';
import express from 'express';
import { Server } from 'socket.io';

let __dirname = process.cwd();
try {
  __dirname = path.dirname(fileURLToPath(import.meta.url));
} catch {
  // bundled CJS: import.meta unavailable, fall back to process.cwd()
}
const PORT = process.env.PORT || 4000;
const CONSENT_TIMEOUT_MS = 8000;
const NEEDS_CONSENT = {
  processes: true,
  live: true,
  files: true,
  keylogger: true,
  power: true,
  shell: true,
  message: true,
};
const FRAME_DIMS = {
  screen: { w: 480, h: 270, rate: 6 },
  webcam: { w: 320, h: 240, rate: 4 },
};

const pendingConsents = new Map();
const shellSessions = new Set();
const streams = new Map();

function delay(ms) {
  return new Promise((r) => setTimeout(r, ms));
}

// ===== Mock agents =====

const AGENTS = [
  { id: 'pc1', name: 'WIN-PC1', ip: '192.168.1.10', os: 'Windows 10 Pro', status: 'online' },
  { id: 'pc2', name: 'OFFICE-DESKTOP', ip: '192.168.1.22', os: 'Windows 11 Enterprise', status: 'online' },
  { id: 'pc3', name: 'MACBOOK-THANH', ip: '192.168.1.31', os: 'macOS 14 Sonoma', status: 'online' },
  { id: 'pc4', name: 'UBUNTU-SRV', ip: '192.168.1.45', os: 'Ubuntu 22.04 LTS', status: 'online' },
  { id: 'pc5', name: 'OLD-PC', ip: '192.168.1.50', os: 'Windows 7 Ultimate', status: 'offline' },
];

const consentModes = Object.fromEntries(AGENTS.map((a) => [a.id, 'accept']));

const getAgents = () => AGENTS.map((a) => ({ ...a }));
const getAgent = (id) => AGENTS.find((a) => a.id === id);
const getConsentMode = (id) => consentModes[id] || 'accept';
const setConsentMode = (id, mode) => { consentModes[id] = mode; };
const setAgentStatus = (id, status) => { const a = getAgent(id); if (a) a.status = status; };

const PROCESS_SPECS = [
  { name: 'chrome.exe', pid: 8412, base: 20 },
  { name: 'explorer.exe', pid: 4132, base: 2 },
  { name: 'node.exe', pid: 6924, base: 6 },
  { name: 'python.exe', pid: 5510, base: 4 },
  { name: 'Discord.exe', pid: 3840, base: 3 },
  { name: 'Teams.exe', pid: 7712, base: 5 },
  { name: 'winword.exe', pid: 9233, base: 1 },
  { name: 'svchost.exe', pid: 1184, base: 0 },
  { name: 'svchost.exe', pid: 2204, base: 0 },
  { name: 'dwm.exe', pid: 3008, base: 1 },
  { name: 'csrss.exe', pid: 456, base: 0 },
  { name: 'OneDrive.exe', pid: 6120, base: 2 },
  { name: 'Spotify.exe', pid: 5521, base: 2 },
  { name: 'Code.exe', pid: 7340, base: 4 },
  { name: 'firefox.exe', pid: 8877, base: 3 },
];

const generateProcesses = () =>
  PROCESS_SPECS.map((p) => ({
    name: p.name,
    pid: p.pid,
    cpu: Math.round((Math.random() * 8 + p.base) * 10) / 10,
    ram: Math.round(Math.random() * 1300 + 60),
  }));

function generateFrame(width, height, t, source) {
  const buf = new Uint8Array(width * height * 4);
  for (let y = 0; y < height; y++) {
    for (let x = 0; x < width; x++) {
      const i = (y * width + x) * 4;
      const u = x / width;
      const v = y / height;
      let r;
      let g;
      let b;
      if (source === 'webcam') {
        r = 128 + 80 * Math.sin(u * 3 + v * 2 + t * 0.02);
        g = 105 + 55 * Math.cos(v * 4 - u * 2 + t * 0.015);
        b = 150 + 60 * Math.sin(u * 2.5 + v * 3 + t * 0.02);
      } else {
        r = 35 + 85 * u + 30 * Math.sin(u * 6 + t * 0.05);
        g = 45 + 65 * v + 25 * Math.cos(v * 6 - t * 0.04);
        b = 95 + 75 * Math.sin(u * 3 + v * 3 + t * 0.03);
      }
      const cx = 0.5 + 0.4 * Math.sin(t * 0.1);
      const cy = 0.5 + 0.35 * Math.cos(t * 0.07);
      if (Math.hypot(u - cx, v - cy) < 0.02) {
        r = 255;
        g = 255;
        b = 255;
      }
      const n = (Math.random() - 0.5) * 18;
      buf[i] = Math.max(0, Math.min(255, r + n));
      buf[i + 1] = Math.max(0, Math.min(255, g + n));
      buf[i + 2] = Math.max(0, Math.min(255, b + n));
      buf[i + 3] = 255;
    }
  }
  return { width, height, data: buf.buffer };
}

const mkDir = (name, p, children) => ({ name, type: 'dir', path: p, children: children || [] });
const mkFile = (name, p, size) => ({ name, type: 'file', path: p, size });

function buildFileTree() {
  return mkDir('C:\\', 'C:\\', [
    mkDir('Program Files', 'C:\\Program Files', [
      mkDir('Google', 'C:\\Program Files\\Google', [
        mkDir('Chrome', 'C:\\Program Files\\Google\\Chrome', [
          mkDir('Application', 'C:\\Program Files\\Google\\Chrome\\Application', [
            mkFile('chrome.exe', 'C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe', 188 * 1024 * 1024),
          ]),
        ]),
      ]),
      mkDir('Mozilla Firefox', 'C:\\Program Files\\Mozilla Firefox', [
        mkFile('firefox.exe', 'C:\\Program Files\\Mozilla Firefox\\firefox.exe', 95200000),
      ]),
    ]),
    mkDir('Users', 'C:\\Users', [
      mkDir('Admin', 'C:\\Users\\Admin', [
        mkDir('Desktop', 'C:\\Users\\Admin\\Desktop', [
          mkFile('quy_che_bao_mat.docx', 'C:\\Users\\Admin\\Desktop\\quy_che_bao_mat.docx', 48000),
          mkFile('ke_hoach_2026.xlsx', 'C:\\Users\\Admin\\Desktop\\ke_hoach_2026.xlsx', 120000),
          mkFile('meo_ghi_chu.txt', 'C:\\Users\\Admin\\Desktop\\meo_ghi_chu.txt', 2048),
        ]),
        mkDir('Documents', 'C:\\Users\\Admin\\Documents', [
          mkFile('hop_dong_khach_hang.pdf', 'C:\\Users\\Admin\\Documents\\hop_dong_khach_hang.pdf', 245000),
          mkFile('mat_khau_notes.txt', 'C:\\Users\\Admin\\Documents\\mat_khau_notes.txt', 1800),
          mkDir('DuAn', 'C:\\Users\\Admin\\Documents\\DuAn', [
            mkFile('briefing_2026.pptx', 'C:\\Users\\Admin\\Documents\\DuAn\\briefing_2026.pptx', 890000),
            mkFile('README.md', 'C:\\Users\\Admin\\Documents\\DuAn\\README.md', 3100),
          ]),
        ]),
        mkDir('Downloads', 'C:\\Users\\Admin\\Downloads', [
          mkFile('setup-remote.exe', 'C:\\Users\\Admin\\Downloads\\setup-remote.exe', 55200000),
          mkFile('bao_cao_thang7.zip', 'C:\\Users\\Admin\\Downloads\\bao_cao_thang7.zip', 3200000),
        ]),
        mkDir('Pictures', 'C:\\Users\\Admin\\Pictures', [
          mkFile('anh_dai_dien.jpg', 'C:\\Users\\Admin\\Pictures\\anh_dai_dien.jpg', 1250000),
        ]),
      ]),
    ]),
    mkDir('Windows', 'C:\\Windows', [
      mkDir('System32', 'C:\\Windows\\System32', [
        mkFile('kernel32.dll', 'C:\\Windows\\System32\\kernel32.dll', 6240000),
        mkFile('cmd.exe', 'C:\\Windows\\System32\\cmd.exe', 310000),
      ]),
      mkDir('Temp', 'C:\\Windows\\Temp', [mkFile('log_tmp.tmp', 'C:\\Windows\\Temp\\log_tmp.tmp', 900)]),
    ]),
    mkFile('pagefile.sys', 'C:\\pagefile.sys', 4 * 1024 * 1024 * 1024),
  ]);
}

function findFileByPath(root, p) {
  const stack = [root];
  while (stack.length) {
    const node = stack.pop();
    if (node.path === p && node.type === 'file') return node;
    if (node.children) stack.push(...node.children);
  }
  return null;
}

function fileContent(file) {
  let s = '';
  for (let i = 1; i <= 40; i++) s += `[Mock file] ${file.path}\n[${i}] Dòng dữ liệu giả lập cho file "${file.name}".\n`;
  return Buffer.from(s, 'utf8').toString('base64');
}

function runShellCommand(cmd) {
  const c = (cmd || '').trim();
  const cl = c.toLowerCase();
  if (!c) return '\r\n';
  if (cl === 'dir' || cl === 'ls') {
    return [
      '\r\n Volume in drive C is SYSTEM',
      ' Volume Serial Number is ABCD-1234',
      '',
      ' Directory of C:\\Users\\Admin',
      '',
      '<DIR>          .',
      '<DIR>          ..',
      '<DIR>          Desktop',
      '<DIR>          Documents',
      '<DIR>          Downloads',
      '<DIR>          Pictures',
      '              1 File(s)             12.345 bytes',
      '              5 Dir(s)  123.456.789 bytes free',
    ].join('\r\n');
  }
  if (cl === 'ipconfig') {
    return [
      '\r\nWindows IP Configuration',
      '',
      'Ethernet adapter Ethernet0:',
      '   Connection-specific DNS Suffix  . : local',
      '   IPv4 Address. . . . . . . . . . . : 192.168.1.10',
      '   Subnet Mask . . . . . . . . . . . : 255.255.255.0',
      '   Default Gateway . . . . . . . . . : 192.168.1.1',
    ].join('\r\n');
  }
  if (cl === 'whoami') return 'desktop-abc123\\admin';
  if (cl === 'hostname') return 'WIN-PC1';
  if (cl === 'tasklist') {
    return [
      '',
      'Image Name                     PID  Session Name  Mem Usage',
      '========================= ======== ================ ============',
      'chrome.exe                    8412  Console           412,4 MB',
      'explorer.exe                  4132  Console            91,2 MB',
      'node.exe                      6924  Console            78,5 MB',
      'Teams.exe                     7712  Console           204,8 MB',
    ].join('\r\n');
  }
  if (cl.startsWith('echo ')) return c.slice(5);
  if (cl.startsWith('cd ')) return `\r\nThư mục hiện tại: ${c.slice(3)} (mock)`;
  if (cl === 'help') {
    return 'Lệnh giả lập: dir, ls, ipconfig, whoami, hostname, tasklist, echo <text>, cd <path>, help';
  }
  return `\r\n> ${c}\r\n(Mock shell) Không nhận dạng lệnh "${c}".\r\nGõ "help" để xem danh sách lệnh giả lập.`;
}

const CHARS = 'abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 .,!?';
function generateKeys() {
  const n = 1 + Math.floor(Math.random() * 5);
  let out = '';
  for (let i = 0; i < n; i++) out += CHARS[Math.floor(Math.random() * CHARS.length)];
  const r = Math.random();
  if (r < 0.04) out = '\n[ENTER]\n';
  else if (r < 0.06) out = ' [TAB] ';
  else if (r < 0.08) out = ' [BACKSPACE] ';
  return out;
}

const POWER_PLAN = {
  sleep: { label: 'Sleep', offlineMs: 8000 },
  restart: { label: 'Restart', offlineMs: 12000 },
  shutdown: { label: 'Shutdown', offlineMs: 45000 },
};

const messageText = (text) => `[Mock] Đã gửi tin nhắn "${text}" tới máy đích.`;

// ===== Consent flow =====

const pendingByAgentModule = new Map();

function dropPending(requestId) {
  const pc = pendingConsents.get(requestId);
  if (!pc) return;
  clearTimeout(pc.timer);
  pendingConsents.delete(requestId);
  const key = `${pc.meta.agentId}:${pc.meta.module}`;
  if (pendingByAgentModule.get(key) === requestId) pendingByAgentModule.delete(key);
}

function scheduleDecision(requestId) {
  const pc = pendingConsents.get(requestId);
  if (!pc) return;
  const { socket, meta, resolve } = pc;
  const mode = getConsentMode(meta.agentId);
  const emit = (event, extra) => socket.emit(event, { ...meta, ...extra });
  if (mode === 'reject') {
    setTimeout(() => {
      if (!pendingConsents.has(requestId)) return;
      dropPending(requestId);
      emit('command:rejected', { reason: 'USER_DECLINED' });
      resolve('rejected');
    }, 1200 + Math.random() * 800);
  } else if (mode === 'timeout') {
    return;
  } else if (mode === 'random') {
    setTimeout(() => {
      if (!pendingConsents.has(requestId)) return;
      const r = Math.random();
      if (r < 0.55) {
        dropPending(requestId);
        emit('command:accepted');
        resolve('accepted');
      } else if (r < 0.78) {
        dropPending(requestId);
        emit('command:rejected', { reason: 'USER_DECLINED' });
        resolve('rejected');
      }
    }, 1200 + Math.random() * 2000);
  } else {
    setTimeout(() => {
      if (!pendingConsents.has(requestId)) return;
      dropPending(requestId);
      emit('command:accepted');
      resolve('accepted');
    }, 900 + Math.random() * 1200);
  }
}

function requestConsent(socket, meta) {
  return new Promise((resolve) => {
    socket.emit('command:consent', meta);
    const timer = setTimeout(() => {
      if (!pendingConsents.has(meta.requestId)) return;
      dropPending(meta.requestId);
      socket.emit('command:timeout', meta);
      resolve('timeout');
    }, CONSENT_TIMEOUT_MS);
    pendingConsents.set(meta.requestId, { socket, meta, resolve, timer });
    pendingByAgentModule.set(`${meta.agentId}:${meta.module}`, meta.requestId);
    scheduleDecision(meta.requestId);
  });
}

function cancelConsent(requestId) {
  const pc = pendingConsents.get(requestId);
  if (!pc) return;
  dropPending(requestId);
  pc.socket.emit('command:rejected', { ...pc.meta, reason: 'CANCELLED' });
  pc.resolve('rejected');
}

function cancelPendingForAgentModule(agentId, moduleName) {
  const rid = pendingByAgentModule.get(`${agentId}:${moduleName}`);
  if (rid) cancelConsent(rid);
}

// ===== Streams & command handlers =====

function stopStream(agentId, moduleName) {
  cancelPendingForAgentModule(agentId, moduleName);
  const key = `${agentId}:${moduleName}`;
  const s = streams.get(key);
  if (s) {
    clearInterval(s.timer);
    streams.delete(key);
  }
}

function stopStreamsForAgent(agentId) {
  for (const [key, rid] of [...pendingByAgentModule]) {
    if (key.startsWith(`${agentId}:`)) cancelConsent(rid);
  }
  for (const key of [...streams.keys()]) {
    if (key.startsWith(`${agentId}:`)) {
      clearInterval(streams.get(key).timer);
      streams.delete(key);
    }
  }
}

function startProcessStream(socket, agentId, requestId) {
  stopStream(agentId, 'processes');
  const push = () => socket.emit('command:data', { requestId, type: 'processes', rows: generateProcesses() });
  push();
  streams.set(`${agentId}:processes`, { timer: setInterval(push, 2000), socket });
}

function startFrameStream(socket, agentId, requestId, source) {
  const { w, h, rate } = FRAME_DIMS[source] || FRAME_DIMS.screen;
  stopStream(agentId, 'live');
  let t = 0;
  const push = () => {
    t++;
    const frame = generateFrame(w, h, t, source);
    socket.emit('command:data', {
      requestId,
      type: 'frame',
      source,
      width: frame.width,
      height: frame.height,
      data: frame.data,
    });
  };
  push();
  streams.set(`${agentId}:live`, { timer: setInterval(push, 1000 / rate), socket });
}

function startKeyloggerStream(socket, agentId, requestId) {
  stopStream(agentId, 'keylogger');
  const push = () => socket.emit('command:data', { requestId, type: 'keys', keys: generateKeys() });
  push();
  streams.set(`${agentId}:keylogger`, { timer: setInterval(push, 650 + Math.random() * 900), socket });
}

async function handleFiles(socket, meta, payload) {
  const { requestId } = meta;
  if (meta.action === 'download') {
    const file = findFileByPath(buildFileTree(), payload.path);
    await delay(500);
    if (!file) {
      socket.emit('command:error', { requestId, message: 'Không tìm thấy file trên máy đích' });
      return;
    }
    socket.emit('command:data', {
      requestId,
      type: 'file',
      name: file.name,
      path: file.path,
      size: file.size,
      mime: 'application/octet-stream',
      data: fileContent(file),
    });
    socket.emit('command:end', { requestId });
    return;
  }
  socket.emit('command:data', { requestId, type: 'tree', tree: buildFileTree() });
  socket.emit('command:end', { requestId });
}

function handleShell(socket, meta, payload) {
  const { requestId } = meta;
  const cmd = payload.cmd || '';
  socket.emit('command:data', { requestId, type: 'output', cmd, text: runShellCommand(cmd) });
  socket.emit('command:end', { requestId });
}

function handleMessage(socket, meta, payload) {
  const { requestId } = meta;
  const text = payload.message || '';
  socket.emit('command:data', { requestId, type: 'status', status: 'sent', message: messageText(text) });
  socket.emit('command:end', { requestId });
}

async function handlePower(socket, meta) {
  const { requestId, agentId, action } = meta;
  const plan = POWER_PLAN[action];
  if (!plan) {
    socket.emit('command:error', { requestId, message: 'Hành động nguồn không hợp lệ' });
    return;
  }
  await delay(600);
  socket.emit('command:data', { requestId, type: 'status', status: 'executed', action, message: `Đã thực thi: ${plan.label}` });
  socket.emit('command:end', { requestId });
  setAgentStatus(agentId, 'offline');
  stopStreamsForAgent(agentId);
  io.emit('agent:status', { id: agentId, status: 'offline' });
  setTimeout(() => {
    setAgentStatus(agentId, 'online');
    io.emit('agent:status', { id: agentId, status: 'online' });
  }, plan.offlineMs);
}

async function handleCommand(socket, msg) {
  const { requestId, agentId, module: moduleName, action, payload = {} } = msg || {};
  if (!requestId || !agentId || !moduleName || !action) return;
  if (action === 'cancel') {
    cancelConsent(payload.requestId);
    return;
  }
  const agent = getAgent(agentId);
  if (!agent) {
    socket.emit('command:error', { requestId, message: 'Agent không tồn tại' });
    return;
  }
  if (agent.status !== 'online') {
    socket.emit('command:error', { requestId, message: `Agent "${agent.name}" đang offline`, code: 'OFFLINE' });
    return;
  }
  if (action === 'stop') {
    stopStream(agentId, moduleName);
    socket.emit('command:end', { requestId });
    return;
  }

  const needConsent = NEEDS_CONSENT[moduleName] && !(moduleName === 'shell' && shellSessions.has(agentId));
  if (needConsent) {
    const decision = await requestConsent(socket, { requestId, agentId, module: moduleName, action });
    if (decision !== 'accepted') return;
    if (moduleName === 'shell') shellSessions.add(agentId);
  }

  if (moduleName === 'processes' && action === 'kill') {
    await delay(350);
    socket.emit('command:data', { requestId, type: 'processes', action: 'kill', pid: payload.pid, ok: true });
    socket.emit('command:end', { requestId });
    return;
  }
  if (moduleName === 'processes') return startProcessStream(socket, agentId, requestId);
  if (moduleName === 'live') return startFrameStream(socket, agentId, requestId, payload.source);
  if (moduleName === 'keylogger') return startKeyloggerStream(socket, agentId, requestId);
  if (moduleName === 'files') return handleFiles(socket, { requestId, agentId, module: moduleName, action }, payload);
  if (moduleName === 'shell') return handleShell(socket, { requestId, agentId, module: moduleName, action }, payload);
  if (moduleName === 'power') return handlePower(socket, { requestId, agentId, module: moduleName, action });
  if (moduleName === 'message') return handleMessage(socket, { requestId, agentId, module: moduleName, action }, payload);
  socket.emit('command:error', { requestId, message: `Module không hỗ trợ: ${moduleName}` });
}

// ===== HTTP + Socket.IO =====

const app = express();
app.use(express.json());
const server = http.createServer(app);
const io = new Server(server, {
  cors: { origin: true, methods: ['GET', 'POST'] },
});

io.on('connection', (socket) => {
  socket.emit('agents:list', getAgents());
  socket.on('agents:get', () => socket.emit('agents:list', getAgents()));
  socket.on('session:start', ({ agentId }) => {
    const agent = getAgent(agentId);
    if (!agent) {
      socket.emit('error:message', { message: 'Agent không tồn tại' });
      return;
    }
    if (agent.status !== 'online') {
      socket.emit('error:message', { message: `Agent "${agent.name}" đang offline, không thể kết nối` });
      return;
    }
    socket.join(`agent:${agentId}`);
    socket.emit('session:started', { agentId });
  });
  socket.on('session:end', ({ agentId }) => {
    socket.leave(`agent:${agentId}`);
  });
  socket.on('command', (msg) => handleCommand(socket, msg));
  socket.on('disconnect', () => {
    for (const [key, s] of [...streams]) {
      if (s.socket === socket) {
        clearInterval(s.timer);
        streams.delete(key);
      }
    }
  });
});

setInterval(() => {
  const offs = getAgents().filter((a) => a.status === 'offline');
  const ons = getAgents().filter((a) => a.status === 'online');
  const pool = offs.length && Math.random() < 0.6 ? offs : ons;
  if (!pool.length) return;
  const agent = pool[Math.floor(Math.random() * pool.length)];
  const next = agent.status === 'online' ? 'offline' : 'online';
  if (next === 'offline') stopStreamsForAgent(agent.id);
  setAgentStatus(agent.id, next);
  io.emit('agent:status', { id: agent.id, status: next });
}, 25000);

app.get('/api/agents', (_req, res) => res.json(getAgents()));
app.get('/api/consent', (_req, res) => res.json(Object.fromEntries(getAgents().map((a) => [a.id, getConsentMode(a.id)]))));
app.post('/api/consent', (req, res) => {
  const { agentId, mode } = req.body || {};
  if (!agentId || !['accept', 'reject', 'timeout', 'random'].includes(mode)) {
    res.status(400).json({ error: 'Cần agentId và mode (accept | reject | timeout | random)' });
    return;
  }
  setConsentMode(agentId, mode);
  res.json({ ok: true, agentId, mode });
});

const dist = path.resolve(__dirname, 'dist');
if (fs.existsSync(dist)) {
  app.use(express.static(dist));
  app.get('*', (_req, res) => res.sendFile(path.join(dist, 'index.html')));
}

server.listen(PORT, () => {
  console.log(`[server] Agent Console mock server đang chạy tại http://localhost:${PORT}`);
  console.log('[server] Web client dev: http://localhost:5173');
  console.log('[server] Đổi chế độ consent: POST /api/consent {"agentId":"pc1","mode":"reject"}');
});
