#!/usr/bin/env python3
"""Convert an Excel workbook into UTF-8 JSON for the Godot client.

Usage:
    python3 client/tools/excel_to_json.py INPUT [options]

Parameters:
    INPUT                       Source workbook. Must be an existing .xlsx or
                                .xlsm file; legacy .xls files are not supported.
    -o, --output PATH           Destination JSON path. Defaults to INPUT with
                                its suffix replaced by .json. Use - for stdout.
    --sheet NAME               Convert the named worksheet. Defaults to the
                                first visible worksheet.
    --all-sheets               Convert every visible worksheet under a
                                top-level "sheets" object. Cannot be combined
                                with --sheet or --root-key.
    --root-key KEY             Top-level array key for one-sheet output.
                                Defaults to a snake_case form of the sheet name
                                and must match [a-z][a-z0-9_]*.
    --header-row NUMBER        One-based row containing field names; must be at
                                least 1 (default: 1).
    --schema-version NUMBER    Non-negative schema_version written at the JSON
                                root (default: 1).
    --indent NUMBER            JSON indentation from 0 through 8 (default: 2).
    --literal-strings          Keep strings beginning with [ or { as strings
                                instead of parsing them as JSON arrays/objects.
    --force                    Replace an existing destination file. Without
                                this option, an existing file is an error.
    -h, --help                 Show command help.

Environment variables:
    None.

Examples:
    python3 client/tools/excel_to_json.py design/items.xlsx \
        --sheet items --root-key items -o client/data/items.json --force
    python3 client/tools/excel_to_json.py design/game_data.xlsx \
        --all-sheets -o client/data/game_data.json
    python3 client/tools/excel_to_json.py design/rules.xlsx \
        --sheet rules --root-key rules --output -
"""

import argparse
import json
import math
import os
import posixpath
import re
import sys
import tempfile
import zipfile
from pathlib import Path
from xml.etree import ElementTree


EXIT_OK = 0
EXIT_FAILED = 1

SPREADSHEET_NS = "http://schemas.openxmlformats.org/spreadsheetml/2006/main"
DOCUMENT_REL_NS = "http://schemas.openxmlformats.org/officeDocument/2006/relationships"
PACKAGE_REL_NS = "http://schemas.openxmlformats.org/package/2006/relationships"
ROOT_KEY_PATTERN = re.compile(r"^[a-z][a-z0-9_]*$")
CELL_REFERENCE_PATTERN = re.compile(r"^([A-Za-z]+)([1-9][0-9]*)$")


class ConversionError(Exception):
    """A workbook or conversion error suitable for command-line output."""


def xml_name(namespace, local_name):
    """Return an ElementTree qualified XML name."""
    return "{%s}%s" % (namespace, local_name)


def parse_arguments(argv):
    """Parse and validate command-line arguments."""
    parser = argparse.ArgumentParser(
        description="Convert .xlsx/.xlsm worksheet rows into UTF-8 JSON."
    )
    parser.add_argument("input", type=Path, metavar="INPUT")
    parser.add_argument("-o", "--output", type=Path)
    parser.add_argument("--sheet")
    parser.add_argument("--all-sheets", action="store_true")
    parser.add_argument("--root-key")
    parser.add_argument("--header-row", type=int, default=1)
    parser.add_argument("--schema-version", type=int, default=1)
    parser.add_argument("--indent", type=int, default=2)
    parser.add_argument("--literal-strings", action="store_true")
    parser.add_argument("--force", action="store_true")
    args = parser.parse_args(argv)

    if args.all_sheets and args.sheet:
        parser.error("--all-sheets cannot be combined with --sheet")
    if args.all_sheets and args.root_key:
        parser.error("--all-sheets cannot be combined with --root-key")
    if args.header_row < 1:
        parser.error("--header-row must be at least 1")
    if args.schema_version < 0:
        parser.error("--schema-version must be non-negative")
    if args.indent < 0 or args.indent > 8:
        parser.error("--indent must be between 0 and 8")
    if args.root_key and not ROOT_KEY_PATTERN.fullmatch(args.root_key):
        parser.error("--root-key must match [a-z][a-z0-9_]*")

    return args


