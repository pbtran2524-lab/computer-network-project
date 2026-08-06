import { useAgent } from './core.jsx';

export default function Toaster() {
  const { toasts, dismissToast } = useAgent();
  return (
    <div className="toast-container">
      {toasts.map((t) => (
        <div key={t.id} className={`toast ${t.type}`}>
          <span className="toast-dot" />
          <span className="toast-msg">{t.message}</span>
          <button className="toast-close" onClick={() => dismissToast(t.id)} type="button">
            x
          </button>
        </div>
      ))}
    </div>
  );
}

export function ConsentOverlay({ visible, subtitle, onCancel }) {
  if (!visible) return null;
  return (
    <div className="consent-overlay">
      <div className="consent-box">
        <div className="spinner" />
        <div className="consent-title">Đang chờ người dùng đồng ý...</div>
        {subtitle && <div className="consent-sub">{subtitle}</div>}
        <button className="btn btn-ghost" onClick={onCancel} type="button">
          Hủy yêu cầu
        </button>
      </div>
    </div>
  );
}

export function IdleState({ title, description, actionLabel, onAction, disabled }) {
  return (
    <div className="module-idle">
      <div className="module-idle-icon" />
      <h3>{title}</h3>
      <p>{description}</p>
      <button className="btn btn-primary" onClick={onAction} disabled={disabled} type="button">
        {actionLabel}
      </button>
      {disabled && <div className="hint-warn">Agent đang offline — không thể gửi lệnh.</div>}
    </div>
  );
}

export function RefusedState({ message, onRetry }) {
  return (
    <div className="module-flag error">
      <div className="flag-title">Yêu cầu bị từ chối</div>
      <p>{message || 'Người dùng đã từ chối cấp quyền hoặc yêu cầu đã bị hủy.'}</p>
      <button className="btn" onClick={onRetry} type="button">
        Thử lại
      </button>
    </div>
  );
}

export function TimeoutState({ onRetry }) {
  return (
    <div className="module-flag error">
      <div className="flag-title">Hết thời gian chờ</div>
      <p>Agent không phản hồi yêu cầu cấp quyền trong thời gian cho phép. Yêu cầu đã bị hủy.</p>
      <button className="btn" onClick={onRetry} type="button">
        Thử lại
      </button>
    </div>
  );
}
