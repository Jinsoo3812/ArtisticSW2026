import unreal
unreal.log(f"StoryNode enum exists: {hasattr(unreal, 'StoryNode')}")
if hasattr(unreal, 'StoryNode'):
    unreal.log(f"StoryNode values: {dir(unreal.StoryNode)}")
'