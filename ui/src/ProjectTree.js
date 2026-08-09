import React, { useMemo, useState } from 'react';
import { isSubRef, subIdOf, subViewKey } from './graphUtils';

/* 蒸馏分析里的块 → Orpheus 组件映射规则（顺序即优先级，先匹配先生效）。
 * status: builtin=已有组件 / substitute=可用现有组件替代 / missing=无内置需自定义 / na=结构件无需组件 */
const BLOCK_RULES = [
  { re: /pooliir/i, name: 'pooliir IIR 加速器', status: 'substitute', detail: '→ biquad_bank（N 段双二阶）' },
  { re: /biquad/i, name: 'Biquad', status: 'builtin', detail: 'biquad / biquad_bank' },
  { re: /fir/i, name: 'FIR', status: 'builtin', detail: 'fir（kind: bulk 系数）' },
  { re: /(?:r?fft|ifft|stft)/i, name: 'FFT/RFFT/IFFT', status: 'missing', detail: '无内置，需自定义 DSP 组件' },
  { re: /windowing|窗函数|窗/i, name: '窗函数', status: 'builtin', detail: 'window（BULK 系数）' },
  { re: /limiter/i, name: 'Limiter', status: 'builtin', detail: 'limiter（阈值/attack/release）' },
  { re: /softclipper|clipper/i, name: 'SoftClipper', status: 'builtin', detail: 'soft_clipper（tanh + drive）' },
  { re: /saturation/i, name: 'Saturation', status: 'builtin', detail: 'saturation（上限/软硬）' },
  { re: /matrixmultiply|矩阵乘/i, name: 'MatrixMultiply', status: 'builtin', detail: 'matrix_mul（BULK 矩阵）' },
  { re: /coherence|相干/i, name: '相干计算', status: 'missing', detail: '无内置，需自定义' },
  { re: /noiseslew/i, name: 'NoiseSlew', status: 'builtin', detail: 'noise_slew（rise/fall 速率）' },
  { re: /speedbounds/i, name: 'SpeedBounds', status: 'missing', detail: '无内置，需自定义' },
  { re: /leveldetect/i, name: 'LevelDetect', status: 'builtin', detail: 'level_detect（峰值/RMS 探针）' },
  { re: /sleepingbeauty/i, name: 'SleepingBeauty', status: 'missing', detail: '无内置，近似 fade 的自定义 ramper' },
  { re: /reverb/i, name: 'ReverbExtraction', status: 'missing', detail: '无内置，需自定义' },
  { re: /switch/i, name: 'Switch', status: 'builtin', detail: 'switch（enable 直通/静音）' },
  { re: /spatialfader/i, name: 'SpatialFader', status: 'substitute', detail: '→ fade' },
  { re: /selector/i, name: 'Selector', status: 'substitute', detail: '→ input_select / output_router' },
  { re: /(?:output|input)_select|inputselect|\broster\b/i, name: '路由/选择', status: 'builtin', detail: 'input_select / output_router' },
  { re: /downmix/i, name: 'Downmix', status: 'substitute', detail: '→ mixer' },
  { re: /sumofelements|sum\(|求和/i, name: 'Sum', status: 'substitute', detail: '→ mixer' },
  { re: /lpf|lowpass|low.?pass/i, name: 'LPF', status: 'builtin', detail: 'biquad type=lowpass' },
  { re: /volume/i, name: 'Volume', status: 'substitute', detail: '→ gain' },
  { re: /balance/i, name: 'Balance', status: 'builtin', detail: 'balance' },
  { re: /delay/i, name: 'Delay', status: 'builtin', detail: 'delay（大缓冲需评估内存）' },
  { re: /gain/i, name: 'Gain', status: 'builtin', detail: 'gain' },
  { re: /mute/i, name: 'Mute', status: 'builtin', detail: 'mute' },
  { re: /fade/i, name: 'Fade', status: 'builtin', detail: 'fade' },
  { re: /bass/i, name: 'Bass', status: 'builtin', detail: 'bass' },
  { re: /midrange/i, name: 'Midrange', status: 'builtin', detail: 'midrange' },
  { re: /treble/i, name: 'Treble', status: 'builtin', detail: 'treble' },
  { re: /mixer/i, name: 'Mixer', status: 'builtin', detail: 'mixer' },
  { re: /psd|smooth/i, name: 'PSD/平滑', status: 'missing', detail: '无内置，需自定义（单极点平滑可用 biquad 近似）' },
  { re: /bufferin|bufferout|块缓冲/i, name: '块缓冲', status: 'na', detail: '结构件，Orpheus 按 block 调度无需' },
  { re: /正弦调制/i, name: '正弦调制', status: 'missing', detail: '无内置（signal_gen 仅测试信号源）' },
];

const STATUS_META = {
  builtin: { label: '已有', cls: 'ok' },
  substitute: { label: '可替代', cls: 'alt' },
  missing: { label: '缺失', cls: 'miss' },
  na: { label: '结构件', cls: 'na' },
};

function analyzeBlocks(text) {
  const out = [];
  const seen = new Set();
  for (const r of BLOCK_RULES) {
    if (r.re.test(text) && !seen.has(r.name)) {
      seen.add(r.name);
      out.push(r);
    }
  }
  return out;
}

function BlockChip({ rule }) {
  const meta = STATUS_META[rule.status];
  return (
    <span className="block-chip" title={rule.detail}>
      <span className={`badge ${meta.cls}`}>{meta.label}</span>
      {rule.name}
    </span>
  );
}

function Blocks({ text }) {
  const rules = useMemo(() => analyzeBlocks(text || ''), [text]);
  if (!rules.length) return null;
  return (
    <div className="chain-blocks">
      {rules.map((r) => (
        <BlockChip key={r.name} rule={r} />
      ))}
    </div>
  );
}

/** 蒸馏分析内容（无外层容器，供工程树内嵌）。 */
function DistillContent({ doc }) {
  const mt = doc?.model_tree;
  const [collapsed, setCollapsed] = useState({});
  const toggle = (key) => setCollapsed((prev) => ({ ...prev, [key]: !prev[key] }));
  if (!mt) return null;

  const Section = ({ title, children }) => (
    <div className="chain">
      <div className="chain-title">{title}</div>
      {children}
    </div>
  );

  const renderGeneric = (node, depth) => {
    const key = `${depth}:${node.id || node.label}`;
    const isOpen = !collapsed[key];
    return (
      <div key={key} className="tree-item" style={{ paddingLeft: depth * 12 }}>
        <div className="tree-item-row" onClick={() => node.children && node.children.length && toggle(key)}>
          {node.children && node.children.length ? (
            <span className="tree-caret">{isOpen ? '▼' : '▶'}</span>
          ) : (
            <span className="tree-caret" />
          )}
          <span className="tree-item-label">{node.label || node.id}</span>
          {node.filter && <span className="tree-kind">{node.filter}</span>}
        </div>
        {isOpen && node.children && node.children.map((c) => renderGeneric(c, depth + 1))}
        {isOpen && node.params && typeof node.params === 'object' && (
          <div className="tree-params">
            {Object.entries(node.params)
              .map(([k, v]) => `${k}=${Array.isArray(v) ? `[${v.length}]` : v}`)
              .join('  ')}
          </div>
        )}
      </div>
    );
  };

  return (
    <>
      <div className="distill-header">
        <div className="distill-name">{mt.name || doc?.metadata?.name}</div>
        {mt.source && <div className="distill-meta">{mt.source}</div>}
        {mt.base_rate && <div className="distill-meta">{mt.base_rate}</div>}
      </div>
      <div className="distill-legend">
        {Object.entries(STATUS_META).map(([k, m]) => (
          <span key={k} className={`badge ${m.cls}`}>
            {m.label}
          </span>
        ))}
        <span className="distill-legend-note">点块看替代/说明</span>
      </div>

      {mt.children ? (
        <div className="distill-generic">{mt.children.map((c) => renderGeneric(c, 0))}</div>
      ) : (
        <>
          {Array.isArray(mt.task_flows) && mt.task_flows.length > 0 && (
            <Section title={`Task 流程（${mt.task_flows.length}）`}>
              {mt.task_flows.map((t) => {
                const key = `tid:${t.tid}`;
                const open = !collapsed[key];
                return (
                  <div key={key} className="tree-item">
                    <div className="tree-item-row" onClick={() => toggle(key)}>
                      <span className="tree-caret">{open ? '▼' : '▶'}</span>
                      <span className="tree-item-label">
                        TID{t.tid} · {t.rate_hz}Hz · {t.label}
                      </span>
                    </div>
                    {open && (
                      <div className="chain-flow">
                        {t.chain}
                        <Blocks text={t.chain} />
                      </div>
                    )}
                  </div>
                );
              })}
            </Section>
          )}

          {Array.isArray(mt.chains) && mt.chains.length > 0 && (
            <Section title={`音频链（${mt.chains.length}）`}>
              {mt.chains.map((ch) => (
                <div key={ch.id} className="chain">
                  <div className="chain-title">{ch.label || ch.id}</div>
                  <div className="chain-flow">{ch.flow}</div>
                  <Blocks text={`${ch.flow || ''} ${Array.isArray(ch.params) ? ch.params.join(' ') : ch.params || ''}`} />
                  {ch.params && (
                    <div className="tree-params">
                      {Array.isArray(ch.params) ? ch.params.join('；') : ch.params}
                    </div>
                  )}
                </div>
              ))}
            </Section>
          )}

          {Array.isArray(mt.pooliir_instances) && mt.pooliir_instances.length > 0 && (
            <Section title={`pooliir 实例（${mt.pooliir_instances.length}）→ biquad_bank`}>
              {mt.pooliir_instances.map((p) => (
                <div key={p.name} className="tree-params">
                  <BlockChip rule={{ name: 'pooliir', status: 'substitute', detail: '→ biquad_bank' }} />
                  {p.name}
                  {p.channels ? ` · ${p.channels}ch × ${p.stages || '?'}stages · workMem ${p.work_mem}` : ` · workMem ${p.work_mem}`}
                </div>
              ))}
            </Section>
          )}

          {Array.isArray(mt.delay_lines) && mt.delay_lines.length > 0 && (
            <Section title={`延迟线（${mt.delay_lines.length}）→ delay`}>
              {mt.delay_lines.map((d) => (
                <div key={d.name} className="tree-params">
                  <BlockChip rule={{ name: 'Delay', status: 'builtin', detail: 'delay（大缓冲需评估内存）' }} />
                  {d.name} · {d.samples} samples
                  {d.channels ? ` × ${d.channels}ch` : ''}
                </div>
              ))}
            </Section>
          )}

          {Array.isArray(mt.parameter_partitions) && mt.parameter_partitions.length > 0 && (
            <Section title={`参数分区（${mt.parameter_partitions.length}）`}>
              {mt.parameter_partitions.map((p) => {
                const key = `part:${p.partition}`;
                const open = !collapsed[key];
                return (
                  <div key={key} className="tree-item">
                    <div className="tree-item-row" onClick={() => toggle(key)}>
                      <span className="tree-caret">{open ? '▼' : '▶'}</span>
                      <span className="tree-item-label">
                        {p.partition} · {p.label}
                      </span>
                    </div>
                    {open && <div className="tree-params">{p.fields}</div>}
                  </div>
                );
              })}
            </Section>
          )}
        </>
      )}
    </>
  );
}

function ProjectOutline({ doc, views, subsMeta, activeView, onLocate, onOpenView }) {
  const [query, setQuery] = useState('');
  const [showDistill, setShowDistill] = useState(false);
  const [expanded, setExpanded] = useState(() => {
    const init = { main: true };
    for (const s of subsMeta || []) init[subViewKey(s.id)] = true;
    return init;
  });
  const mt = doc?.model_tree;

  const outline = useMemo(() => {
    const subName = (id) => subsMeta.find((s) => s.id === id)?.name || id;
    const walk = (viewKey, nodes) =>
      (nodes || []).map((n) => {
        const comp = n.data.component;
        if (isSubRef(comp)) {
          const sid = subIdOf(comp);
          return {
            id: `${viewKey}/${n.id}`,
            kind: 'sub',
            node: n,
            subId: sid,
            name: subName(sid),
            viewKey,
            children: walk(subViewKey(sid), views[subViewKey(sid)]?.nodes || []),
          };
        }
        return { id: `${viewKey}/${n.id}`, kind: 'node', node: n, viewKey };
      });
    return walk('main', views.main?.nodes || []);
  }, [views, subsMeta]);

  const matches = (n) => {
    if (!query) return true;
    const q = query.toLowerCase();
    const label = n.data.label || n.id;
    return (
      label.toLowerCase().includes(q) ||
      n.id.toLowerCase().includes(q) ||
      (n.data.component || '').toLowerCase().includes(q)
    );
  };

  const renderItem = (item, depth) => {
    const isSub = item.kind === 'sub';
    // 搜索时强制展开，避免命中项被折叠隐藏
    const open = query ? true : expanded[isSub ? subViewKey(item.subId) : item.id] !== false;
    const node = item.node;
    const shortComp = isSub ? 'sub' : (node.data.component || '').split('.').pop();
    const active = isSub ? activeView === subViewKey(item.subId) : false;

    if (query && !isSub && !matches(node)) return null;
    if (query && isSub && !matches(node)) {
      // 子组件名字命中或内部节点命中时才保留
      const hasHit = (item.children || []).some((c) => c.kind === 'sub' || matches(c.node));
      if (!hasHit) return null;
    }

    return (
      <div key={item.id} className="tree-item" style={{ paddingLeft: depth * 12 }}>
        <div
          className={`tree-item-row ${active ? 'active' : ''}`}
          onClick={() => {
            if (isSub) {
              toggleSub(item.subId);
              onOpenView(subViewKey(item.subId));
            } else {
              onLocate(item.viewKey, node.id);
            }
          }}
          title={
            isSub
              ? `${item.name}（子组件，点击打开）`
              : `${node.data.label || node.id} · ${node.data.component || ''}`
          }
        >
          <span className="tree-caret">{isSub && item.children.length ? (open ? '▼' : '▶') : ''}</span>
          <span className="tree-item-label">{node.data.label || node.id}</span>
          <span className="tree-kind">{shortComp}</span>
          {node.data.missing && <span className="tree-missing-badge">组件缺失</span>}
          {isSub && <span className="palette-count">{item.children.length}</span>}
        </div>
        {isSub && open && (item.children || []).map((c) => renderItem(c, depth + 1))}
      </div>
    );
  };

  const toggleSub = (subId) =>
    setExpanded((prev) => ({ ...prev, [subViewKey(subId)]: prev[subViewKey(subId)] !== false }));

  const mainOpen = query ? true : expanded.main !== false;

  return (
    <div className="tree-root">
      <input
        className="tree-search"
        placeholder="搜索节点（名称 / id / 组件）"
        value={query}
        onChange={(e) => setQuery(e.target.value)}
      />
      {mt && (
        <div className="tree-item">
          <div
            className="tree-item-row"
            onClick={() => setShowDistill(true)}
            title="查看完整蒸馏分析：Task 流程、各链的滤波器块映射与参数（标注缺失/可替代组件）"
          >
            <span className="tree-caret">▶</span>
            <span className="tree-item-label">蒸馏分析</span>
            <span className="palette-count">查看</span>
          </div>
        </div>
      )}
      {showDistill && (
        <div className="distill-modal-overlay" onClick={() => setShowDistill(false)}>
          <div className="distill-modal" onClick={(e) => e.stopPropagation()}>
            <div className="distill-modal-header">
              <span>蒸馏分析（来源：工程 YAML 顶层 model_tree）</span>
              <button onClick={() => setShowDistill(false)}>× 关闭</button>
            </div>
            <div className="distill-modal-body">
              <DistillContent doc={doc} />
            </div>
          </div>
        </div>
      )}
      <div className="tree-item">
        <div
          className={`tree-item-row ${activeView === 'main' ? 'active' : ''}`}
          onClick={() => {
            setExpanded((prev) => ({ ...prev, main: !prev.main }));
            onOpenView('main');
          }}
        >
          <span className="tree-caret">{mainOpen ? '▼' : '▶'}</span>
          <span className="tree-item-label">主图</span>
          <span className="palette-count">{outline.length}</span>
        </div>
        {mainOpen && outline.map((c) => renderItem(c, 1))}
      </div>
    </div>
  );
}

/** 左侧栏：工程树导航（含可选的蒸馏分析内嵌）。 */
export default function ProjectTree(props) {
  return (
    <ProjectOutline
      doc={props.doc}
      views={props.views}
      subsMeta={props.subsMeta}
      activeView={props.activeView}
      onLocate={props.onLocate}
      onOpenView={props.onOpenView}
    />
  );
}
