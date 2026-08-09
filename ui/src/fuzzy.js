/** 模糊匹配：query 的字符按顺序出现在 text 中即命中（不区分大小写）。
 *  中文按字符子序列匹配（如 "滤波" 命中 "滤波器"），英文按字母子序列匹配
 *  （如 "bq" 命中 "biquad_bank"）。 */
export function fuzzyMatch(query, ...fields) {
  const q = String(query || '').toLowerCase().trim();
  if (!q) return true;
  const t = fields.map((f) => String(f || '')).join(' ').toLowerCase();
  let qi = 0;
  for (let i = 0; i < t.length && qi < q.length; i++) {
    if (t[i] === q[qi]) qi++;
  }
  return qi === q.length;
}
