# S 战场出场指令字段角色

当前编辑器的字段规范由 `cczPreviewCore/src/battle_field_spec.cpp` 固化，按角色解释字段，不直接凭下标复用相近指令布局。

| 指令 | 字段角色顺序 | 容量 |
|---|---|---:|
| 0x46 友军 | DataId, Hidden, X, Y, Direction, Level, UnitType, AiPolicy, TargetDataId, TargetX, TargetY | 每页 20 行 |
| 0x47 敌军 | DataId, Reinforcement, Hidden, X, Y, Direction, Level, UnitType, AiPolicy, TargetDataId, TargetX, TargetY | 每页 80 行 |
| 0x4B 我军 | DeploymentIndex, X, Y, Direction, Hidden | 每条记录 5 字段 |

关键隔离规则：

- 0x4B 的 DeploymentIndex 是槽号，不是角色 DataId；4B 记录没有能持久保存角色 DataId 的字段。
- `BattleUnit::battlefield_index` 是 UI 编号；不能作为形象查询键。
- `BattleCharacterEntry::movement_class_code` 是 Data.e5 移动类别；不能与指令 `UnitType` 字段（UI“兵种级别”）或图像 `arm_kind` 混用。
- 方向、等级等字段可能含默认/未知哨兵。只在用户明确编辑时标 dirty；写回保留其他原始字段。
- 0x46 不含援军位；0x47 才含援军位。

完整 S 参数控制设计与实施记录见 [`2026-09-30 S 战场单位参数方案`](../../docs/superpowers/specs/2026-09-30-s-battle-unit-parameter-controls.md)。普罗工具依据为静态托管 IL 分析，未等同运行态行为证据。
