import unreal


BLUEPRINT_PATH = "/Game/New/NPC/Blueprints"
DATA_PATH = "/Game/New/NPC/Data"


def get_or_create_data(asset_tools):
    asset_path = f"{DATA_PATH}/DA_TestNPCDialogue"
    existing = (
        unreal.EditorAssetLibrary.load_asset(asset_path)
        if unreal.EditorAssetLibrary.does_asset_exist(asset_path)
        else None
    )
    if existing:
        data = existing
    else:
        factory = unreal.DataAssetFactory()
        data = asset_tools.create_asset(
            "DA_TestNPCDialogue", DATA_PATH, unreal.NPCDialogueData, factory
        )
    if not data:
        raise RuntimeError("Failed to create DA_TestNPCDialogue")

    data.set_editor_property("display_name", "테스트 NPC")
    data.set_editor_property("interaction_action_text", "대화하기")

    first_line = unreal.NPCDialogueLine()
    first_line.set_editor_property("line_id", "Ambient_01")
    first_line.set_editor_property("text", "테스트용 NPC 대화의 첫 번째 줄입니다.")

    second_line = unreal.NPCDialogueLine()
    second_line.set_editor_property("line_id", "Ambient_02")
    second_line.set_editor_property(
        "text", "추후 WBP는 PlayerDialogueComponent의 현재 상태를 표시하면 됩니다."
    )

    ask_reply = unreal.NPCDialogueReply()
    ask_reply.set_editor_property("reply_id", "AskAgain")
    ask_reply.set_editor_property("text", "조금 더 설명해 주세요.")
    ask_reply.set_editor_property("next_line_id", "Ambient_03")

    leave_reply = unreal.NPCDialogueReply()
    leave_reply.set_editor_property("reply_id", "Leave")
    leave_reply.set_editor_property("text", "이만 가보겠습니다.")
    second_line.set_editor_property("replies", [ask_reply, leave_reply])

    third_line = unreal.NPCDialogueLine()
    third_line.set_editor_property("line_id", "Ambient_03")
    third_line.set_editor_property(
        "text", "선택지는 서버에서 검증되며, 이 문장 이후 대화가 종료됩니다."
    )

    ambient_rule = unreal.NPCDialogueRule()
    ambient_rule.set_editor_property("rule_id", "Ambient_Default")
    ambient_rule.set_editor_property("priority", 0)
    ambient_rule.set_editor_property("lines", [first_line, second_line, third_line])
    data.set_editor_property("rules", [ambient_rule])
    unreal.EditorAssetLibrary.save_loaded_asset(data, only_if_is_dirty=False)
    return data


def get_or_create_blueprint(asset_tools, dialogue_data):
    asset_path = f"{BLUEPRINT_PATH}/BP_TestNPC"
    existing = (
        unreal.EditorAssetLibrary.load_asset(asset_path)
        if unreal.EditorAssetLibrary.does_asset_exist(asset_path)
        else None
    )
    if existing:
        blueprint = existing
    else:
        factory = unreal.BlueprintFactory()
        factory.set_editor_property("parent_class", unreal.NPCCharacter)
        blueprint = asset_tools.create_asset(
            "BP_TestNPC", BLUEPRINT_PATH, unreal.Blueprint, factory
        )
    if not blueprint:
        raise RuntimeError("Failed to create BP_TestNPC")

    generated_class = blueprint.generated_class()
    cdo = unreal.get_default_object(generated_class)
    source = cdo.get_component_by_class(unreal.NPCDialogueSourceComponent)
    if not source:
        raise RuntimeError("BP_TestNPC has no NPCDialogueSourceComponent")
    source.modify()
    source.set_editor_property("dialogue_data", dialogue_data)

    unreal.EditorAssetLibrary.save_loaded_asset(blueprint, only_if_is_dirty=False)
    return blueprint


def main():
    unreal.EditorAssetLibrary.make_directory(BLUEPRINT_PATH)
    unreal.EditorAssetLibrary.make_directory(DATA_PATH)
    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
    dialogue_data = get_or_create_data(asset_tools)
    get_or_create_blueprint(asset_tools, dialogue_data)
    unreal.log("Created /Game/New/NPC/Blueprints/BP_TestNPC")
    unreal.log("Created /Game/New/NPC/Data/DA_TestNPCDialogue")


main()
