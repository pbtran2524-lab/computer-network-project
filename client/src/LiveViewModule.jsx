import { useCallback, useEffect, useRef, useState } from 'react';
import { useModule } from '../hooks/useModule.js';
import ConsentOverlay from '../components/ConsentOverlay.jsx';
import { IdleState, RefusedState, TimeoutState } from '../components/ModuleStatus.jsx';

export default function LiveViewModule({ agent }) {
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

  const togglePause = () => setPaused((p) => !p);

  if (status === 'idle') {
    return (
      <div className="module">
        <IdleState
          title="Live Screen / Webcam"
          description="Phát trực tiếp màn hình hoặc camera của máy đích. Video sẽ được vẽ liên tục lên canvas từ dữ liệu khung hình gửi về."
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
            <button className="btn btn-ghost" onClick={togglePause} type="button">
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
