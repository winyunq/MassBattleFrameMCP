---
name: massbattle-stat-tuning
description: 用户调整 Mass Unit 时使用。
---

# 数值调整

用户希望调整游戏中某个单位的属性，包括挂载特效、光环半径、体积、血量、尺寸等任意属性。

这类业务通过 Unit 读写实现；考虑可维护性，推荐通过单位源蓝图（Source Actor）维护，再同步到单位配置 DataAsset。

Unit 读写通常只需少量调用，无须设置 Target，直接用 `unit_path` 指定对象。

查找通常沿用默认范围，省略 `options.roots`。`unit_write` 默认保存修改。

`unit_write` 按字段差异（diff）增量合并：`unit_data` 只传需要修改的字段，未提供的字段保持原样。

## 直接修改单位

1. 使用 `unit_list` 寻找单位配置 DataAsset。
2. 从结果中选定目标单位，记住其资产路径，供后续调用的 `unit_path` 使用。
3. 使用 `unit_write` 修改属性：`unit_path` 传选定的资产路径，`unit_data` 传属性补丁。

## 统计游戏单位

1. 使用 `unit_list` 寻找需要统计的单位配置。
2. 从结果中选定要统计的单位，记住需要维护单位的资产路径。
3. 使用 `unit_write` 修改属性。

如果需要批量处理，建议直接编写脚本批量执行第 3 步。

## 维护游戏单位

单位源蓝图作为单位配置的草稿和源码，保存维护结果；游戏使用的是同步后的单位配置 DataAsset。

1. 使用 `unit_find_assets` 寻找对应的单位源蓝图。
2. 选定源蓝图，记住其资产路径，供后续调用的 `unit_path` 使用。
3. 使用 `unit_write` 写入属性补丁，`unit_path` 传源蓝图资产路径；工具保存源蓝图并执行 `Update`，同步到单位配置 DataAsset。
