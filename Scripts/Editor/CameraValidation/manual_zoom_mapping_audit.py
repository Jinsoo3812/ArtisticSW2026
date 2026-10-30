import json
rows=[]
for ctx in unreal.ObjectIterator(unreal.InputMappingContext):
    try:
        mappings=ctx.get_editor_property('default_key_mappings').get_editor_property('mappings')
        wheel=[]
        for m in mappings:
            key=m.get_editor_property('key').get_editor_property('key_name')
            if 'Wheel' in str(key) or 'Scroll' in str(key):
                a=m.get_editor_property('action')
                wheel.append({'key':str(key),'action':a.get_path_name() if a else None,'consume':a.get_editor_property('consume_input') if a else None})
        if wheel:rows.append({'context':ctx.get_path_name(),'wheel':wheel})
    except Exception as e:rows.append({'context':ctx.get_path_name(),'error':str(e)})
result=rows
(OUT/'manual_zoom_mapping_audit.json').write_text(json.dumps(rows,indent=2))
