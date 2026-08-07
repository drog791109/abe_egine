# 共享数据表

本目录保存策划可编辑的 Excel 源表。客户端运行时不直接读取 Excel，发布数据由 `client/tools/excel_to_json.py` 转换到 `client/data/`。

## 数据源映射

| Excel 源表 | 工作表 | JSON 产物 |
| :--- | :--- | :--- |
| `items.xlsx` | `items` | `client/data/items.json` |

## 生成命令

从仓库根目录执行：

```bash
python3 client/tools/excel_to_json.py share/tables/items.xlsx \
  --sheet items \
  --root-key items \
  --header-row 4 \
  --data-row 5 \
  --output client/data/items.json \
  --force
```

`items` 工作表不设置额外的说明列，从 A 列开始每一列都对应一个正式字段。第 1 行是字段说明，第 2 行是数据归属，第 3 行是字段类型，第 4 行是字段名，第 5 行开始是正式数据；前三行元数据必须与第 4 行字段逐列对齐。提交前应确认转换命令成功，并校验生成 JSON 中的 ID、数组和对象字段。