def normalize_archive_path(base_path, target):
    """Resolve an Office relationship target to a safe ZIP member path."""
    if target.startswith("/"):
        resolved = posixpath.normpath(target.lstrip("/"))
    else:
        resolved = posixpath.normpath(
            posixpath.join(posixpath.dirname(base_path), target)
        )
    if resolved == ".." or resolved.startswith("../"):
        raise ConversionError("workbook relationship escapes the Excel archive")
    return resolved


def read_workbook_sheets(archive):
    """Read worksheet names, visibility, and XML member paths in workbook order."""
    workbook_path = "xl/workbook.xml"
    relationships_path = "xl/_rels/workbook.xml.rels"
    try:
        workbook_root = ElementTree.fromstring(archive.read(workbook_path))
        relationships_root = ElementTree.fromstring(
            archive.read(relationships_path)
        )
    except KeyError as error:
        raise ConversionError("input is missing required Excel workbook files") from error
    except ElementTree.ParseError as error:
        raise ConversionError("input contains invalid workbook XML") from error

    relationship_targets = {}
    for relationship in relationships_root.findall(
        xml_name(PACKAGE_REL_NS, "Relationship")
    ):
        if relationship.get("TargetMode") == "External":
            continue
        relationship_id = relationship.get("Id")
        target = relationship.get("Target")
        if relationship_id and target:
            relationship_targets[relationship_id] = normalize_archive_path(
                workbook_path, target
            )

    sheets_parent = workbook_root.find(xml_name(SPREADSHEET_NS, "sheets"))
    if sheets_parent is None:
        raise ConversionError("workbook contains no worksheets")

    sheets = []
    relationship_attribute = xml_name(DOCUMENT_REL_NS, "id")
    for sheet in sheets_parent.findall(xml_name(SPREADSHEET_NS, "sheet")):
        title = sheet.get("name")
        relationship_id = sheet.get(relationship_attribute)
        target = relationship_targets.get(relationship_id)
        if not title or not target:
            raise ConversionError("worksheet has an invalid relationship")
        if target not in archive.namelist():
            raise ConversionError(
                "worksheet '%s' points to missing archive member '%s'"
                % (title, target)
            )
        sheets.append(
            {
                "title": title,
                "path": target,
                "state": sheet.get("state", "visible"),
            }
        )

    if not sheets:
        raise ConversionError("workbook contains no worksheets")
    return sheets


def read_shared_strings(archive):
    """Read the workbook shared-string table, including rich text runs."""
    shared_strings_path = "xl/sharedStrings.xml"
    if shared_strings_path not in archive.namelist():
        return []

    try:
        root = ElementTree.fromstring(archive.read(shared_strings_path))
    except ElementTree.ParseError as error:
        raise ConversionError("input contains invalid shared-string XML") from error

    values = []
    for string_item in root.findall(xml_name(SPREADSHEET_NS, "si")):
        text_parts = []
        for text_node in string_item.iter(xml_name(SPREADSHEET_NS, "t")):
            text_parts.append(text_node.text or "")
        values.append("".join(text_parts))
    return values


def column_index(cell_reference):
    """Convert an Excel cell reference such as AB12 to a zero-based column."""
    match = CELL_REFERENCE_PATTERN.fullmatch(cell_reference or "")
    if not match:
        raise ConversionError("invalid Excel cell reference '%s'" % cell_reference)

    index = 0
    for character in match.group(1).upper():
        index = index * 26 + ord(character) - ord("A") + 1
    return index - 1


