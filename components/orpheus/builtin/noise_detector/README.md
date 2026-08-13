# 噪声检测(单端) 组件说明
> 组件 ID `orpheus.builtin.noise_detector` · 类型：纯观测（直通输出，不做任何去噪/滤波）
> 靠 readback 上报指标并依阈值给 UI 节点加黄/红预警色。

## 1. 为什么需要它
“主观视听”难以定量“这一段到底噪不噪、哪里爆音”。本组件在流程中插一个“观测探针”节点，把信号“噪声化”的几个维度数字化：

- 频谱平坦度（噪声为主 vs 乐音集中）
- 宽带噪声底（相对信号的底噪高低）
- 时域突刺计数 / 削波占比（爆声/杂音/过载）

它不做自动处理——“怎么处理”由你在流程中另接其他组件决定。

## 2. 端口与参数
- 端口：`in`（输入） / `out`（直通输出），通道数取决于 `channels`。
- 参数：
  - `channels`(默认2)：通道数，restart生效。
  - `clip_level`(默认0.999)：绝对值超过此阈值计为削波。
  - `click_thres`(默认0.3)：相邻样本差分超过此阈值计为突刺。
- readback：`flatness` / `noise_floor_db` / `clicks` / `clip_pct` / `detail`(JSON)。
  - `flatness` → 0 代表纯音集中，→ 1 代表白噪全频铺平。
  - `noise_floor_db`：底噪相对信号的 dB。

## 3. 用法（怎么接）
1. 在被测节点（EQ / 软限幅 / 调色 / 混音等）的“输出之后”插入本组件：`前级:out → in`，`out → 后级:in`。
2. `in` 接好后运行，看 指标 readback：
   - `flatness` 接近1 且 `noise_floor_db` 高 → 噪声为主；反之纯音集中。
   - `clicks` / `clip_pct` 非零 → 爆声/杂音/过载，提示查前级是否过载。
3. 想逐级定位：把本组件插入流程各个节点后，看哪个级后 `flatness`/底噪突变。

## 4. 实例（对应测试 `test_noise_detectors.py`）
```yaml
graph:
  nodes:
    - id: wav_in ; component: orpheus.builtin.wav_in ; params: {file_path: input.wav, channels: 2}
    - id: nd      ; component: orpheus.builtin.noise_detector ; params: {channels: 2, clip_level: 0.999, click_thres: 0.3}
    - id: out     ; component: orpheus.builtin.wav_out ; params: {file_path: outputs/out.wav, channels: 2, sample_rate: 48000}
  connections:
    - {from: wav_in:out, to: nd:in}
    - {from: nd:out, to: out:in}
```
快速验证：输入一段干净正弦 → `flatness` 很低；换成白噪 → `flatness` 接近1、`noise_floor_db` 提高。相关组件共同读取见 `docs/HOW.md` §30。
