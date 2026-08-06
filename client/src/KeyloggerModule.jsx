import { useEffect, useRef, useState } from 'react';
import { useModule } from '../hooks/useModule.js';
import ConsentOverlay from '../components/ConsentOverlay.jsx';
import { IdleState, RefusedState, TimeoutState } from '../components/ModuleStatus.jsx';

export default function KeyloggerModule({ agent }) {
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