def parse_number(raw_value, cell_reference):
    """Convert an Excel numeric XML value to an int or finite float."""
    try:
        if re.fullmatch(r"[+-]?[0-9]+", raw_value):
            return int(raw_value)
        value = float(raw_value)
    except ValueError as error:
        raise ConversionError(
            "cell %s contains invalid number '%s'" % (cell_reference, raw_value)
        ) from error
    if not math.isfinite(value):
        raise ConversionError("cell %s contains a non-finite number" % cell_reference)
    return value


def parse_cell_value(cell, shared_strings):
    """Convert one worksheet cell from Office XML to a JSON-compatible value."""
    cell_reference = cell.get("r", "unknown")
    cell_type = cell.get("t", "n")

    if cell_type == "inlineStr":
        inline_string = cell.find(xml_name(SPREADSHEET_NS, "is"))
        if inline_string is None:
            return ""
        return "".join(
            text_node.text or ""
            for text_node in inline_string.iter(xml_name(SPREADSHEET_NS, "t"))
        )

    value_node = cell.find(xml_name(SPREADSHEET_NS, "v"))
    if value_node is None or value_node.text is None:
        return None
    raw_value = value_node.text

    if cell_type == "s":
        try:
            shared_string_index = int(raw_value)
            return shared_strings[shared_string_index]
        except (ValueError, IndexError) as error:
            raise ConversionError(
                "cell %s has an invalid shared-string index" % cell_reference
            ) from error
    if cell_type in ("str", "d"):
        return raw_value
    if cell_type == "b":
        if raw_value not in ("0", "1"):
            raise ConversionError(
                "cell %s has invalid boolean value '%s'"
                % (cell_reference, raw_value)
            )
        return raw_value == "1"
    if cell_type == "e":
        raise ConversionError(
            "cell %s contains Excel error '%s'" % (cell_reference, raw_value)
        )
    if cell_type == "n":
        return parse_number(raw_value, cell_reference)

    raise ConversionError(
        "cell %s uses unsupported Excel type '%s'" % (cell_reference, cell_type)
    )


def read_sheet_rows(archive, sheet, shared_strings):
    """Return sparse worksheet rows as (row number, column-value mapping)."""
    rows = []
    try:
        with archive.open(sheet["path"]) as worksheet_stream:
            for _event, element in ElementTree.iterparse(
                worksheet_stream, events=("end",)
            ):
                if element.tag != xml_name(SPREADSHEET_NS, "row"):
                    continue
                raw_row_number = element.get("r")
                try:
                    row_number = int(raw_row_number)
                except (TypeError, ValueError) as error:
                    raise ConversionError(
                        "worksheet '%s' contains a row without a valid number"
                        % sheet["title"]
                    ) from error

                values = {}
                for cell in element.findall(xml_name(SPREADSHEET_NS, "c")):
                    reference = cell.get("r")
                    values[column_index(reference)] = parse_cell_value(
                        cell, shared_strings
                    )
                rows.append((row_number, values))
                element.clear()
    except ElementTree.ParseError as error:
        raise ConversionError(
            "worksheet '%s' contains invalid XML" % sheet["title"]
        ) from error
    return rows


def normalize_header(value, sheet_title, column_number):
    """Validate and normalize one field-name cell."""
    if value is None:
        return None
    header = str(value).strip()
    if not header or header.startswith("#"):
        return None
    segments = header.split(".")
    if any(not segment or not ROOT_KEY_PATTERN.fullmatch(segment) for segment in segments):
        raise ConversionError(
            "worksheet '%s' column %d has invalid field name '%s'; "
            "use snake_case names separated by dots"
            % (sheet_title, column_number, header)
        )
    return segments


