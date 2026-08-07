class_name StaticDataRegistry
extends RefCounted

const EXPECTED_SCHEMA_VERSION: int = 1
const TABLE_ORDER: Array[String] = [
	"items",
	"rules",
	"enemies",
	"explore_nodes",
	"story_nodes",
]
const TABLE_SPECS: Dictionary = {
	"items": {
		"path": "res://data/items.json",
		"root_key": "items",
	},
	"rules": {
		"path": "res://data/rules.json",
		"root_key": "rules",
	},
	"enemies": {
		"path": "res://data/enemies.json",
		"root_key": "enemies",
	},
	"explore_nodes": {
		"path": "res://data/explore_nodes.json",
		"root_key": "nodes",
	},
	"story_nodes": {
		"path": "res://data/story_nodes.json",
		"root_key": "story_nodes",
	},
}
const REQUIRED_FIELDS: Dictionary = {
	"items": {
		"id": TYPE_STRING,
		"name": TYPE_STRING,
		"category": TYPE_STRING,
		"rarity": TYPE_STRING,
		"shape": TYPE_ARRAY,
		"rotatable": TYPE_BOOL,
		"tags": TYPE_ARRAY,
		"base_effects": TYPE_ARRAY,
		"source_stage": TYPE_STRING,
		"source_location": TYPE_STRING,
		"design_note": TYPE_STRING,
	},
	"rules": {
		"id": TYPE_STRING,
		"name": TYPE_STRING,
		"category": TYPE_STRING,
		"trigger": TYPE_DICTIONARY,
		"conditions": TYPE_ARRAY,
		"effects": TYPE_ARRAY,
		"costs": TYPE_ARRAY,
		"limits": TYPE_DICTIONARY,
		"ui_hint": TYPE_STRING,
		"p1_priority": TYPE_BOOL,
	},
	"enemies": {
		"id": TYPE_STRING,
		"name": TYPE_STRING,
		"rank": TYPE_STRING,
		"location": TYPE_STRING,
		"tags": TYPE_ARRAY,
		"stats": TYPE_DICTIONARY,
		"skills": TYPE_ARRAY,
		"loot_tags": TYPE_ARRAY,
		"p1_priority": TYPE_BOOL,
		"design_note": TYPE_STRING,
	},
	"explore_nodes": {
		"id": TYPE_STRING,
		"name": TYPE_STRING,
		"location": TYPE_STRING,
		"node_type": TYPE_STRING,
		"rewards": TYPE_ARRAY,
		"next_nodes": TYPE_ARRAY,
		"p1_priority": TYPE_BOOL,
	},
	"story_nodes": {
		"id": TYPE_STRING,
		"title": TYPE_STRING,
		"location": TYPE_STRING,
		"speaker": TYPE_STRING,
		"summary": TYPE_STRING,
		"requirements": TYPE_ARRAY,
		"effects": TYPE_ARRAY,
		"next_nodes": TYPE_ARRAY,
	},
}
const ITEM_CATEGORIES: Array[String] = ["array", "material", "pill", "talisman", "weapon"]
const ITEM_RARITIES: Array[String] = ["common", "uncommon", "rare"]
const ITEM_SOURCE_STAGES: Array[String] = ["P1", "P2"]
const LOCATIONS: Array[String] = [
	"xuanli_cellar",
	"taixuan_sect",
	"underground_vein",
	"huanggu_city",
	"luoxing_valley",
]
const ITEM_EFFECT_TYPES: Array[String] = [
	"adjacent_rule_power",
	"apply_armor_break",
	"armor_break_power",
	"attack",
	"attack_when_low_health",
	"chain_damage",
	"defense",
	"first_strike",
	"heal",
	"heal_over_time",
	"healing_power",
	"linked_spirit_gain",
	"loot_mark",
	"repeat_linked_trigger",
	"rule_power",
	"shield",
	"speed",
	"spirit_capacity",
	"spirit_damage",
	"spirit_gain",
	"stun",
	"thunder_damage",
	"trigger_count",
]
const RULE_CATEGORIES: Array[String] = ["condition", "cost", "modifier", "transform", "trigger"]
const RULE_TRIGGER_EVENTS: Array[String] = [
	"actor_damaged",
	"battle_start",
	"battle_won",
	"enemy_killed",
	"first_actor_attack",
	"pill_triggered",
	"rule_triggered",
	"status_applied",
	"talisman_triggered",
	"unstable_rule_triggered",
]
const RULE_CONDITION_TYPES: Array[String] = [
	"adjacent_item_count",
	"connected_item_count",
	"damage_amount",
	"health_ratio",
	"inventory_has_tag",
	"placed_on_cell",
	"same_row_item_count",
	"source_has_tag",
	"target_has_status",
]
const RULE_EFFECT_TYPES: Array[String] = [
	"convert_tag_to_resource",
	"extend_status",
	"gain_resource",
	"gain_shield",
	"grant_loot",
	"guarantee_rule_success",
	"immediate_attack",
	"increase_connected_power",
	"increase_item_power",
	"increase_rule_power",
	"increase_same_row_power",
	"increase_tagged_loot_chance",
	"multiply_item_effect",
	"repeat_attack",
	"schedule_repeat",
	"trigger_adjacent_item",
]
const RULE_COST_TYPES: Array[String] = [
	"disable_item_for_battle",
	"pollute_random_empty_cell",
	"spend_resource",
]
const RULE_LIMIT_KEYS: Array[String] = [
	"chain_depth",
	"per_battle",
	"per_enemy",
	"per_item",
	"per_target",
	"per_turn",
]
const ENEMY_RANKS: Array[String] = ["normal", "elite", "boss"]
const ENEMY_SKILL_IDS: Array[String] = [
	"array_barrier",
	"bite",
	"core_resonance",
	"corrupt_cell",
	"execute_mark",
	"first_cut",
	"ground_pulse",
	"guard_stance",
	"mark_preload",
	"mirror_last_rule",
	"star_burst",
	"steal_spirit",
	"sword_sweep",
]
const EXPLORE_NODE_TYPES: Array[String] = ["battle", "boss", "choice", "rest", "story", "tutorial"]
const STORY_REQUIREMENT_TYPES: Array[String] = ["encounter_won", "flag", "item_owned", "rule_unlocked"]
const STORY_EFFECT_TYPES: Array[String] = [
	"set_companion",
	"set_flag",
	"start_encounter",
	"unlock_location",
	"unlock_rule",
]

