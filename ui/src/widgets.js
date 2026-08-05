import React, { useState } from 'react';
import FileBrowseModal from './FileBrowseModal';

/**
 * Widget registry: manifest `widget` hint -> React control component.
 * To customize a parameter's control: register a widget here and declare
 * `widget: <name>` in the component manifest. Unknown hints fall back to
 * type inference (float/int -> number, bool -> checkbox, string -> text).
 *
 * Every widget receives: { schema, value, onChange(id, value), disabled, ctx }
 * ctx carries { dynamicOptions, projectName }.
 */

function NumberWidget({ schema, value, onChange, disabled }) {
  const isInt = schema.type === 'int';
  return (
    <input
      type="number"
      value={value ?? ''}
      step={isInt ? 1 : 'any'}
      min={schema.range?.[0]}
      max={schema.range?.[1]}
      disabled={disabled}
      onChange={(e) => {
        const raw = e.target.value;
        if (raw === '') return onChange(schema.id, isInt ? 0 : 0.0);
        onChange(schema.id, isInt ? parseInt(raw, 10) : parseFloat(raw));
      }}
    />
  );
}

function TextWidget({ schema, value, onChange, disabled }) {
  return (
    <input
      type="text"
      value={value ?? ''}
      disabled={disabled}
      onChange={(e) => onChange(schema.id, e.target.value)}
    />
  );
}

function SliderWidget({ schema, value, onChange, disabled }) {
  const min = schema.range?.[0] ?? 0;
  const max = schema.range?.[1] ?? 1;
  const v = value ?? schema.default ?? min;
  return (
    <div className="slider-widget">
      <input
        type="range"
        min={min}
        max={max}
        step={schema.type === 'int' ? 1 : (max - min) / 200}
        value={v}
        disabled={disabled}
        onChange={(e) =>
          onChange(schema.id, schema.type === 'int' ? parseInt(e.target.value, 10) : parseFloat(e.target.value))
        }
      />
      <span className="slider-value">{typeof v === 'number' ? +v.toFixed(3) : v}</span>
    </div>
  );
}

function normalizeOptions(schema, ctx) {
  if (schema.options_source && ctx?.dynamicOptions?.[schema.options_source]) {
    return ctx.dynamicOptions[schema.options_source];
  }
  return (schema.options || []).map((o) =>
    typeof o === 'object' && o !== null ? { value: o.value, label: o.label ?? String(o.value) } : { value: o, label: String(o) }
  );
}

function SelectWidget({ schema, value, onChange, disabled, ctx }) {
  const options = normalizeOptions(schema, ctx);
  return (
    <select
      value={value ?? schema.default ?? ''}
      disabled={disabled}
      onChange={(e) => onChange(schema.id, e.target.value)}
    >
      {options.map((o) => (
        <option key={String(o.value)} value={o.value}>
          {o.label}
        </option>
      ))}
    </select>
  );
}

function CheckboxWidget({ schema, value, onChange, disabled }) {
  return (
    <input
      type="checkbox"
      checked={Boolean(value ?? schema.default)}
      disabled={disabled}
      onChange={(e) => onChange(schema.id, e.target.checked)}
    />
  );
}

function FileWidget({ schema, value, onChange, disabled, ctx }) {
  const [browsing, setBrowsing] = useState(false);
  const ext = schema.file_ext || '.wav';
  return (
    <div className="file-widget">
      <input type="text" value={value ?? ''} readOnly placeholder="（未选择文件）" disabled={disabled} />
      <button disabled={disabled || !ctx?.projectName} onClick={() => setBrowsing(true)}>
        浏览…
      </button>
      {browsing && (
        <FileBrowseModal
          projectName={ctx.projectName}
          ext={ext}
          onSelect={(path) => {
            onChange(schema.id, path);
            setBrowsing(false);
          }}
          onClose={() => setBrowsing(false)}
        />
      )}
    </div>
  );
}

export const WIDGETS = {
  number: NumberWidget,
  text: TextWidget,
  slider: SliderWidget,
  select: SelectWidget,
  checkbox: CheckboxWidget,
  file: FileWidget,
};

export function inferWidget(schema) {
  if (schema.widget && WIDGETS[schema.widget]) return schema.widget;
  if (schema.options || schema.options_source) return 'select';
  if (schema.type === 'float' || schema.type === 'int') return 'number';
  if (schema.type === 'bool') return 'checkbox';
  return 'text';
}

/** Render one parameter field using the widget registry. */
export default function ParamField({ schema, value, onChange, ctx }) {
  const Widget = WIDGETS[inferWidget(schema)];
  const disabled = Boolean(schema.readonly);
  return (
    <div className={`param-field ${disabled ? 'readonly' : ''}`}>
      <label>
        {schema.name || schema.id}
        {schema.unit ? ` (${schema.unit})` : ''}
        {disabled && <span className="readonly-badge">只读</span>}
      </label>
      <Widget schema={schema} value={value} onChange={onChange} disabled={disabled} ctx={ctx} />
    </div>
  );
}
