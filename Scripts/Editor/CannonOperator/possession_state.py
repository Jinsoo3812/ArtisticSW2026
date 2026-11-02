import json
f=live_api['cannon_test'];rows=[]
for w in f['worlds']:
    for pc in unreal.GameplayStatics.get_all_actors_of_class(w,unreal.PlayerController):
        description=unreal.SWCannonOperatorAuthoringLibrary.describe_operator_controller(pc)
        row=dict(part.split('=',1) for part in description.split(' '))
        row['matches']=row['Pawn']==row['Ack']
        rows.append(row)
result=rows
(OUT/'cannon_possession_state.json').write_text(json.dumps(rows,indent=2))