var _documents: Dictionary = {}
var _records: Dictionary = {}
var _indexes: Dictionary = {}
var _errors: Array[String] = []
var _loaded: bool = false
var _id_pattern: RegEx = RegEx.new()


func _init() -> void:
	_id_pattern.compile("^[a-z][a-z0-9_]*$")


func load_all() -> bool:
	_documents.clear()
	_records.clear()
	_indexes.clear()
	_errors.clear()
	_loaded = false

	for table_name in TABLE_ORDER:
		_load_table(table_name)

	if _indexes.size() == TABLE_ORDER.size():
		_validate_cross_references()

	_loaded = _errors.is_empty()
	return _loaded


func is_loaded() -> bool:
	return _loaded


func get_errors() -> Array[String]:
	return _errors.duplicate()


func get_record_count(table_name: String) -> int:
	var table_records: Array = _records.get(table_name, [])
	return table_records.size()


func has_record(table_name: String, record_id: String) -> bool:
	var table_index: Dictionary = _indexes.get(table_name, {})
	return table_index.has(record_id)


func get_record(table_name: String, record_id: String) -> Dictionary:
	var table_index: Dictionary = _indexes.get(table_name, {})
	if not table_index.has(record_id):
		return {}
	return table_index[record_id].duplicate(true)


func get_records(table_name: String) -> Array:
	var table_records: Array = _records.get(table_name, [])
	return table_records.duplicate(true)


func get_root_value(table_name: String, key: String, default_value: Variant = null) -> Variant:
	var document: Dictionary = _documents.get(table_name, {})
	if not document.has(key):
		return default_value
	var value: Variant = document[key]
	if typeof(value) == TYPE_ARRAY or typeof(value) == TYPE_DICTIONARY:
		return value.duplicate(true)
	return value


