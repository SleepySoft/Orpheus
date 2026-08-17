#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""pooliir → SOS 转换脚本

Symphony Symphony 生成的 pooliir 系数是紧凑型双二阶级联格式：

    每个通道：gain, [a2, a1, b2/b0, b1/b0] * num_stages

其中 gain = 所有 SOS 段 b0 的乘积。本脚本把它还原成标准 SOS：

    [b0, b1, b2, a1, a2] * num_stages

并输出可直接填进 Orpheus `iir_bank` 的 `coefs` 字符串。

用法示例：
    python scripts/pooliir2sos.py \\
        --coeffs "0.000246762647,0.96057564,-1.95958507,1.0,-1.7446692,0.984361,..." \\
        --stages 2,2,2,2 \\
        --gain-mode equal \\
        --format iir_bank

也可以从文件读取：
    python scripts/pooliir2sos.py --file path/to/Model_Target_Ehc_p0_b0_TOP.c \\
        --var MicAaFilterpooliirCoeffs --stages 2,2,2,2
"""

import argparse
import json
import math
import re
import sys
from pathlib import Path
from typing import List, Literal

# 保证 Windows PowerShell 下中文错误信息正常显示
sys.stdout.reconfigure(encoding="utf-8")
sys.stderr.reconfigure(encoding="utf-8")


def _extract_numbers(text: str) -> List[float]:
    """从 C/MATLAB Initializer 文本中提取所有浮点/整数值。"""
    text = re.sub(r"//[^\n]*", "", text)
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    # 去掉尾缀 F/U/L 等
    tokens = re.findall(r"[-+]?\d*\.?\d+(?:[eE][-+]?\d+)?[FfUuLl]?", text)
    out = []
    for t in tokens:
        t = t.rstrip("FfUuLl")
        try:
            out.append(float(t))
        except ValueError:
            pass
    return out


def _partition(coeffs: List[float], stages: List[int]) -> List[List[float]]:
    """按每通道级数把 flat 系数切分成每通道块。"""
    blocks: List[List[float]] = []
    idx = 0
    for s in stages:
        length = 1 + 4 * s
        if idx + length > len(coeffs):
            raise ValueError(
                f"系数长度不足：通道 {len(blocks)+1} 需要 {length} 个值，"
                f"但从偏移 {idx} 开始只剩 {len(coeffs) - idx} 个。"
            )
        blocks.append(coeffs[idx : idx + length])
        idx += length
    if idx != len(coeffs):
        sys.stderr.write(
            f"警告：切分后还剩 {len(coeffs) - idx} 个未使用系数。\n"
        )
    return blocks


def _block_to_sos(block: List[float], gain_mode: Literal["equal", "first", "last"]) -> List[List[float]]:
    """把一个通道的 pooliir 块转换为 SOS 列表，每段 [b0,b1,b2,a1,a2]。"""
    if len(block) < 1:
        return []
    gain = block[0]
    n = (len(block) - 1) // 4
    if 1 + 4 * n != len(block):
        raise ValueError(f"通道系数长度 {len(block)} 不符合 1+4*num_stages 格式。")

    if gain_mode == "equal":
        if n == 0:
            per_stage_gain = gain
        else:
            per_stage_gain = gain ** (1.0 / n) if gain >= 0 else -((-gain) ** (1.0 / n))
        stage_gains = [per_stage_gain] * n
    elif gain_mode == "first":
        stage_gains = [gain] + [1.0] * (n - 1)
    elif gain_mode == "last":
        stage_gains = [1.0] * (n - 1) + [gain]
    else:
        raise ValueError(f"未知的 gain_mode: {gain_mode}")

    sos: List[List[float]] = []
    for k in range(n):
        a2 = block[1 + 4 * k + 0]
        a1 = block[1 + 4 * k + 1]
        b2_over_b0 = block[1 + 4 * k + 2]
        b1_over_b0 = block[1 + 4 * k + 3]
        b0 = stage_gains[k]
        sos.append([b0, b1_over_b0 * b0, b2_over_b0 * b0, a1, a2])
    return sos


def pooliir_to_sos(
    coeffs: List[float],
    stages: List[int],
    gain_mode: Literal["equal", "first", "last"] = "equal",
) -> List[List[List[float]]]:
    """把多通道 pooliir 系数转换为 SOS。"""
    blocks = _partition(coeffs, stages)
    return [_block_to_sos(b, gain_mode) for b in blocks]


def _is_stable(stage: List[float]) -> bool:
    """简单双二阶稳定性检查（基于 a1, a2）。"""
    _, _, _, a1, a2 = stage
    return abs(a2) < 1.0 and abs(a1) < 1.0 + a2


def _format_iir_bank(sos_channels: List[List[List[float]]]) -> str:
    """输出 Orpheus iir_bank 可直接使用的 flat 系数字符串。"""
    if not sos_channels:
        return ""
    num_stages = max(len(ch) for ch in sos_channels)
    # 用 identity 段补齐短通道
    flat: List[float] = []
    identity = [1.0, 0.0, 0.0, 0.0, 0.0]
    for ch in sos_channels:
        for stage in ch:
            flat.extend(stage)
        for _ in range(num_stages - len(ch)):
            flat.extend(identity)
    return ",".join(f"{v:g}" for v in flat)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="把 Symphony pooliir 系数转换为标准 SOS / Orpheus iir_bank 系数。"
    )
    parser.add_argument(
        "--coeffs",
        type=str,
        default=None,
        help="逗号/空格分隔的 pooliir 系数，或包含系数的原始 C 文本片段。",
    )
    parser.add_argument(
        "--file",
        type=Path,
        default=None,
        help="从文件读取系数（自动提取数字）。",
    )
    parser.add_argument(
        "--var",
        type=str,
        default=None,
        help="在 --file 中定位变量名后的 { ... } 作为系数来源（简化扫描）。",
    )
    parser.add_argument(
        "--stages",
        type=str,
        required=True,
        help="每通道级数，逗号分隔，例如 2,2,2,2。",
    )
    parser.add_argument(
        "--channels",
        type=int,
        default=None,
        help="通道数；若提供且 --stages 为单个值，则所有通道级数相同。",
    )
    parser.add_argument(
        "--gain-mode",
        choices=["equal", "first", "last"],
        default="equal",
        help="总增益分配方式：equal=每段均分（默认），first=第一段，last=最后一段。",
    )
    parser.add_argument(
        "--format",
        choices=["json", "iir_bank", "yaml"],
        default="iir_bank",
        help="输出格式。",
    )
    parser.add_argument(
        "--check-stability",
        action="store_true",
        help="检查每段稳定性并输出警告。",
    )
    args = parser.parse_args()

    # 读取系数
    if args.file:
        text = args.file.read_text(encoding="utf-8")
        if args.var:
            # 先在注释里定位变量名，再取它后面的第一个 { ... } 初始化块
            marker = re.search(
                rf"/\*[^*]*{re.escape(args.var)}[^*]*\*/", text, re.S
            )
            if not marker:
                sys.stderr.write(f"错误：在文件中找不到变量 {args.var} 的注释标记。\n")
                return 1
            m = re.search(r"\{(.*?)\}", text[marker.end() :], re.S)
            if not m:
                sys.stderr.write(f"错误：变量 {args.var} 标记后找不到初始化块。\n")
                return 1
            text = m.group(1)
        coeffs = _extract_numbers(text)
    elif args.coeffs:
        coeffs = _extract_numbers(args.coeffs)
    else:
        sys.stderr.write("错误：必须提供 --coeffs 或 --file。\n")
        return 1

    # 解析级数
    stages_raw = [int(x.strip()) for x in args.stages.split(",") if x.strip()]
    if args.channels is not None:
        if len(stages_raw) == 1:
            stages = stages_raw * args.channels
        else:
            sys.stderr.write("错误：提供 --channels 时，--stages 只能是单个值。\n")
            return 1
    else:
        stages = stages_raw

    expected = sum(1 + 4 * s for s in stages)
    if len(coeffs) != expected:
        sys.stderr.write(
            f"警告：系数数量 {len(coeffs)} 与期望 {expected} 不一致 "
            f"（级数配置 {stages}）。将继续尝试切分。\n"
        )

    try:
        sos_channels = pooliir_to_sos(coeffs, stages, args.gain_mode)
    except ValueError as e:
        sys.stderr.write(f"错误：{e}\n")
        return 1

    # 稳定性检查
    if args.check_stability:
        for ci, ch in enumerate(sos_channels, 1):
            for si, stage in enumerate(ch, 1):
                if not _is_stable(stage):
                    sys.stderr.write(
                        f"警告：通道 {ci} 第 {si} 段可能不稳定：a1={stage[3]}, a2={stage[4]}\n"
                    )

    # 输出
    if args.format == "json":
        print(json.dumps(sos_channels, indent=2))
    elif args.format == "yaml":
        print("# 每通道 SOS 列表 [b0, b1, b2, a1, a2]")
        for ci, ch in enumerate(sos_channels, 1):
            print(f"channel_{ci}:")
            for stage in ch:
                print(f"  - [{', '.join(f'{v:g}' for v in stage)}]")
    else:  # iir_bank
        num_stages = max(len(ch) for ch in sos_channels)
        flat = _format_iir_bank(sos_channels)
        print(f"# pooliir → SOS 转换结果")
        print(f"# 通道数：{len(sos_channels)}，每通道级数：{num_stages}")
        print(f"# iir_bank 参数：")
        print(f"coefs_mode: per_channel")
        print(f"num_stages: {num_stages}")
        print(f"coefs: {flat}")

    return 0


if __name__ == "__main__":
    sys.exit(main())
