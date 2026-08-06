# Excel 转 JSON 工具

`excel_to_json.py` 将策划 `.xlsx` 或 `.xlsm` 工作表转换为 Godot 可读取的 UTF-8 JSON，不依赖第三方 Python 包。旧版二进制 `.xls` 不受支持，请先用 Excel 或 LibreOffice 另存为 `.xlsx`。

## 表格约定

- 默认第 1 行是字段名，之后每个非空行输出一个 JSON 对象。
- 字段名使用 `snake_case`。使用 `stats.hp` 这样的点路径可以生成嵌套对象。
- 空单元格输出为 `null`，整行为空时跳过。
- 表头为空或以 `#` 开头的列会被忽略。
- 如果存在 `id` 列，每个数据行必须提供非空字符串 ID，并且工作表内不能重复。
- 以 `[` 或 `{` 开头的字符串按 JSON 数组或对象解析；需要保留原字符串时使用 `--literal-strings`。
- Excel 数字和布尔值保留对应 JSON 类型。日期建议在表格中保存为 ISO 8601 文本，例如 `2026-08-24`。
- 公式读取 Excel 文件中保存的缓存结果；没有缓存结果的公式会输出 `null`。

## 表格示例

| id | name | tags | stats.hp | enabled |
| :--- | :--- | :--- | :--- | :--- |
| vein_worm | 灵脉蚀虫 | `["beast", "vein"]` | 24 | TRUE |

转换结果：

```json
{
  "schema_version": 1,
  "enemies": [
    {
      "id": "vein_worm",
      "name": "灵脉蚀虫",
      "tags": ["beast", "vein"],
      "stats": {"hp": 24},
      "enabled": true
    }
  ]
}
```

## 使用方式

转换指定工作表，并覆盖已有数据文件：

```bash
python3 client/tools/excel_to_json.py design/items.xlsx \
  --sheet items \
  --root-key items \
  --output client/data/items.json \
  --force
```

转换工作簿内所有可见工作表：

```bash
python3 client/tools/excel_to_json.py design/game_data.xlsx \
  --all-sheets \
  --output client/data/game_data.json
```

先输出到终端检查，不写文件：

```bash
python3 client/tools/excel_to_json.py design/rules.xlsx \
  --sheet rules \
  --root-key rules \
  --output -
```

输出文件已存在时，工具默认终止；只有显式传入 `--force` 才会原子替换原文件。
