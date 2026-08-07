extends Node

signal load_completed(success: bool, errors: Array)

const STATIC_DATA_REGISTRY_SCRIPT = preload("res://scripts/data/static_data_registry.gd")

var _registry: StaticDataRegistry = STATIC_DATA_REGISTRY_SCRIPT.new()


func _ready() -> void:
	reload_data()


func reload_data() -> bool:
	var success := _registry.load_all()
	var errors := _registry.get_errors()
	for message in errors:
		push_error(message)
	load_completed.emit(success, errors)
	return success


func is_loaded() -> bool:
	return _registry.is_loaded()


func get_errors() -> Array[String]:
	return _registry.get_errors()


func get_record_count(table_name: String) -> int:
	return _registry.get_record_count(table_name)


func has_record(table_name: String, record_id: String) -> bool:
	return _registry.has_record(table_name, record_id)


func get_record(table_name: String, record_id: String) -> Dictionary:
	return _registry.get_record(table_name, record_id)


func get_records(table_name: String) -> Array:
	return _registry.get_records(table_name)


func get_root_value(table_name: String, key: String, default_value: Variant = null) -> Variant:
	return _registry.get_root_value(table_name, key, default_value)
