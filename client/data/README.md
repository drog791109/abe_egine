# P0 静态数据约定

本目录保存 Godot 客户端随版本发布的静态 JSON 配置。P0 先冻结可读、可校验的数据合同，P1 再实现加载器和运行时对象。

## 文件清单

| 文件 | 内容 | P0 数量 |
| :--- | :--- | :--- |
| `items.json` | 道具、占格形状、标签、基础效果和来源 | 28 件 |
| `rules.json` | 规则触发、条件、效果、代价和限制 | 16 条 |
| `enemies.json` | 普通敌人、精英、首领和掉落标签 | 9 个 |
| `explore_nodes.json` | 节点式探索路线、遭遇和奖励 | 11 个 |
| `story_nodes.json` | 剧情前置、结果和后续节点 | 14 个 |

## 通用字段规则

- 根对象必须包含整数 `schema_version`，字段不兼容时递增版本。
- `id` 使用唯一的 `snake_case` 英文标识；存档和跨表引用只保存 `id`，不保存数组下标。
- `name`、`title` 和描述文本允许中文，仅用于显示，不参与逻辑判断。
- `category`、`rank`、`node_type` 等枚举值使用小写英文。
- 可选集合使用空数组，不省略字段；不存在的单值引用使用 `null`。
- 数值效果由 `type` 和参数对象表达。P1 加载器遇到未知 `type` 时应报错并跳过该条配置。
- 规则、敌人和探索节点使用 `p1_priority: true` 标记 P1 最小演示范围；道具使用 `source_stage: P1`。

## 道具占格

`shape` 是相对左上原点 `[0, 0]` 的坐标列表。例如 `[[0, 0], [1, 0]]` 表示横向两格。旋转时按 90 度顺时针变换坐标，再将最小横纵坐标归零；`rotatable: false` 的道具不生成旋转状态。

## 引用关系

- 探索节点的 `encounter_id` 引用 `enemies.json` 中的敌人 `id`。
- 探索节点的 `rewards` 引用 `items.json` 中的道具 `id`。
- `p1_route` 是 P1 灰盒专用的连续试玩顺序；完整内容使用各节点的 `next_nodes`。
- 剧情节点的 `unlock_rule` 引用 `rules.json` 中的规则 `id`。
- `requirements`、`effects`、`skills`、`base_effects` 的 `type` 由对应运行时注册表解释。

P1 不允许在场景脚本中复制这些数值；灰盒调整应直接修改 JSON，并重新加载配置。

策划 Excel 表可使用 [`client/tools/excel_to_json.py`](../tools/excel_to_json.py) 转换；表头、嵌套字段和运行示例见 [`client/tools/README.md`](../tools/README.md)。
