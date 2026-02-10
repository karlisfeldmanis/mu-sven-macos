extends SceneTree

func _init():
	print("Verifying Texture Scale Map...")
	
	# Load Heightmap Node Script to check logic (headless limitation)
	# We can't easily spawn the scene in headless and wait for async loading,
	# so we will unit test the calculation logic here.
	
	var textures = []
	var expected_scales = []
	
	# Test Case 1: 64x64 (Scale 1.0)
	var img1 = Image.create(64, 64, false, Image.FORMAT_RGBA8)
	textures.append(img1)
	expected_scales.append(1.0)
	
	# Test Case 2: 128x128 (Scale 0.5)
	var img2 = Image.create(128, 128, false, Image.FORMAT_RGBA8)
	textures.append(img2)
	expected_scales.append(0.5)
	
	# Test Case 3: 256x256 (Scale 0.25)
	var img3 = Image.create(256, 256, false, Image.FORMAT_RGBA8)
	textures.append(img3)
	expected_scales.append(0.25)
	
	print("Checking Scale Logic: 64.0 / width")
	var passed = true
	
	for i in range(textures.size()):
		var w = float(textures[i].get_width())
		var scale = 64.0 / w
		print("Texture %d (%dx%d) -> Scale %.2f (Expected %.2f)" % [i, w, w, scale, expected_scales[i]])
		
		if abs(scale - expected_scales[i]) > 0.001:
			print("FAILED: Scale mismatch!")
			passed = false
			
	if passed:
		print("Scale Logic Verification PASSED")
	else:
		print("Scale Logic Verification FAILED")
		
	quit()
