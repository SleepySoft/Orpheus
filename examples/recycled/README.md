# Recycled examples

这里保存仍可加载、但不再出现在 UI 示例列表中的历史工程。它们主要是早期 smoke、单组件测试或已被更完整示例替代的片段。

| 工程 | 回收原因 | 推荐替代 |
|---|---|---|
| `anc_fxlms_demo.yaml` | 单体组件演示，教学信息较少 | `../anc_fxlms_decomposed.yaml` |
| `delay_test.yaml` | 单组件试听测试 | 组件 README / 自建链路 |
| `mp3_play.yaml` | 格式接入测试，不构成完整工作流 | `../headphone_music_player.yaml` |
| `smoke_big6.yaml` | BIG6 QA smoke | 自动化组件测试 |
| `smoke_fade.yaml` | Fade QA smoke | 自动化组件测试 |
| `smoke_routing.yaml` | Router QA smoke | `../wav_channel_map.yaml` |
| `test_gain_chain.yaml` | 编译器 fixture | 自动化编译测试 |
| `test_slc_matrix_mul.yaml` | SLC 组件 fixture | 自动化数值测试 |
| `wav_gain_chain.yaml` | 被更完整文件效果链覆盖 | `../wav_gain_biquad.yaml` |

回收工程继续通过 `orpheus_core.cli validate examples/recycled` 校验，但不会被 `GET /api/examples` 列出。