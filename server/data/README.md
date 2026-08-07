# 服务端静态数据

本目录保存由 `share/tables/` 中策划 Excel 生成的服务端运行时 JSON。生成文件只包含 Excel 第 2 行标记为 `server` 或 `both` 的字段，不应手工编辑。

## 生成方式

在 `dev` 容器的 `/workspace` 中，从仓库根目录执行：

```bash
./scripts/generate_client_data.sh
```

脚本会同时暂存并校验客户端、服务端数据，全部通过后再分别更新 `client/data/` 和 `server/data/`。

## 文件清单

| 文件 | 内容 |
| :--- | :--- |
| `items.json` | 服务端需要的道具标识、分类、占格、标签和效果 |
| `rules.json` | 服务端规则触发、条件、效果、代价和限制 |
| `enemies.json` | 服务端敌人属性、技能和掉落标签 |
| `explore_nodes.json` | 服务端探索节点、遭遇、奖励和连接关系 |
| `story_nodes.json` | 服务端剧情前置、效果和后续节点 |

所有文件都保留 `schema_version`。跨表引用必须使用稳定的 `id`，生成脚本会校验 ID 唯一性以及道具、规则、敌人和节点引用。
