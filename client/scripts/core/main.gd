extends Control

const SUCCESS_COLOR := Color("6bd6a0")
const ERROR_COLOR := Color("ff766d")

@onready var status_label: Label = %StatusLabel
@onready var detail_label: Label = %DetailLabel
@onready var items_count: Label = %ItemsCount
@onready var rules_count: Label = %RulesCount
@onready var enemies_count: Label = %EnemiesCount
@onready var explore_nodes_count: Label = %ExploreNodesCount
@onready var story_nodes_count: Label = %StoryNodesCount
@onready var reload_button: Button = %ReloadButton


func _ready() -> void:
	StaticData.load_completed.connect(_on_static_data_load_completed)
	reload_button.pressed.connect(_on_reload_button_pressed)
	_render_data_state()


func _on_reload_button_pressed() -> void:
	StaticData.reload_data()


func _on_static_data_load_completed(_success: bool, _errors: Array) -> void:
	_render_data_state()


func _render_data_state() -> void:
	items_count.text = str(StaticData.get_record_count("items"))
	rules_count.text = str(StaticData.get_record_count("rules"))
	enemies_count.text = str(StaticData.get_record_count("enemies"))
	explore_nodes_count.text = str(StaticData.get_record_count("explore_nodes"))
	story_nodes_count.text = str(StaticData.get_record_count("story_nodes"))

	var errors := StaticData.get_errors()
	if StaticData.is_loaded():
		status_label.text = "数据合同有效"
		status_label.add_theme_color_override("font_color", SUCCESS_COLOR)
		detail_label.text = "5 张静态表已建立 ID 索引，P0 工程入口可用。"
		reload_button.text = "重新校验"
		return

	status_label.text = "数据合同无效"
	status_label.add_theme_color_override("font_color", ERROR_COLOR)
	detail_label.text = "未发现具体错误。"
	if not errors.is_empty():
		detail_label.text = errors[0]
	reload_button.text = "重试校验"
