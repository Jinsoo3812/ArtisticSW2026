import unreal


def log_widget(owner, name):
    widget = owner.get_widget_from_name(name) if owner else None
    unreal.log_warning(
        "CODEX_WIDGET name={} found={} class={}".format(
            name,
            bool(widget),
            widget.get_class().get_name() if widget else "None",
        )
    )
    if isinstance(widget, unreal.Image):
        brush = widget.get_brush()
        resource = brush.get_resource_object() if brush else None
        unreal.log_warning(
            "CODEX_WIDGET_BRUSH name={} resource={} tint={} opacity={}".format(
                name,
                resource.get_path_name() if resource else "None",
                widget.get_color_and_opacity(),
                widget.get_render_opacity(),
            )
        )
    return widget


world = unreal.EditorLevelLibrary.get_editor_world()
hud_class = unreal.EditorAssetLibrary.load_blueprint_class(
    "/Game/Blueprints/02_UI/UI_HUD/WBP_PlayerHUD"
)
weapon_class = unreal.EditorAssetLibrary.load_blueprint_class(
    "/Game/Blueprints/02_UI/UI_HUD/UI_WeaponQuickSlot/WBP_WeaponQuickSlot"
)

for label, widget_class, names in [
    ("HUD", hud_class, ["WeaponQuickSlot", "WBP_WeaponQuickSlot"]),
    (
        "WEAPON",
        weapon_class,
        ["WeaponImage1", "SlotNumber1", "WeaponImage2", "SlotNumber2", "Glow", "Image"],
    ),
]:
    unreal.log_warning("CODEX_WIDGET_BEGIN " + label)
    instance = unreal.WidgetBlueprintLibrary.create(world, widget_class, None)
    unreal.log_warning("CODEX_WIDGET_INSTANCE " + (instance.get_class().get_name() if instance else "None"))
    for widget_name in names:
        log_widget(instance, widget_name)
    unreal.log_warning("CODEX_WIDGET_END " + label)
