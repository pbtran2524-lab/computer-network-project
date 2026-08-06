import { useState } from 'react';
import { useModule } from '../hooks/useModule.js';
import ConsentOverlay from '../components/ConsentOverlay.jsx';
import { RefusedState, TimeoutState } from '../components/ModuleStatus.jsx';

export default function MessageModule({ agent }) {
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
