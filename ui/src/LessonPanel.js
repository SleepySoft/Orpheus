import React from 'react';

export default function LessonPanel({ lesson, result, checking, onCheck, onClose }) {
  return (
    <div className="modal-backdrop" onClick={onClose}>
      <div className="modal lesson-panel" onClick={(event) => event.stopPropagation()}>
        <h4>{lesson.title || '教学任务'}</h4>
        {lesson.description && <p>{lesson.description}</p>}
        <ol className="lesson-steps">
          {(lesson.steps || []).map((step, index) => (
            <li key={`${index}-${step.title || ''}`}>
              <strong>{step.title || `步骤 ${index + 1}`}</strong>
              {step.body && <p>{step.body}</p>}
            </li>
          ))}
        </ol>
        {result && (
          <div className={`lesson-results ${result.passed ? 'passed' : 'failed'}`}>
            <strong>{result.passed_count}/{result.total} 项通过</strong>
            {(result.results || []).map((item) => (
              <div className="lesson-check" key={item.id}>
                <span className={item.passed ? 'lesson-pass' : 'lesson-fail'}>
                  {item.passed ? '通过' : '未通过'}
                </span>
                <span>{item.label}</span>
                <small>{item.detail}</small>
              </div>
            ))}
          </div>
        )}
        <div className="modal-actions">
          <button onClick={onClose}>关闭</button>
          <button className="primary" disabled={checking} onClick={onCheck}>
            {checking ? '检查中…' : '检查当前工程'}
          </button>
        </div>
      </div>
    </div>
  );
}
