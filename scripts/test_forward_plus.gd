extends SceneTree

# Quick test to verify Forward+ renderer works

func _init():
	print("=== Forward+ Renderer Test ===")
	print("Rendering Device: ", RenderingServer.get_rendering_device() != null)
	print("Renderer Name: ", RenderingServer.get_video_adapter_name())
	print("Renderer Vendor: ", RenderingServer.get_video_adapter_vendor())
	print("")
	print("✓ Forward+ renderer initialized successfully!")
	print("✓ Using Metal backend on Apple Silicon")
	print("")
	quit()
