# 共享数据表

本目录保存策划可编辑的 Excel 源表。客户端和服务端运行时不直接读取 Excel，发布数据由 `tools/excel_to_json.py` 按字段归属分别转换到 `client/data/` 和 `server/data/`。

## 数据源映射

| Excel 源表 | 工作表 | 客户端 / 服务端 JSON |
| :--- | :--- | :--- |
| `items.xlsx` | `items` | `client/data/items.json` / `server/data/items.json` |
| `rules.xlsx` | `rules` | `client/data/rules.json` / `server/data/rules.json` |
| `enemies.xlsx` | `enemies` | `client/data/enemies.json` / `server/data/enemies.json` |
| `explore_nodes.xlsx` | `nodes`、`document` | `client/data/explore_nodes.json` / `server/data/explore_nodes.json` |
| `story_nodes.xlsx` | `story_nodes` | `client/data/story_nodes.json` / `server/data/story_nodes.json` |

## 生成命令

在 `dev` 容器的 `/workspace` 中，从仓库根目录一键生成并校验全部 JSON：

```bash
./scripts/generate_client_data.sh
```

脚本默认扫描本目录下全部 `.xlsx` 和 `.xlsm` 文件，跳过 Excel 的 `~$` 临时锁文件，并在 `client/data/`、`server/data/` 生成同名 JSON。每个工作簿应包含一个可见数据工作表；需要补充 JSON 根级字段时，可额外添加一个名为 `document` 的工作表。

单独生成某一份数据时使用下面的转换命令：

```bash
python3 tools/excel_to_json.py share/tables/items.xlsx \
  --sheet items \
  --root-key items \
  --header-row 4 \
  --data-row 5 \
  --scope-row 2 \
  --target client \
  --output client/data/items.json \
  --force
```

其他普通数据表使用相同的行号约定：

```bash
python3 tools/excel_to_json.py share/tables/rules.xlsx \
  --sheet rules --root-key rules --header-row 4 --data-row 5 \
  --scope-row 2 --target client \
  --output client/data/rules.json --force

python3 tools/excel_to_json.py share/tables/enemies.xlsx \
  --sheet enemies --root-key enemies --header-row 4 --data-row 5 \
  --scope-row 2 --target client \
  --output client/data/enemies.json --force

python3 tools/excel_to_json.py share/tables/story_nodes.xlsx \
  --sheet story_nodes --root-key story_nodes --header-row 4 --data-row 5 \
  --scope-row 2 --target client \
  --output client/data/story_nodes.json --force
```

探索节点除 `nodes` 数据表外，还使用 `document` 工作表保存根级的起始节点和 P1 路线：

```bash
python3 tools/excel_to_json.py share/tables/explore_nodes.xlsx \
  --sheet nodes \
  --document-sheet document \
  --root-key nodes \
  --header-row 4 \
  --data-row 5 \
  --scope-row 2 \
  --target client \
  --output client/data/explore_nodes.json \
  --force
```

将单表命令中的 `--target client` 和输出目录分别改为 `--target server`、`server/data/`，即可生成服务端版本。

所有工作表都不设置额外的说明列，从 A 列开始每一列都对应一个正式字段。第 1 行是字段说明，第 2 行是数据归属，第 3 行是字段类型，第 4 行是字段名，第 5 行开始是正式数据；前三行元数据必须与第 4 行字段逐列对齐。数据归属只允许 `client`、`server`、`both`。`document` 工作表只允许一条正式数据。提交前应确认转换命令成功，并校验生成 JSON 中的 ID、数组、对象和跨表引用。
