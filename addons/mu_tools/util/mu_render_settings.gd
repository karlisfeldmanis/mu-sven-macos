class_name MURenderSettings
extends Node

## FSR 2.2 Settings Manager
## Handles resolution scaling and upscale modes

enum QualityMode {
	NATIVE,    # 1.0
	ULTRA,     # 0.77
	QUALITY,   # 0.67
	BALANCED,  # 0.59
	PERFORMANCE # 0.50
}

const SCALES = {
	QualityMode.NATIVE: 1.0,
	QualityMode.ULTRA: 0.77,
	QualityMode.QUALITY: 0.67,
	QualityMode.BALANCED: 0.59,
	QualityMode.PERFORMANCE: 0.50
}

const MODES = {
	QualityMode.NATIVE: Viewport.SCALING_3D_MODE_BILINEAR,
	QualityMode.ULTRA: Viewport.SCALING_3D_MODE_FSR,
	QualityMode.QUALITY: Viewport.SCALING_3D_MODE_FSR,
	QualityMode.BALANCED: Viewport.SCALING_3D_MODE_FSR,
	QualityMode.PERFORMANCE: Viewport.SCALING_3D_MODE_FSR
}

static func set_quality_mode(viewport: Viewport, mode: QualityMode):
	var scale_val = SCALES[mode]
	var scale_mode = MODES[mode]
	
	viewport.scaling_3d_mode = scale_mode
	viewport.scaling_3d_scale = scale_val
	
	# FSR 1.0 is a spatial upscaler. TAA blurs it too much, so we disable TAA.
	# It provides a very sharp, crisp image.
	viewport.use_taa = false
		
	var mode_name = QualityMode.keys()[mode]
	print("[RenderSettings] Set Scaling Mode: %s (Scale: %.2f)" % [mode_name, scale_val])

static func cycle_mode(viewport: Viewport, current_mode: QualityMode) -> QualityMode:
	var next_mode = (current_mode + 1) % QualityMode.size()
	set_quality_mode(viewport, next_mode)
	return next_mode
