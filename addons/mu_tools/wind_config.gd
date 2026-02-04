## Wind-Affected Models in MU Online
#
# User Request: Only grass should react to wind (SVEN behavior)
# Trees are excluded based on specific request, even if they might have StreamMesh.

WIND_OBJECTS = [
\t"Grass01", "Grass02", "Grass03", "Grass04",
\t"Grass05", "Grass06", "Grass07", "Grass08"
]

# Note: In SVEN/Webzen, wind animation is often controlled by 'StreamMesh' 
# or 'Video Mesh' flags in the BMD file.
# The shader should apply UV scrolling or vertex displacement based on time.