def read_headers(sheet_title, row_values):
    """Build a column-to-field-path mapping and reject ambiguous paths."""
    headers = {}
    seen_paths = set()
    for column, value in sorted(row_values.items()):
        path = normalize_header(value, sheet_title, column + 1)
        if path is None:
            continue
        path_tuple = tuple(path)
        if path_tuple in seen_paths:
            raise ConversionError(
                "worksheet '%s' contains duplicate field '%s'"
                % (sheet_title, ".".join(path))
            )
        for existing_path in seen_paths:
            shortest_length = min(len(path_tuple), len(existing_path))
            if path_tuple[:shortest_length] == existing_path[:shortest_length]:
                raise ConversionError(
                    "worksheet '%s' has conflicting fields '%s' and '%s'"
                    % (
                        sheet_title,
                        ".".join(existing_path),
                        ".".join(path_tuple),
                    )
                )
        seen_paths.add(path_tuple)
        headers[column] = path

    if not headers:
        raise ConversionError(
            "worksheet '%s' header row contains no fields" % sheet_title
        )
    return headers


def parse_json_string(value, sheet_title, row_number, field_name):
    """Parse array/object text while preserving ordinary string values."""
    if not isinstance(value, str):
        return value
    stripped = value.strip()
    if not stripped.startswith(("[", "{")):
        return value
    try:
        parsed = json.loads(stripped)
    except json.JSONDecodeError as error:
        raise ConversionError(
            "worksheet '%s' row %d field '%s' contains invalid JSON: %s"
            % (sheet_title, row_number, field_name, error.msg)
        ) from error
    if not isinstance(parsed, (list, dict)):
        return value
    return parsed


def assign_nested_value(record, path, value):
    """Assign a value to a dotted field path in a row object."""
    target = record
    for segment in path[:-1]:
        child = target.get(segment)
        if child is None:
            child = {}
            target[segment] = child
        if not isinstance(child, dict):
            raise ConversionError(
                "field path '%s' conflicts with a non-object value"
                % ".".join(path)
            )
        target = child
    target[path[-1]] = value


def convert_sheet(archive, sheet, shared_strings, header_row, literal_strings):
    """Convert one worksheet to a list of row objects."""
    rows = read_sheet_rows(archive, sheet, shared_strings)
    header_values = None
    for row_number, values in rows:
        if row_number == header_row:
            header_values = values
            break
    if header_values is None:
        raise ConversionError(
            "worksheet '%s' does not contain header row %d"
            % (sheet["title"], header_row)
        )
    headers = read_headers(sheet["title"], header_values)

    records = []
    seen_ids = set()
    has_id_field = any(path == ["id"] for path in headers.values())
    for row_number, values in rows:
        if row_number <= header_row:
            continue
        active_values = [values.get(column) for column in headers]
        if all(value is None for value in active_values):
            continue

        record = {}
        for column, path in headers.items():
            value = values.get(column)
            if not literal_strings:
                value = parse_json_string(
                    value, sheet["title"], row_number, ".".join(path)
                )
            assign_nested_value(record, path, value)

        if has_id_field:
            record_id = record.get("id")
            if not isinstance(record_id, str) or not record_id.strip():
                raise ConversionError(
                    "worksheet '%s' row %d requires a non-empty string id"
                    % (sheet["title"], row_number)
                )
            if record_id in seen_ids:
                raise ConversionError(
                    "worksheet '%s' row %d duplicates id '%s'"
                    % (sheet["title"], row_number, record_id)
                )
            seen_ids.add(record_id)
        records.append(record)
    return records


def normalize_root_key(sheet_title):
    """Convert a worksheet title to a JSON root key."""
    normalized = re.sub(r"[^A-Za-z0-9]+", "_", sheet_title).strip("_").lower()
    if not normalized or not ROOT_KEY_PATTERN.fullmatch(normalized):
        raise ConversionError(
            "worksheet name '%s' cannot become a root key; pass --root-key"
            % sheet_title
        )
    return normalized


def select_sheets(sheets, requested_sheet, all_sheets):
    """Select workbook sheets according to command-line options."""
    if requested_sheet:
        for sheet in sheets:
            if sheet["title"] == requested_sheet:
                return [sheet]
        available = ", ".join(sheet["title"] for sheet in sheets)
        raise ConversionError(
            "worksheet '%s' was not found; available sheets: %s"
            % (requested_sheet, available)
        )

    visible_sheets = [sheet for sheet in sheets if sheet["state"] == "visible"]
    if not visible_sheets:
        raise ConversionError("workbook contains no visible worksheets")
    if all_sheets:
        return visible_sheets
    return [visible_sheets[0]]


