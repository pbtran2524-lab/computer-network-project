import { useCallback, useState } from 'react';
import { useModule } from '../hooks/useModule.js';
import { MODULE_LABELS, downloadBlob, formatBytes } from '../utils.js';
import ConsentOverlay from '../components/ConsentOverlay.jsx';
import { IdleState, RefusedState, TimeoutState } from '../components/ModuleStatus.jsx';

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
        {isDir ? (
          <span className="tree-twist">{isOpen ? 'v' : '>'}</span>
        ) : (
          <span className="tree-twist" />
        )}
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

export default function FileManagerModule({ agent }) {
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

  const toggle = (path) => {
    setExpanded((cur) => {
      const next = new Set(cur);
      if (next.has(path)) next.delete(path);
      else next.add(path);
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
                <div className="file-detail-empty">Chọn một file trong cây bên trái để tải về. Module: {MODULE_LABELS.files}</div>
              )}
            </div>
          </div>
        </>
      )}
    </div>
  );
}
