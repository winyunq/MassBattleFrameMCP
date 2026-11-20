---
name: massbattle-unit-authoring
description: 用户进行 UE5 MassBattleFrame 资产转换与适配时使用。本 skill 介绍本插件的其他 skill。
---

# Mass 资产操作

MassBattleFrame 简称 Mass；本插件中的 Mass 特指 MassBattleFrame。

Unit 指游戏中的单位；Source Actor 是单位的源蓝图，单位配置 DataAsset 是供游戏使用的配置资产。

## 工具介绍

Mass MCP 聚焦资产操作。基础操作调用单条命令，复合操作组合已有命令完成。

| 资产类型 | 功能介绍 | 支持操作 |
|---|---|---|
| 单位源蓝图（Source Actor） | 我们定义的单位“源码”和编辑入口。保存维护结果，新增动画等修改可复用已有配置，也支持用户直接编辑。 | 创建、读写、保存、更新导出 |
| 单位配置 DataAsset | Mass 单位的核心配置资产，保存游戏最终使用的数据。 | 创建、读写、导出、删除 |
| 投射物配置 DataAsset | 非瞬发伤害（弹道武器）使用；基于投射物配置制作攻击特效。 | 创建、读写、校验、删除 |
| 网格资产 | 单位的几何模型，转换后供 Mass 渲染使用。 | 读取、转换、生成静态网格与 LOD |
| 动画序列 | 单位动作素材，用于动作映射和 VAT 烘焙。 | 查找、兼容性检查、配置动画映射 |
| VAT 数据资产 | Mass 单位的核心配置资产，保存游戏最终动画的数据。 | 创建、烘焙、更新 |
| 材质实例 | 模型的外观配置，用于绑定贴图和 VAT 动画数据。 | 创建、贴图绑定与更新 |
| Niagara 系统 | 特效表现资产，适配后供 Mass 批量调用。 | 读写属性、模块和图，转换批量特效 |
| 渲染器蓝图 | 绑定模型或特效及其默认参数，供 Mass 渲染使用。 | 读取、修改默认配置与资产绑定 

MCP 的读写、修改等操作均针对 Target。指定一次操作对象后，可连续执行多次操作。先理清任务流程，再组合调用 MCP 工具完成需求。

以下业务流程展示了如何通过MCP组合实现业务流程；你也可以通过自行编写脚本实现批量处理

按任务读取对应 skill：

- 单位与建筑的数值调整：[数值调整](../massbattle-stat-tuning/SKILL.md)。
- Niagara 特效转换与适配：[massbattle-effect-mcp](../massbattle-effect-mcp/SKILL.md)。
- 瞬发伤害及一次性攻击特效：[massbattle-instant-damage-fx](../massbattle-instant-damage-fx/SKILL.md)。
- 非瞬发伤害、弹道武器及投射物配置：[massbattle-projectile-authoring](../massbattle-projectile-authoring/SKILL.md)。