def convert_workbook(input_path, args):
    """Read an Excel workbook and return its target JSON object and row count."""
    try:
        with zipfile.ZipFile(input_path, "r") as archive:
            sheets = read_workbook_sheets(archive)
            selected_sheets = select_sheets(
                sheets, args.sheet, args.all_sheets
            )
            shared_strings = read_shared_strings(archive)
            converted = {}
            total_rows = 0
            for sheet in selected_sheets:
                records = convert_sheet(
                    archive,
                    sheet,
                    shared_strings,
                    args.header_row,
                    args.literal_strings,
                )
                converted[sheet["title"]] = records
                total_rows += len(records)
    except zipfile.BadZipFile as error:
        raise ConversionError("input is not a valid .xlsx/.xlsm workbook") from error

    if args.all_sheets:
        document = {
            "schema_version": args.schema_version,
            "sheets": converted,
        }
    else:
        sheet_title = selected_sheets[0]["title"]
        root_key = args.root_key or normalize_root_key(sheet_title)
        document = {
            "schema_version": args.schema_version,
            root_key: converted[sheet_title],
        }
    return document, total_rows, len(selected_sheets)


def render_json(document, indent):
    """Serialize a JSON document with stable UTF-8-friendly formatting."""
    return json.dumps(
        document,
        ensure_ascii=False,
        indent=indent if indent > 0 else None,
        allow_nan=False,
    ) + "\n"


def write_output(output_path, content, force):
    """Write JSON atomically, refusing accidental replacement by default."""
    output_path.parent.mkdir(parents=True, exist_ok=True)
    if output_path.exists() and not force:
        raise ConversionError(
            "output '%s' already exists; pass --force to replace it" % output_path
        )

    temporary_path = None
    try:
        with tempfile.NamedTemporaryFile(
            mode="w",
            encoding="utf-8",
            newline="\n",
            prefix=".%s." % output_path.name,
            suffix=".tmp",
            dir=str(output_path.parent),
            delete=False,
        ) as temporary_file:
            temporary_file.write(content)
            temporary_path = Path(temporary_file.name)
        os.replace(str(temporary_path), str(output_path))
    except OSError:
        if temporary_path is not None:
            try:
                temporary_path.unlink()
            except OSError:
                pass
        raise


def resolve_output_path(args):
    """Resolve the destination while preserving '-' as standard output."""
    if args.output is not None:
        return args.output
    return args.input.with_suffix(".json")


def run(args):
    """Execute one conversion and return a process exit status."""
    input_path = args.input
    if input_path.suffix.lower() not in (".xlsx", ".xlsm"):
        raise ConversionError("input must use the .xlsx or .xlsm extension")
    if not input_path.is_file():
        raise ConversionError("input '%s' does not exist or is not a file" % input_path)

    document, row_count, sheet_count = convert_workbook(input_path, args)
    content = render_json(document, args.indent)
    output_path = resolve_output_path(args)
    if str(output_path) == "-":
        sys.stdout.write(content)
    else:
        if output_path.resolve() == input_path.resolve():
            raise ConversionError("output path must not replace the input workbook")
        write_output(output_path, content, args.force)
        print(
            "Converted %d row(s) from %d sheet(s) to %s"
            % (row_count, sheet_count, output_path)
        )
    return EXIT_OK


def main(argv=None):
    """Command-line entry point."""
    args = parse_arguments(argv)
    try:
        return run(args)
    except (ConversionError, OSError) as error:
        print("excel_to_json: %s" % error, file=sys.stderr)
        return EXIT_FAILED


if __name__ == "__main__":
    sys.exit(main())
