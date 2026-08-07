#!/usr/bin/env bash
#
# Run example:
#   ./scripts/generate_client_data.sh
# Command description:
#   Convert all shared Excel tables into validated client and server JSON files.

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO_ROOT=$(cd "${SCRIPT_DIR}/.." && pwd)
TOOL="${REPO_ROOT}/client/tools/excel_to_json.py"
TABLE_DIR="${REPO_ROOT}/share/tables"
CLIENT_OUTPUT_DIR="${REPO_ROOT}/client/data"
SERVER_OUTPUT_DIR="${REPO_ROOT}/server/data"
TARGETS=(client server)
WORKBOOKS=()
OUTPUT_NAMES=()

usage() {
  cat <<'EOF'
Usage:
  ./scripts/generate_client_data.sh

This command converts all Excel source tables under share/tables, filters fields
using the ownership values in row 2, validates the generated documents and
cross-table references, then replaces the corresponding JSON files under
client/data and server/data.

The command does not start Docker. Run it inside /workspace in the dev container,
or in an environment with Python 3 available.
EOF
}

if [ "${1:-}" = "-h" ] || [ "${1:-}" = "--help" ]; then
  usage
  exit 0
fi

if [ "$#" -ne 0 ]; then
  usage >&2
  exit 2
fi

if ! command -v python3 >/dev/null 2>&1; then
  echo "python3 is required" >&2
  exit 1
fi

if [ ! -f "${TOOL}" ]; then
  echo "required file is missing: ${TOOL}" >&2
  exit 1
fi

shopt -s nullglob
workbook_candidates=("${TABLE_DIR}"/*.xlsx "${TABLE_DIR}"/*.xlsm)
shopt -u nullglob

declare -A output_sources=()
for workbook in "${workbook_candidates[@]}"; do
  workbook_file=${workbook##*/}
  if [[ "${workbook_file}" == '~$'* ]]; then
    continue
  fi
  output_name=${workbook_file%.*}
  if [[ -n "${output_sources[${output_name}]+present}" ]]; then
    echo "multiple workbooks would generate ${output_name}.json:" >&2
    echo "  ${output_sources[${output_name}]}" >&2
    echo "  ${workbook}" >&2
    exit 1
  fi
  output_sources[${output_name}]=${workbook}
  WORKBOOKS+=("${workbook}")
  OUTPUT_NAMES+=("${output_name}")
done

if [ "${#WORKBOOKS[@]}" -eq 0 ]; then
  echo "no .xlsx or .xlsm workbooks found under ${TABLE_DIR}" >&2
  exit 1
fi

TEMP_DIR=$(mktemp -d "${TMPDIR:-/tmp}/abe_runtime_data.XXXXXX")
mkdir -p "${TEMP_DIR}/client" "${TEMP_DIR}/server"

cleanup() {
  local name
  local target
  local temporary_file

  for target in "${TARGETS[@]}"; do
    for name in "${OUTPUT_NAMES[@]}"; do
      temporary_file="${TEMP_DIR}/${target}/${name}.json"
      if [ -f "${temporary_file}" ]; then
        unlink "${temporary_file}"
      fi
    done
    if [ -d "${TEMP_DIR}/${target}" ]; then
      rmdir "${TEMP_DIR}/${target}" 2>/dev/null || true
    fi
  done
  if [ -d "${TEMP_DIR}" ]; then
    rmdir "${TEMP_DIR}" 2>/dev/null || true
  fi
}
trap cleanup EXIT

cd "${REPO_ROOT}"

for index in "${!WORKBOOKS[@]}"; do
  workbook=${WORKBOOKS[${index}]}
  output_name=${OUTPUT_NAMES[${index}]}
  workbook_info=()
  mapfile -t workbook_info < <(
    PYTHONDONTWRITEBYTECODE=1 python3 - "${TOOL}" "${workbook}" <<'PY'
import importlib.util
import sys
import zipfile


tool_path = sys.argv[1]
workbook_path = sys.argv[2]
spec = importlib.util.spec_from_file_location("excel_to_json", tool_path)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

with zipfile.ZipFile(workbook_path, "r") as archive:
    sheets = module.read_workbook_sheets(archive)

document_sheets = [sheet for sheet in sheets if sheet["title"] == "document"]
data_sheets = [
    sheet
    for sheet in sheets
    if sheet["state"] == "visible" and sheet["title"] != "document"
]
if len(document_sheets) > 1:
    raise ValueError("workbook contains duplicate document worksheets")
if len(data_sheets) != 1:
    raise ValueError(
        "workbook must contain exactly one visible data worksheet; found %d"
        % len(data_sheets)
    )

print(data_sheets[0]["title"])
print("document" if document_sheets else "")
PY
  )
  data_sheet=${workbook_info[0]:-}
  document_sheet=${workbook_info[1]:-}
  if [ -z "${data_sheet}" ]; then
    echo "failed to identify the data worksheet in ${workbook}" >&2
    exit 1
  fi

  conversion_args=(
    "${workbook}"
    --sheet "${data_sheet}"
    --header-row 4
    --data-row 5
    --scope-row 2
  )
  if [ -n "${document_sheet}" ]; then
    conversion_args+=(--document-sheet "${document_sheet}")
  fi
  for target in "${TARGETS[@]}"; do
    python3 "${TOOL}" "${conversion_args[@]}" \
      --target "${target}" \
      --output "${TEMP_DIR}/${target}/${output_name}.json"
  done
