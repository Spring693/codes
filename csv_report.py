#!/usr/bin/env python3
"""CSV 数据分析报告生成器（纯标准库）。

功能：读取任意 CSV 文件，自动识别数值列，统计计数、均值、中位数、
最值、标准差与缺失值数量，并输出一份纯文本分析报告。

用法：
    python3 csv_report.py data.csv
    python3 csv_report.py data.csv --output report.txt
    python3 csv_report.py data.csv --column 销售额
"""

from __future__ import annotations

import argparse
import csv
import math
import statistics
import sys
from pathlib import Path


def read_csv(path: Path) -> tuple[list[str], list[dict[str, str]]]:
    """读取 CSV，返回表头与行字典列表。"""
    if not path.is_file():
        raise FileNotFoundError(f"找不到文件：{path}")

    with path.open("r", encoding="utf-8-sig", newline="") as fp:
        reader = csv.DictReader(fp)
        header = reader.fieldnames or []
        rows = [row for row in reader if any((v or "").strip() for v in row.values())]

    if not header:
        raise ValueError("CSV 文件为空或缺少表头")
    return header, rows


def numeric_columns(header: list[str], rows: list[dict[str, str]], threshold: float = 0.6) -> list[str]:
    """挑出「数值占比超过 threshold」的列，视为可统计的数值列。"""
    if not rows:
        return []

    result = []
    for col in header:
        numeric_hits = 0
        for row in rows:
            value = (row.get(col) or "").strip()
            if not value:
                continue
            try:
                float(value)
                numeric_hits += 1
            except ValueError:
                pass
        if numeric_hits / len(rows) >= threshold:
            result.append(col)
    return result


def column_stats(name: str, rows: list[dict[str, str]]) -> dict:
    """计算单列的统计指标。"""
    values: list[float] = []
    missing = 0
    invalid = 0

    for row in rows:
        raw = (row.get(name) or "").strip()
        if not raw:
            missing += 1
            continue
        try:
            values.append(float(raw))
        except ValueError:
            invalid += 1

    stats = {
        "column": name,
        "count": len(values),
        "missing": missing,
        "invalid": invalid,
        "mean": None,
        "median": None,
        "min": None,
        "max": None,
        "stdev": None,
        "sum": None,
    }

    if values:
        stats.update(
            {
                "mean": statistics.fmean(values),
                "median": statistics.median(values),
                "min": min(values),
                "max": max(values),
                "stdev": statistics.stdev(values) if len(values) > 1 else 0.0,
                "sum": math.fsum(values),
            }
        )
    return stats


def render_report(path: Path, header: list[str], rows: list[dict[str, str]],
                  targets: list[str]) -> str:
    """把统计结果渲染成人类可读的文本报告。"""
    lines: list[str] = []
    lines.append("=" * 56)
    lines.append(f"CSV 数据分析报告：{path.name}")
    lines.append("=" * 56)
    lines.append(f"总行数    ：{len(rows)}")
    lines.append(f"总列数    ：{len(header)}")
    lines.append(f"列名      ：{', '.join(header)}")
    lines.append("")
    lines.append("-" * 56)

    for col in targets:
        stats = column_stats(col, rows)
        lines.append(f"字段：{stats['column']}")
        if stats["count"] == 0:
            lines.append("  该列没有可解析的数值，跳过统计。")
        else:
            lines.append(f"  有效数值：{stats['count']}")
            lines.append(f"  缺失值  ：{stats['missing']}")
            lines.append(f"  非法值  ：{stats['invalid']}")
            lines.append(f"  合计    ：{stats['sum']:.4f}")
            lines.append(f"  均值    ：{stats['mean']:.4f}")
            lines.append(f"  中位数  ：{stats['median']:.4f}")
            lines.append(f"  最小值  ：{stats['min']:.4f}")
            lines.append(f"  最大值  ：{stats['max']:.4f}")
            lines.append(f"  标准差  ：{stats['stdev']:.4f}")
        lines.append("-" * 56)

    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description="生成 CSV 数据分析报告")
    parser.add_argument("csv_file", help="待分析的 CSV 文件路径")
    parser.add_argument("-c", "--column", action="append", default=[],
                        help="只统计指定列，可重复传入")
    parser.add_argument("-o", "--output", help="报告输出文件，默认打印到终端")
    args = parser.parse_args()

    try:
        header, rows = read_csv(Path(args.csv_file))
    except (FileNotFoundError, ValueError) as exc:
        print(f"错误：{exc}", file=sys.stderr)
        return 1

    if not rows:
        print("错误：CSV 中没有有效数据行。", file=sys.stderr)
        return 1

    if args.column:
        unknown = [c for c in args.column if c not in header]
        if unknown:
            print(f"错误：以下列不存在：{', '.join(unknown)}", file=sys.stderr)
            return 1
        targets = args.column
    else:
        targets = numeric_columns(header, rows)
        if not targets:
            print("未自动识别到数值列，可用 -c 指定要统计的列。", file=sys.stderr)
            return 1

    report = render_report(Path(args.csv_file), header, rows, targets)

    if args.output:
        Path(args.output).write_text(report + "\n", encoding="utf-8")
        print(f"报告已写入：{args.output}")
    else:
        print(report)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
