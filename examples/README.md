# Orpheus examples

顶层只保留具有独立用户价值、教学价值或架构参考意义的工程。纯 smoke、测试 fixture、重复链路放入 [`recycled/`](recycled/README.md)，不会出现在 UI 示例列表中。

## 产品工作流

- `headphone_music_player.yaml`：系统音频到耳机的完整调音链。
- `video_loudness_voice.yaml`：视频响度均衡与人声增强。
- `wav_gain_biquad.yaml`：最小文件效果链。
- `device_gain_biquad.yaml`：最小设备实时效果链。

## 教学与功能

- `anc_fxlms_decomposed.yaml`：可展开的 FxLMS 教学图。
- `video_loudness_voice_decomposed.yaml`：响度控制链分解。
- `control_link_demo.yaml`：非音频控制连接。
- `mux_ab_compare.yaml`：平滑 A/B 对比。
- `probe_waveform_scope.yaml`、`signal_probe_wav.yaml`：观测组件。
- `sweep_record_plot.yaml`、`sweep_spectrum.yaml`：扫频测量。
- `wav_channel_map.yaml`：通道拆分、映射与合并。
- `wav_resample.yaml`：多速率与分频调度。

## 平台与架构

- `pc_dsp_dual_target.yaml`：Win/DSP alter 双目标。
- `adsp21593_sport_gain.yaml`：SPORT/TDM 外部触发。
- `embed_chain.yaml`：嵌入式平台 IO 骨架。
- `dsp_model_reference.yaml`：复杂递归复合与参数布局参考。

## 外部模型

- `symphony_asm_ehc_rnc.yaml`：ASM/EHC/RNC 多 Task 参考。
- `symphony_baf_structural_reference.yaml`：BAF 全局结构调查参考。
- `symphony_baf_heq/`：目录工程形式的 Model_1_2 HEQ 可执行蒸馏切片。

## 收录准则

新增顶层示例至少满足一项：完整用户工作流、唯一平台能力、可执行教学内容、复杂模型事实来源。只验证单一组件或编译器行为的工程应进入自动测试；仍有历史参考价值时放入 `recycled/`。