func _load_table(table_name: String) -> void:
	var spec: Dictionary = TABLE_SPECS[table_name]
	var path: String = spec["path"]
	if not FileAccess.file_exists(path):
		_add_error("%s: file does not exist" % path)
		return

	var file: FileAccess = FileAccess.open(path, FileAccess.READ)
	if file == null:
		_add_error("%s: file cannot be opened" % path)
		return

	var parser: JSON = JSON.new()
	var parse_status: Error = parser.parse(file.get_as_text())
	if parse_status != OK:
		_add_error("%s:%d: %s" % [path, parser.get_error_line(), parser.get_error_message()])
		return

	var parsed: Variant = parser.data
	if typeof(parsed) != TYPE_DICTIONARY:
		_add_error("%s: root value must be an object" % path)
		return
	var document: Dictionary = parsed
	_validate_schema_version(path, document)

	var root_key: String = spec["root_key"]
	if not document.has(root_key) or typeof(document[root_key]) != TYPE_ARRAY:
		_add_error("%s.%s: required array is missing" % [path, root_key])
		return

	var table_records: Array = []
	var table_index: Dictionary = {}
	var source_records: Array = document[root_key]
	for record_index in range(source_records.size()):
		var record_path := "%s.%s[%d]" % [path, root_key, record_index]
		var value: Variant = source_records[record_index]
		if typeof(value) != TYPE_DICTIONARY:
			_add_error("%s: record must be an object" % record_path)
			continue
		var record: Dictionary = value
		_validate_required_fields(table_name, record, record_path)
		_validate_record(table_name, record, record_path)
		table_records.append(record.duplicate(true))

		var record_id: Variant = record.get("id")
		if typeof(record_id) != TYPE_STRING or String(record_id).is_empty():
			continue
		if table_index.has(record_id):
			_add_error("%s.id: duplicate id '%s'" % [record_path, record_id])
			continue
		table_index[record_id] = record.duplicate(true)

	_documents[table_name] = document.duplicate(true)
	_records[table_name] = table_records
	_indexes[table_name] = table_index

	if table_name == "explore_nodes":
		_validate_explore_metadata(path, document)


func _validate_schema_version(path: String, document: Dictionary) -> void:
	if not document.has("schema_version"):
		_add_error("%s.schema_version: required field is missing" % path)
		return
	var value: Variant = document["schema_version"]
	if not _is_integer_number(value):
		_add_error("%s.schema_version: expected an integer" % path)
		return
	if int(value) != EXPECTED_SCHEMA_VERSION:
		_add_error(
			"%s.schema_version: expected %d, got %d"
			% [path, EXPECTED_SCHEMA_VERSION, int(value)]
		)


func _validate_required_fields(table_name: String, record: Dictionary, record_path: String) -> void:
	var required: Dictionary = REQUIRED_FIELDS[table_name]
	for field_name in required:
		if not record.has(field_name):
			_add_error("%s.%s: required field is missing" % [record_path, field_name])
			continue
		var expected_type: int = required[field_name]
		if typeof(record[field_name]) != expected_type:
			_add_error("%s.%s: unexpected value type" % [record_path, field_name])


func _validate_record(table_name: String, record: Dictionary, record_path: String) -> void:
	_validate_id(record.get("id"), "%s.id" % record_path)
	match table_name:
		"items":
			_validate_item(record, record_path)
		"rules":
			_validate_rule(record, record_path)
		"enemies":
			_validate_enemy(record, record_path)
		"explore_nodes":
			_validate_explore_node(record, record_path)
		"story_nodes":
			_validate_story_node(record, record_path)


func _validate_item(record: Dictionary, record_path: String) -> void:
	_validate_nonempty_strings(record, ["name", "design_note"], record_path)
	_validate_enum(record.get("category"), ITEM_CATEGORIES, "%s.category" % record_path)
	_validate_enum(record.get("rarity"), ITEM_RARITIES, "%s.rarity" % record_path)
	_validate_enum(record.get("source_stage"), ITEM_SOURCE_STAGES, "%s.source_stage" % record_path)
	_validate_enum(record.get("source_location"), LOCATIONS, "%s.source_location" % record_path)
	_validate_shape(record.get("shape"), "%s.shape" % record_path)
	_validate_string_array(record.get("tags"), "%s.tags" % record_path)
	_validate_typed_entries(
		record.get("base_effects"), ITEM_EFFECT_TYPES, "%s.base_effects" % record_path
	)


