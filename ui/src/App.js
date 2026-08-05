import React, { useState, useCallback, useEffect } from 'react';
import ReactFlow, {
  Background,
  Controls,
  MiniMap,
  addEdge,
  useNodesState,
  useEdgesState,
  Handle,
  Position,
} from 'reactflow';
import 'reactflow/dist/style.css';

import './App.css';

const defaultProject = {
  version: "0.1.0",
  sample_rate: 48000,
  block_size: 128,
  graph: {
    nodes: [
      { id: 'wav_in', component: 'orpheus.builtin.wav_in', params: { file_path: 'test_input.wav', channels: 2 }, position: { x: 100, y: 100 } },
      { id: 'gain', component: 'orpheus.builtin.gain', params: { gain_db: -6, channels: 2 }, position: { x: 350, y: 100 } },
      { id: 'biquad', component: 'orpheus.builtin.biquad', params: { type: 'lowpass', fc: 2000, q: 0.707, gain_db: 0, channels: 2 }, position: { x: 600, y: 100 } },
      { id: 'wav_out', component: 'orpheus.builtin.wav_out', params: { file_path: 'test_output.wav', channels: 2, sample_rate: 48000 }, position: { x: 850, y: 100 } },
    ],
    connections: [
      { from: 'wav_in:out', to: 'gain:in' },
      { from: 'gain:out', to: 'biquad:in' },
      { from: 'biquad:out', to: 'wav_out:in' },
    ],
  },
};

function OrpheusNode({ data, selected }) {
  const ports = data.ports || [];
  const inputs = ports.filter(p => p.direction === 'input');
  const outputs = ports.filter(p => p.direction === 'output');

  return (
    <div className={`orpheus-node ${selected ? 'selected' : ''}`}>
      <div className="node-header">{data.label}</div>
      <div className="node-body">
        <div className="node-ports inputs">
          {inputs.map(p => (
            <div key={p.id} className="port-row">
              <Handle type="target" position={Position.Left} id={p.id} />
              <span>{p.id}</span>
            </div>
          ))}
        </div>
        <div className="node-ports outputs">
          {outputs.map(p => (
            <div key={p.id} className="port-row">
              <span>{p.id}</span>
              <Handle type="source" position={Position.Right} id={p.id} />
            </div>
          ))}
        </div>
      </div>
    </div>
  );
}

const nodeTypes = { orpheus: OrpheusNode };

function App() {
  const [nodes, setNodes, onNodesChange] = useNodesState([]);
  const [edges, setEdges, onEdgesChange] = useEdgesState([]);
  const [selectedNode, setSelectedNode] = useState(null);

  const loadProject = useCallback((project) => {
    const ns = project.graph.nodes.map(n => ({
      id: n.id,
      type: 'orpheus',
      position: n.position || { x: 0, y: 0 },
      data: {
        label: `${n.id}\n${n.component}`,
        component: n.component,
        params: n.params || {},
        ports: [
          { id: 'in', direction: 'input' },
          { id: 'out', direction: 'output' },
        ],
      },
    }));

    const es = project.graph.connections.map((c, idx) => {
      const [source, sourceHandle] = c.from.split(':');
      const [target, targetHandle] = c.to.split(':');
      return {
        id: `e${idx}`,
        source,
        target,
        sourceHandle,
        targetHandle,
      };
    });

    setNodes(ns);
    setEdges(es);
  }, [setNodes, setEdges]);

  useEffect(() => {
    loadProject(defaultProject);
  }, [loadProject]);

  const onConnect = useCallback((params) => setEdges((eds) => addEdge(params, eds)), [setEdges]);

  const onNodeClick = useCallback((_, node) => {
    setSelectedNode(node);
  }, []);

  const runProject = useCallback(() => {
    alert('Run: 调用 orpheus-cli compile 与 orpheus_runtime（需后端服务支持）');
  }, []);

  return (
    <div className="app">
      <div className="toolbar">
        <h1>Orpheus</h1>
        <button onClick={runProject}>▶ 运行</button>
        <button onClick={() => loadProject(defaultProject)}>重新加载示例</button>
      </div>
      <div className="main">
        <div className="canvas">
          <ReactFlow
            nodes={nodes}
            edges={edges}
            onNodesChange={onNodesChange}
            onEdgesChange={onEdgesChange}
            onConnect={onConnect}
            onNodeClick={onNodeClick}
            nodeTypes={nodeTypes}
            fitView
          >
            <Background />
            <Controls />
            <MiniMap />
          </ReactFlow>
        </div>
        <div className="sidebar">
          <h3>参数面板</h3>
          {selectedNode ? (
            <div>
              <p><strong>{selectedNode.id}</strong></p>
              <p>{selectedNode.data.component}</p>
              <pre>{JSON.stringify(selectedNode.data.params, null, 2)}</pre>
            </div>
          ) : (
            <p>选择一个节点查看参数</p>
          )}
        </div>
      </div>
    </div>
  );
}

export default App;
