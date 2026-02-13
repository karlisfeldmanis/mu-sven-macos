@tool
extends Node3D

## MU Animation Controller (Phase 18)
## Manages skeletal animation playback for world objects.
##
## Node tree expected:
##   root (Node3D)
##   ├── Skeleton (Skeleton3D)
##   │   ├── MeshInstance3D...
##   │   └── AnimationController (this node)
##   └── AnimationPlayer

@export var velocity: float = 0.16:
	set(v):
		velocity = v
		if _animation_player:
			# SVEN Parity: 30Hz Tick / 25Hz Baked Key = 1.2
			_animation_player.speed_scale = velocity * 1.2

@export var action_index: int = 0:
	set(v):
		action_index = v
		_play_action()

var _animation_player: AnimationPlayer
var _skeleton: Skeleton3D

func _ready():
	_find_nodes()
	_play_action()

func _find_nodes():
	# Search own children first (model viewer layout)
	for child in get_children():
		if child is AnimationPlayer:
			_animation_player = child
		if child is Skeleton3D:
			_skeleton = child

	if not _animation_player:
		# Search deeper in own subtree
		for child in get_children():
			var found = child.find_child("*AnimationPlayer*", true)
			if found:
				_animation_player = found
				break

	if not _animation_player:
		# Search siblings (world spawner layout: AnimationPlayer is sibling of Skeleton)
		var root = get_parent()
		if root:
			root = root.get_parent() if root is Skeleton3D else root
			for child in root.get_children():
				if child is AnimationPlayer:
					_animation_player = child
					break

	if _animation_player:
		_animation_player.speed_scale = velocity * 1.2

func _play_action():
	if not _animation_player:
		return

	var anim_list = _animation_player.get_animation_list()
	if anim_list.is_empty():
		return

	var anim_name = "Action_%d" % action_index
	if action_index < anim_list.size():
		# SVEN fallback: if "Action_N" doesn't exist, use the Nth animation
		if not anim_list.has(anim_name):
			anim_name = anim_list[action_index]

		_animation_player.play(anim_name)
		var anim = _animation_player.get_animation(anim_name)
		if anim:
			anim.loop_mode = Animation.LOOP_LINEAR