func _validate_rule(record: Dictionary, record_path: String) -> void:
	_validate_nonempty_strings(record, ["name", "ui_hint"], record_path)
	_validate_enum(record.get("category"), RULE_CATEGORIES, "%s.category" % record_path)
	var trigger: Variant = record.get("trigger")
	if typeof(trigger) == TYPE_DICTIONARY:
		if not trigger.has("event"):
			_add_error("%s.trigger.event: required field is missing" % record_path)
		elif typeof(trigger["event"]) != TYPE_STRING or String(trigger["event"]).is_empty():
			_add_error("%s.trigger.event: expected a non-empty string" % record_path)
		else:
			_validate_enum(trigger.get("event"), RULE_TRIGGER_EVENTS, "%s.trigger.event" % record_path)
		if trigger.has("status") and typeof(trigger["status"]) != TYPE_STRING:
			_add_error("%s.trigger.status: expected a string" % record_path)
	_validate_typed_entries(
		record.get("conditions"), RULE_CONDITION_TYPES, "%s.conditions" % record_path
	)
	_validate_typed_entries(record.get("effects"), RULE_EFFECT_TYPES, "%s.effects" % record_path)
	_validate_typed_entries(record.get("costs"), RULE_COST_TYPES, "%s.costs" % record_path)
	_validate_nonnegative_integer_values(
		record.get("limits"), RULE_LIMIT_KEYS, "%s.limits" % record_path
	)


func _validate_enemy(record: Dictionary, record_path: String) -> void:
	_validate_nonempty_strings(record, ["name", "design_note"], record_path)
	_validate_enum(record.get("rank"), ENEMY_RANKS, "%s.rank" % record_path)
	_validate_enum(record.get("location"), LOCATIONS, "%s.location" % record_path)
	_validate_string_array(record.get("tags"), "%s.tags" % record_path)
	_validate_string_array(record.get("loot_tags"), "%s.loot_tags" % record_path)
	_validate_enemy_stats(record.get("stats"), "%s.stats" % record_path)
	_validate_enemy_skills(record.get("skills"), "%s.skills" % record_path)


func _validate_explore_node(record: Dictionary, record_path: String) -> void:
	_validate_nonempty_strings(record, ["name"], record_path)
	_validate_enum(record.get("location"), LOCATIONS, "%s.location" % record_path)
	_validate_enum(record.get("node_type"), EXPLORE_NODE_TYPES, "%s.node_type" % record_path)
	if not record.has("encounter_id"):
		_add_error("%s.encounter_id: required field is missing" % record_path)
	var encounter_id: Variant = record.get("encounter_id")
	if encounter_id != null and typeof(encounter_id) != TYPE_STRING:
		_add_error("%s.encounter_id: expected a string or null" % record_path)
	_validate_string_array(record.get("rewards"), "%s.rewards" % record_path)
	_validate_string_array(record.get("next_nodes"), "%s.next_nodes" % record_path)


func _validate_story_node(record: Dictionary, record_path: String) -> void:
	_validate_nonempty_strings(record, ["title", "location", "speaker", "summary"], record_path)
	_validate_enum(record.get("location"), LOCATIONS, "%s.location" % record_path)
	_validate_typed_entries(
		record.get("requirements"), STORY_REQUIREMENT_TYPES, "%s.requirements" % record_path
	)
	_validate_typed_entries(record.get("effects"), STORY_EFFECT_TYPES, "%s.effects" % record_path)
	_validate_story_entries(record.get("requirements"), "%s.requirements" % record_path)
	_validate_story_entries(record.get("effects"), "%s.effects" % record_path)
	_validate_string_array(record.get("next_nodes"), "%s.next_nodes" % record_path)


func _validate_explore_metadata(path: String, document: Dictionary) -> void:
	_validate_id(document.get("start_node"), "%s.start_node" % path)
	_validate_id(document.get("p1_start_node"), "%s.p1_start_node" % path)
	if not document.has("p1_route"):
		_add_error("%s.p1_route: required field is missing" % path)
	else:
		_validate_string_array(document.get("p1_route"), "%s.p1_route" % path)


func _validate_cross_references() -> void:
	var explore_document: Dictionary = _documents["explore_nodes"]
	_validate_reference(
		explore_document.get("start_node"), "explore_nodes", "explore_nodes.start_node"
	)
	_validate_reference(
		explore_document.get("p1_start_node"), "explore_nodes", "explore_nodes.p1_start_node"
	)
	_validate_reference_list(
		explore_document.get("p1_route"), "explore_nodes", "explore_nodes.p1_route"
	)

	var explore_records: Array = _records["explore_nodes"]
	for record_index in range(explore_records.size()):
		var record: Dictionary = explore_records[record_index]
		var path := "explore_nodes.nodes[%d]" % record_index
		var encounter_id: Variant = record.get("encounter_id")
		if encounter_id != null:
			_validate_reference(encounter_id, "enemies", "%s.encounter_id" % path)
		_validate_reference_list(record.get("rewards"), "items", "%s.rewards" % path)
		_validate_reference_list(record.get("next_nodes"), "explore_nodes", "%s.next_nodes" % path)

	var story_records: Array = _records["story_nodes"]
	for record_index in range(story_records.size()):
		var record: Dictionary = story_records[record_index]
		var path := "story_nodes.story_nodes[%d]" % record_index
		_validate_reference_list(record.get("next_nodes"), "story_nodes", "%s.next_nodes" % path)
		_validate_story_entry_references(record.get("requirements"), "%s.requirements" % path)
		_validate_story_entry_references(record.get("effects"), "%s.effects" % path)


