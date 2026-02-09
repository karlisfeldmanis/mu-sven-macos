@tool
extends RefCounted

class_name MULogger

const LOG_PATH_USER = "user://mu_launch.log"
const LOG_PATH_RES = "res://mu_launch.log"

static var _log_file_user: FileAccess
static var _log_file_res: FileAccess

static func init():
	_log_file_user = FileAccess.open(LOG_PATH_USER, FileAccess.WRITE)
	_log_file_res = FileAccess.open(LOG_PATH_RES, FileAccess.WRITE)
	
	var header = "=== MU ONLINE REMASTER LOG [%s] ===\n" % Time.get_datetime_string_from_system()
	_write_raw(header)
	
	# Log system info
	info("OS: %s" % OS.get_name())
	info("Model Name: %s" % OS.get_model_name())
	info("Processor: %s" % OS.get_processor_name())
	info("Godot Version: %s" % Engine.get_version_info().string)
	
	# Try to get rendering info
	var device_name = RenderingServer.get_video_adapter_name()
	var vendor_name = RenderingServer.get_video_adapter_vendor()
	info("GPU Adapter: %s (%s)" % [device_name, vendor_name])

static func info(msg: String):
	var line = "[INFO] [%s] %s" % [Time.get_time_string_from_system(), msg]
	print(line)
	_write_line(line)

static func error(msg: String):
	var line = "[ERROR] [%s] %s" % [Time.get_time_string_from_system(), msg]
	push_error(line)
	_write_line(line)

static func warn(msg: String):
	var line = "[WARN] [%s] %s" % [Time.get_time_string_from_system(), msg]
	push_warning(line)
	_write_line(line)

static func _write_line(line: String):
	_write_raw(line + "\n")

static func _write_raw(content: String):
	if _log_file_user:
		_log_file_user.store_string(content)
		_log_file_user.flush()
	if _log_file_res:
		_log_file_res.store_string(content)
		_log_file_res.flush()

static func close():
	if _log_file_user:
		_log_file_user.close()
		_log_file_user = null
	if _log_file_res:
		_log_file_res.close()
		_log_file_res = null
