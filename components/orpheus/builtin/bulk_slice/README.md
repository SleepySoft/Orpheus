# 数组步进切片

`component.yaml` 的 `interfaces` 是声明事实源，独立组件脚本生成
`generated/`；算法在 `user/algorithm.c` 中将 `out[i]` 复制为
`in[offset + i * stride]` 的原始四字节位型。

输入和输出的 `block_size` 表示每通道元素数，不替代 Task 处理量子。
每次输入交付执行一次切片，填完输出后显式发射编号 1；没有会合等待、
同拍去重或额外调度。NULL 输出允许，但仍检查输入；零处理量子不写数据。

纯 `validate` 钩子在候选参数提交前检查最后访问槽。
越界配置、容量不足、错误格式和输入输出重叠明确失败。
无效配置不修改既有参数，数据搬运保留 NaN 载荷、无穷和负零。
实例状态由调用者按生成描述符提供对齐的 Arena。
统一壳要求 Buffer 自身的帧数不超过容量；未知参数返回
`ORPHEUS_ERR_NOT_FOUND`，不忽略配置项。