func _validate_story_entry_references(entries: Variant, path: String) -> void:
	if typeof(entries) != TYPE_ARRAY:
		return
	for entry_index in range(entries.size()):
		var entry: Variant = entries[entry_index]
		if typeof(entry) != TYPE_DICTIONARY:
			continue
		var entry_type: Variant = entry.get("type")
		var target_table := ""
		if entry_type == "item_owned":
			target_table = "items"
		elif entry_type == "rule_unlocked" or entry_type == "unlock_rule":
			target_table = "rules"
		elif entry_type == "encounter_won" or entry_type == "start_encounter":
			target_table = "enemies"
		if not target_table.is_empty():
			_validate_reference(entry.get("id"), target_table, "%s[%d].id" % [path, entry_index])


func _validate_reference(value: Variant, target_table: String, path: String) -> void:
	if typeof(value) != TYPE_STRING or String(value).is_empty():
		_add_error("%s: expected a non-empty id" % path)
		return
	var target_index: Dictionary = _indexes.get(target_table, {})
	if not target_index.has(value):
		_add_error("%s: unknown %s id '%s'" % [path, target_table, value])


func _validate_reference_list(values: Variant, target_table: String, path: String) -> void:
	if typeof(values) != TYPE_ARRAY:
		return
	for value_index in range(values.size()):
		_validate_reference(values[value_index], target_table, "%s[%d]" % [path, value_index])


func _validate_id(value: Variant, path: String) -> void:
	if typeof(value) != TYPE_STRING or String(value).is_empty():
		_add_error("%s: expected a non-empty string" % path)
		return
	if _id_pattern.search(value) == null:
		_add_error("%s: expected a snake_case id, got '%s'" % [path, value])


func _validate_nonempty_strings(record: Dictionary, fields: Array, record_path: String) -> void:
	for field_name in fields:
		var value: Variant = record.get(field_name)
		if typeof(value) == TYPE_STRING and String(value).is_empty():
			_add_error("%s.%s: string must not be empty" % [record_path, field_name])


func _validate_enum(value: Variant, allowed_values: Array[String], path: String) -> void:
	if typeof(value) != TYPE_STRING:
		return
	if not allowed_values.has(value):
		_add_error("%s: unknown value '%s'" % [path, value])


func _validate_string_array(value: Variant, path: String) -> void:
	if typeof(value) != TYPE_ARRAY:
		return
	for value_index in range(value.size()):
		if typeof(value[value_index]) != TYPE_STRING or String(value[value_index]).is_empty():
			_add_error("%s[%d]: expected a non-empty string" % [path, value_index])


func _validate_typed_entries(value: Variant, allowed_types: Array[String], path: String) -> void:
	if typeof(value) != TYPE_ARRAY:
		return
	for entry_index in range(value.size()):
		var entry: Variant = value[entry_index]
		if typeof(entry) != TYPE_DICTIONARY:
			_add_error("%s[%d]: expected an object" % [path, entry_index])
			continue
		var entry_type: Variant = entry.get("type")
		_validate_enum(entry_type, allowed_types, "%s[%d].type" % [path, entry_index])
		if typeof(entry_type) != TYPE_STRING or String(entry_type).is_empty():
			_add_error("%s[%d].type: expected a non-empty string" % [path, entry_index])