done

python3 - "${TEMP_DIR}" <<'PY'
import json
import sys
from pathlib import Path


def require(condition, message):
    if not condition:
        raise ValueError(message)


def read_document(directory, name):
    path = directory / (name + ".json")
    with path.open(encoding="utf-8") as stream:
        document = json.load(stream)
    require(document.get("schema_version") == 1, "%s has invalid schema_version" % path)
    return document


def collect_ids(rows, label):
    identifiers = []
    for row_number, row in enumerate(rows, 1):
        identifier = row.get("id")
        require(
            isinstance(identifier, str) and identifier,
            "%s row %d has an invalid id" % (label, row_number),
        )
        identifiers.append(identifier)
    require(
        len(identifiers) == len(set(identifiers)),
        "%s contains duplicate ids" % label,
    )
    return set(identifiers)


def validate_directory(directory):
    documents = {}
    for path in sorted(directory.glob("*.json")):
        document = read_document(directory, path.stem)
        documents[path.stem] = document
        record_arrays = [
            (key, value)
            for key, value in document.items()
            if key != "schema_version"
            and isinstance(value, list)
            and (not value or all(isinstance(row, dict) for row in value))
        ]
        require(
            len(record_arrays) == 1,
            "%s must contain exactly one root data array" % path,
        )
        root_key, records = record_arrays[0]
        if records and all("id" in record for record in records):
            collect_ids(records, "%s.%s" % (path.name, root_key))

    core_names = {"items", "rules", "enemies", "explore_nodes", "story_nodes"}
    if core_names.issubset(documents):
        items = documents["items"]
        rules = documents["rules"]
        enemies = documents["enemies"]
        explore = documents["explore_nodes"]
        stories = documents["story_nodes"]

        item_ids = collect_ids(items["items"], "items")
        rule_ids = collect_ids(rules["rules"], "rules")
        enemy_ids = collect_ids(enemies["enemies"], "enemies")
        node_ids = collect_ids(explore["nodes"], "explore nodes")
        story_ids = collect_ids(stories["story_nodes"], "story nodes")

        require(
            explore.get("start_node") in node_ids,
            "start_node references an unknown node",
        )
        if "p1_start_node" in explore:
            require(
                explore["p1_start_node"] in node_ids,
                "p1_start_node references an unknown node",
            )
        if "p1_route" in explore:
            require(
                isinstance(explore["p1_route"], list)
                and set(explore["p1_route"]).issubset(node_ids),
                "p1_route references an unknown node",
            )

        for node in explore["nodes"]:
            encounter_id = node.get("encounter_id")
            require(
                encounter_id is None or encounter_id in enemy_ids,
                "node '%s' references unknown encounter '%s'"
                % (node["id"], encounter_id),
            )
            require(
                isinstance(node.get("rewards"), list)
                and set(node["rewards"]).issubset(item_ids),
                "node '%s' references an unknown reward" % node["id"],
            )
            require(
                isinstance(node.get("next_nodes"), list)
                and set(node["next_nodes"]).issubset(node_ids),
                "node '%s' references an unknown next node" % node["id"],
            )

        for story in stories["story_nodes"]:
            require(
                isinstance(story.get("next_nodes"), list)
                and set(story["next_nodes"]).issubset(story_ids),
                "story '%s' references an unknown next node" % story["id"],
            )
            entries = story.get("requirements", []) + story.get("effects", [])
            for entry in entries:
                entry_type = entry.get("type")
                if entry_type in ("rule_unlocked", "unlock_rule"):
                    require(
                        entry.get("id") in rule_ids,
                        "story '%s' references an unknown rule" % story["id"],
                    )
                if entry_type == "item_owned":
                    require(
                        entry.get("id") in item_ids,
                        "story '%s' references an unknown item" % story["id"],
                    )
    return documents


temporary_directory = Path(sys.argv[1])
client_documents = validate_directory(temporary_directory / "client")
server_documents = validate_directory(temporary_directory / "server")
require(
    set(client_documents) == set(server_documents),
    "client and server generated different JSON file sets",
)
print("Validated client/server JSON and cross-table references.")
PY

mkdir -p "${CLIENT_OUTPUT_DIR}" "${SERVER_OUTPUT_DIR}"
for target in "${TARGETS[@]}"; do
  if [ "${target}" = "client" ]; then
    target_output_dir=${CLIENT_OUTPUT_DIR}
  else
    target_output_dir=${SERVER_OUTPUT_DIR}
  fi
  for name in "${OUTPUT_NAMES[@]}"; do
    mv -f "${TEMP_DIR}/${target}/${name}.json" \
      "${target_output_dir}/${name}.json"
  done
done

echo "Generated ${#OUTPUT_NAMES[@]} validated JSON files for client and server."
