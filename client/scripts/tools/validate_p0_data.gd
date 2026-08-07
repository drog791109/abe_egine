extends SceneTree

const STATIC_DATA_REGISTRY_SCRIPT = preload("res://scripts/data/static_data_registry.gd")
const EXPECTED_COUNTS: Dictionary = {
	"items": 28,
	"rules": 16,
	"enemies": 9,
	"explore_nodes": 11,
	"story_nodes": 14,
}


func _init() -> void:
	var registry: StaticDataRegistry = STATIC_DATA_REGISTRY_SCRIPT.new()
	if not registry.load_all():
		for message in registry.get_errors():
			push_error(message)
		quit(1)
		return

	for table_name in EXPECTED_COUNTS:
		var expected_count: int = EXPECTED_COUNTS[table_name]
		var actual_count := registry.get_record_count(table_name)
		if actual_count != expected_count:
			push_error(
				"%s: expected %d P0 records, got %d"
				% [table_name, expected_count, actual_count]
			)
			quit(1)
			return

	print("P0 data validation passed: 78 records across 5 tables.")
	quit(0)