func _validate_story_entries(value: Variant, path: String) -> void:
	if typeof(value) != TYPE_ARRAY:
		return
	for entry_index in range(value.size()):
		var entry: Variant = value[entry_index]
		if typeof(entry) != TYPE_DICTIONARY:
			continue
		var entry_type: Variant = entry.get("type")
		var entry_path := "%s[%d]" % [path, entry_index]
		if entry_type == "flag" or entry_type == "set_flag":
			if typeof(entry.get("key")) != TYPE_STRING or String(entry.get("key")).is_empty():
				_add_error("%s.key: expected a non-empty string" % entry_path)
			if not entry.has("value"):
				_add_error("%s.value: required field is missing" % entry_path)
		elif entry_type == "unlock_location":
			_validate_id(entry.get("id"), "%s.id" % entry_path)
			_validate_enum(entry.get("id"), LOCATIONS, "%s.id" % entry_path)
		elif entry_type == "set_companion":
			_validate_id(entry.get("id"), "%s.id" % entry_path)
			if typeof(entry.get("active")) != TYPE_BOOL:
				_add_error("%s.active: expected a boolean" % entry_path)
		elif entry_type in [
			"encounter_won", "item_owned", "rule_unlocked", "start_encounter", "unlock_rule"
		]:
			_validate_id(entry.get("id"), "%s.id" % entry_path)


func _validate_shape(value: Variant, path: String) -> void:
	if typeof(value) != TYPE_ARRAY:
		return
	if value.is_empty():
		_add_error("%s: shape must contain at least one cell" % path)
		return
	var occupied: Dictionary = {}
	var min_x: int = 2147483647
	var min_y: int = 2147483647
	for cell_index in range(value.size()):
		var cell: Variant = value[cell_index]
		if typeof(cell) != TYPE_ARRAY or cell.size() != 2:
			_add_error("%s[%d]: expected [x, y]" % [path, cell_index])
			continue
		if not _is_nonnegative_integer(cell[0]) or not _is_nonnegative_integer(cell[1]):
			_add_error("%s[%d]: coordinates must be non-negative integers" % [path, cell_index])
			continue
		var x := int(cell[0])
		var y := int(cell[1])
		var coordinate_key := "%d:%d" % [x, y]
		if occupied.has(coordinate_key):
			_add_error("%s[%d]: duplicate coordinate [%d, %d]" % [path, cell_index, x, y])
		occupied[coordinate_key] = true
		min_x = mini(min_x, x)
		min_y = mini(min_y, y)
	if not occupied.is_empty() and (min_x != 0 or min_y != 0):
		_add_error("%s: coordinates must be normalized to the top-left origin" % path)


func _validate_nonnegative_integer_values(
	value: Variant, allowed_keys: Array[String], path: String
) -> void:
	if typeof(value) != TYPE_DICTIONARY:
		return
	for key in value:
		if not allowed_keys.has(key):
			_add_error("%s.%s: unknown limit" % [path, key])
		if not _is_nonnegative_integer(value[key]):
			_add_error("%s.%s: expected a non-negative integer" % [path, key])


func _validate_enemy_stats(value: Variant, path: String) -> void:
	if typeof(value) != TYPE_DICTIONARY:
		return
	for stat_name in ["hp", "attack", "defense", "speed"]:
		if not value.has(stat_name):
			_add_error("%s.%s: required field is missing" % [path, stat_name])
		elif not _is_nonnegative_number(value[stat_name]):
			_add_error("%s.%s: expected a non-negative number" % [path, stat_name])


func _validate_enemy_skills(value: Variant, path: String) -> void:
	if typeof(value) != TYPE_ARRAY:
		return
	for skill_index in range(value.size()):
		var skill: Variant = value[skill_index]
		if typeof(skill) != TYPE_DICTIONARY:
			_add_error("%s[%d]: expected an object" % [path, skill_index])
			continue
		_validate_enum(skill.get("id"), ENEMY_SKILL_IDS, "%s[%d].id" % [path, skill_index])
		_validate_id(skill.get("id"), "%s[%d].id" % [path, skill_index])
		if not skill.has("cooldown") or not _is_nonnegative_integer(skill.get("cooldown")):
			_add_error("%s[%d].cooldown: expected a non-negative integer" % [path, skill_index])


func _is_integer_number(value: Variant) -> bool:
	if typeof(value) != TYPE_INT and typeof(value) != TYPE_FLOAT:
		return false
	return float(value) == floorf(float(value))


func _is_nonnegative_integer(value: Variant) -> bool:
	return _is_integer_number(value) and float(value) >= 0.0


func _is_nonnegative_number(value: Variant) -> bool:
	return (typeof(value) == TYPE_INT or typeof(value) == TYPE_FLOAT) and float(value) >= 0.0


func _add_error(message: String) -> void:
	_errors.append(message)
