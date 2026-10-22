"""Read-only audit of authored sword traces and deck enemy hit surfaces."""
import json
from pathlib import Path
import unreal

report = []
paths = [
    "/Game/GameplayAbilitySystem/Weapon/BP_BaseSwordA",
    "/Game/GameplayAbilitySystem/Weapon/BP_BaseSwordB",
    "/Game/GameplayAbilitySystem/Enemy/BP_DeckMeleeEnemy",
    "/Game/GameplayAbilitySystem/Enemy/BP_DeckRangedEnemy",
    "/Game/GameplayAbilitySystem/Enemy/Balancing/T1/T1_BP_DeckMeleeEnemy",
    "/Game/GameplayAbilitySystem/Enemy/Balancing/T1/T1_BP_DeckRangedEnemy",
]

def prop(obj, name):
    try:
        return str(obj.get_editor_property(name))
    except Exception as error:
        return str(error)

for path in paths:
    cls = unreal.EditorAssetLibrary.load_blueprint_class(path)
    if not cls:
        report.append({"asset": path, "error": "class missing"})
        continue
    actor = unreal.get_default_object(cls)
    row = {"asset": path}
    if isinstance(actor, unreal.SwordItem):
        for name in ["trace_radius", "trace_object_types", "trace_complex",
                     "include_animated_combat_hurtboxes"]:
            row[name] = prop(actor, name)
        for name in ["trace_start_point", "trace_end_point"]:
            component = actor.get_editor_property(name)
            row[name] = {"relative_location": prop(component, "relative_location"),
                         "relative_rotation": prop(component, "relative_rotation")}
    else:
        capsule = actor.get_editor_property("capsule_component")
        mesh = actor.get_editor_property("mesh")
        row["capsule"] = {"profile": str(capsule.get_collision_profile_name()),
                          "enabled": str(capsule.get_collision_enabled()),
                          "object_type": str(capsule.get_collision_object_type())}
        row["mesh"] = {"profile": str(mesh.get_collision_profile_name()),
                       "enabled": str(mesh.get_collision_enabled()),
                       "physics_asset_override": prop(mesh, "physics_asset_override"),
                       "skeletal_mesh": prop(mesh, "skeletal_mesh_asset")}
        hurtbox = actor.get_component_by_class(unreal.CombatHurtboxComponent)
        row["hurtbox_mode"] = prop(hurtbox, "mode")
    report.append(row)

for path in ["/Game/Blueprints/DAs/Weapon_DAs/Sword/WDA_SwordA",
             "/Game/Blueprints/DAs/Weapon_DAs/Sword/WDA_SwordB"]:
    animation_data = unreal.load_asset(path)
    entry = animation_data.get_editor_property("default_entry")
    montage = entry.get_editor_property("basic_attack_montage")
    notifies = []
    for event in unreal.AnimationLibrary.get_animation_notify_events(montage):
        state = event.get_editor_property("notify_state_class")
        notifies.append({"name": prop(event, "notify_name"),
                         "state": prop(event, "notify_state_class"),
                         "native_hit_window": isinstance(state, unreal.ANS_HitScanWindow),
                         "notify": prop(event, "notify"),
                         "server": prop(event, "trigger_on_dedicated_server"),
                         "time": unreal.AnimationLibrary.get_anim_notify_event_trigger_time(event),
                         "duration": unreal.AnimationLibrary.get_anim_notify_event_duration(event)})
    report.append({"asset": path, "montage": str(montage),
                   "sections": prop(entry, "basic_attack_combo_sections"),
                   "montage_notifies": notifies})

output = Path(unreal.Paths.project_saved_dir()) / "DeckMeleeHitAudit.json"
output.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
unreal.log("Deck melee audit: " + str(output))
