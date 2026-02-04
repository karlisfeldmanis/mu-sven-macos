extends Control

## Light Control Panel
## Runtime UI for adjusting DirectionalLight shadow settings

var _sun: DirectionalLight3D
var _is_visible: bool = true

# Slider references
var _pitch_slider: HSlider
var _yaw_slider: HSlider
var _energy_slider: HSlider
var _shadow_bias_slider: HSlider
var _shadow_normal_bias_slider: HSlider
var _shadow_distance_slider: HSlider

func _ready():
	_setup_panel()
	print("[LightControl] Press 'L' to toggle light control panel")

func setup_sun(sun: DirectionalLight3D):
	_sun = sun
	if _sun:
		print("[LightControl] Connected to DirectionalLight3D")
		_update_sliders_from_light()

func _setup_panel():
	# Use CanvasLayer to ensure UI is always on top
	var canvas_layer = CanvasLayer.new()
	canvas_layer.layer = 100  # High layer to ensure it's on top
	add_child(canvas_layer)
	
	# Panel background
	var panel = PanelContainer.new()
	panel.set_anchors_and_offsets_preset(Control.PRESET_TOP_RIGHT)
	panel.offset_left = -320
	panel.offset_top = 10
	panel.offset_right = -10
	panel.offset_bottom = 450
	canvas_layer.add_child(panel)
	
	var vbox = VBoxContainer.new()
	vbox.set("theme_override_constants/separation", 8)
	panel.add_child(vbox)
	
	# Title
	var title = Label.new()
	title.text = "Light & Shadow Controls (Press L to hide)"
	title.add_theme_font_size_override("font_size", 14)
	vbox.add_child(title)
	
	# Pitch slider
	_pitch_slider = _create_slider(vbox, "Sun Pitch", -90, 90, 1, -45)
	_pitch_slider.value_changed.connect(_on_pitch_changed)
	
	# Yaw slider
	_yaw_slider = _create_slider(vbox, "Sun Yaw", -180, 180, 1, 45)
	_yaw_slider.value_changed.connect(_on_yaw_changed)
	
	# Energy slider
	_energy_slider = _create_slider(vbox, "Light Energy", 0.0, 3.0, 0.1, 1.2)
	_energy_slider.value_changed.connect(_on_energy_changed)
	
	# Shadow bias
	_shadow_bias_slider = _create_slider(vbox, "Shadow Bias", 0.0, 1.0, 0.01, 0.05)
	_shadow_bias_slider.value_changed.connect(_on_shadow_bias_changed)
	
	# Shadow normal bias
	_shadow_normal_bias_slider = _create_slider(vbox, "Shadow Normal Bias", 0.0, 5.0, 0.1, 1.0)
	_shadow_normal_bias_slider.value_changed.connect(_on_shadow_normal_bias_changed)
	
	# Shadow distance
	_shadow_distance_slider = _create_slider(vbox, "Shadow Distance", 10, 1000, 10, 500)
	_shadow_distance_slider.value_changed.connect(_on_shadow_distance_changed)
	
	# Info label
	var info = Label.new()
	info.text = "Adjust sliders to tune shadows.\nChanges apply immediately."
	info.add_theme_font_size_override("font_size", 10)
	vbox.add_child(info)

func _create_slider(parent: VBoxContainer, label_text: String, min_val: float, max_val: float, step: float, default: float) -> HSlider:
	var label = Label.new()
	label.text = label_text + ": " + str(default)
	parent.add_child(label)
	
	var slider = HSlider.new()
	slider.min_value = min_val
	slider.max_value = max_val
	slider.step = step
	slider.value = default
	slider.set_custom_minimum_size(Vector2(280, 24))
	slider.set_meta("label", label)
	parent.add_child(slider)
	
	return slider

func _update_slider_label(slider: HSlider, value: float):
	var label = slider.get_meta("label") as Label
	if label:
		var label_text = label.text.split(":")[0]
		label.text = label_text + ": " + ("%.2f" % value)

func _update_sliders_from_light():
	if not _sun:
		return
	
	var euler = _sun.rotation_degrees
	_pitch_slider.value = euler.x
	_yaw_slider.value = euler.y
	_energy_slider.value = _sun.light_energy
	_shadow_bias_slider.value = _sun.shadow_bias
	_shadow_normal_bias_slider.value = _sun.shadow_normal_bias
	_shadow_distance_slider.value = _sun.directional_shadow_max_distance

func _on_pitch_changed(value: float):
	_update_slider_label(_pitch_slider, value)
	if _sun:
		_sun.rotation_degrees.x = value

func _on_yaw_changed(value: float):
	_update_slider_label(_yaw_slider, value)
	if _sun:
		_sun.rotation_degrees.y = value

func _on_energy_changed(value: float):
	_update_slider_label(_energy_slider, value)
	if _sun:
		_sun.light_energy = value

func _on_shadow_bias_changed(value: float):
	_update_slider_label(_shadow_bias_slider, value)
	if _sun:
		_sun.shadow_bias = value

func _on_shadow_normal_bias_changed(value: float):
	_update_slider_label(_shadow_normal_bias_slider, value)
	if _sun:
		_sun.shadow_normal_bias = value

func _on_shadow_distance_changed(value: float):
	_update_slider_label(_shadow_distance_slider, value)
	if _sun:
		_sun.directional_shadow_max_distance = value

func _input(event: InputEvent):
	if event is InputEventKey and event.pressed and event.keycode == KEY_L:
		_is_visible = !_is_visible
		visible = _is_visible
		print("[LightControl] Panel ", "visible" if _is_visible else "hidden")
