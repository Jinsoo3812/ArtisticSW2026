"""UE 5.7 commandlet driver. Pass -FoleyMode=dry-run|apply|verify|verify-source|setup.
Use -FoleySourceContent="<original GASP Content directory>" for source reads,
and -FoleyReportDir="<report directory>" to resume an existing audit.

Never changes animation tracks/curves/poses, imported audio, maps or materials.
Only the explicit, preflighted contact payloads and the new Foley profile are saved.
"""
from pathlib import Path
import hashlib, json, re, shutil
import unreal

args=unreal.SystemLibrary.get_command_line()

def argument(name, default=''):
    match=re.search(r'(?:^|\s)-'+re.escape(name)+r'=(?:"([^"]*)"|([^\s]+))', args)
    return next(value for value in match.groups() if value is not None) if match else default

MODE=argument('FoleyMode','dry-run')
assert MODE in ('dry-run','apply','verify','verify-source','setup')
ROOT=Path(argument('FoleyReportDir',str(Path(unreal.Paths.project_saved_dir())/'Validation/FoleyMigration'))).resolve()
CONTENT=Path(unreal.Paths.project_content_dir()).resolve()
SOURCE=argument('FoleySourceContent')
if MODE in ('dry-run','apply','verify-source'):
    assert SOURCE and Path(SOURCE).is_absolute(), 'Pass -FoleySourceContent="<absolute original GASP Content directory>"'
    SOURCE=str(Path(SOURCE).resolve())
    assert (Path(SOURCE)/'Blueprints/AnimNotifies').is_dir(), 'Source GASP notify directory is missing'
ROOT.mkdir(parents=True, exist_ok=True)

def write(name, value):
    (ROOT/name).write_text(json.dumps(value,ensure_ascii=False,indent=2),encoding='utf-8')

def digest(path):
    with path.open('rb') as f:
        return hashlib.file_digest(f,'sha256').hexdigest()

def tag(name):
    result=unreal.GameplayTag()
    result.import_text('(TagName="Foley.Event.'+name+'")')
    return result

def setup():
    path='/Game/Audio/Foley/DA_Mannequin_Foley'
    profile=unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else None
    if profile:
        assert isinstance(profile,unreal.SWFoleyAudioProfile), 'Profile path occupied'
        print('FOLEY_SETUP existing profile preserved')
        return
    rows=[('Walk','Walk','FOOTSTEP','FOOT',True),('Run','Run_Soft','FOOTSTEP','FOOT',True),
        ('Jump','Jump','JUMP','CAPSULE',False),('Land','Land','LAND','CAPSULE',True),
        ('Scuff','Scuff','SCUFF','FOOT',True),('Handplant','Handplant','OTHER','HAND',True),
        ('RunBackwds','RunBackwards','FOOTSTEP','FOOT',True),('ScuffPivot','ScuffPivot','SCUFF','FOOT',True),
        ('ScuffWall','ScuffWall','OTHER','FOOT',True),('RunStrafe','RunStrafe','FOOTSTEP','FOOT',True),
        ('Tumble','Tumble','OTHER','CAPSULE',True),('WalkBackwds','WalkBackwards','FOOTSTEP','FOOT',True)]
    events={}
    for event,preset,group,contact,trace in rows:
        sound=unreal.load_asset('/Game/Audio/Foley/MetaSounds/Presets/MSS_FoleySound_'+preset)
        assert isinstance(sound,unreal.SoundBase), preset
        definition=unreal.SWFoleySoundSet()
        definition.set_editor_property('group',getattr(unreal.SWFoleyGroup,group))
        definition.set_editor_property('contact',getattr(unreal.SWFoleyContact,contact))
        definition.set_editor_property('default_sound',sound)
        definition.set_editor_property('trace_surface',trace)
        definition.set_editor_property('importance',1.5 if event=='Land' else 1.0)
        events[tag(event)]=definition
    factory=unreal.DataAssetFactory()
    factory.set_editor_property('data_asset_class',unreal.SWFoleyAudioProfile)
    profile=unreal.AssetToolsHelpers.get_asset_tools().create_asset('DA_Mannequin_Foley','/Game/Audio/Foley',unreal.SWFoleyAudioProfile,factory)
    assert profile
    profile.set_editor_property('events',events)
    rules={'RunStrafe':'Run','RunBackwds':'Run','WalkBackwds':'Walk','ScuffPivot':'Scuff','ScuffWall':'Scuff'}
    profile.set_editor_property('event_fallbacks',{tag(a):tag(b) for a,b in rules.items()})
    assert unreal.EditorAssetLibrary.save_loaded_asset(profile,only_if_is_dirty=False)
    write('profile.json',{'path':path,'events':len(events),'fallbacks':rules,'surfaces':'Authored overrides; unspecified surfaces use existing GASP default sounds'})
    print('FOLEY_SETUP created',path,len(events))

if MODE=='setup':
    setup()
elif MODE in ('verify','verify-source'):
    report=json.loads((ROOT/'apply.json').read_text(encoding='utf-8'))
    assert not report['errors']
    checked=0
    unresolved=0
    for row in report['packages']:
        if MODE=='verify-source':
            audit=json.loads(unreal.SWFoleyMigrationLibrary.migrate_gasp_foley([row['package']],SOURCE,False))
            assert not audit['errors'] and not audit['replacements']
            assert audit['packages'][0]['after']==row['after'],row['package']+' persisted source-context metadata mismatch'
        asset=unreal.load_asset(row['package'])
        events=unreal.AnimationLibrary.get_animation_notify_events(asset)
        actual=list(unreal.SWFoleyMigrationLibrary.export_notify_metadata(asset))
        expected=list(row['after'])
        if MODE=='verify':
            # This original non-Foley class was already absent before the migration.
            # Preserve its serialized reference, and separately verify the exact resolved
            # metadata in verify-source. Never ignore a difference on any Foley contact.
            for i,(a,b) in enumerate(zip(expected,actual)):
                if 'BP_NotifyState_EarlyTransition_C' in a and a!=b:
                    clean=re.sub(r',NotifyStateClass="[^"]*"','',a)
                    if clean==b:
                        expected[i]=clean
                        unresolved+=1
        if actual!=expected:
            write('verify_mismatch.json',{'package':row['package'],'expected':row['after'],'actual':actual})
            raise AssertionError(row['package']+' metadata mismatch')
        for expected in row['events']:
            event=events[expected['index']]
            assert isinstance(event.notify,unreal.SWAnimNotify_FoleyEvent),row['package']
            payload=event.notify.foley_event
            assert expected['event'] in payload.event.export_text()
            assert int(payload.side.value)==expected['side']
            assert abs(payload.volume_multiplier-expected['volume'])<1e-6
            assert abs(payload.pitch_multiplier-expected['pitch'])<1e-6
            assert not event.trigger_on_dedicated_server
            checked+=1
        if checked%300==0: unreal.SystemLibrary.collect_garbage()
    profile=unreal.load_asset('/Game/Audio/Foley/DA_Mannequin_Foley')
    assert isinstance(profile,unreal.SWFoleyAudioProfile)
    assert len(profile.get_editor_property('events'))==12
    for bp in ['/Game/Blueprints/Player/BP_Player_Man','/Game/Blueprints/Player/BP_Player_Woman']:
        cls=unreal.EditorAssetLibrary.load_blueprint_class(bp)
        cdo=unreal.get_default_object(cls)
        component=cdo.get_editor_property('foley_component')
        assert component and 'DA_Mannequin_Foley' in str(component.get_editor_property('profile_override'))
    hashes=json.loads((ROOT/'AudioHashes.json').read_text(encoding='utf-8'))
    for path,checksum in hashes.items(): assert digest(CONTENT/path)==checksum,path+' imported audio changed'
    write(MODE+'.json',{'packages':len(report['packages']),'native_events':checked,'metadata':'all event metadata matches persisted apply snapshot; source-context validates original external class references','existing_unresolved_early_transition_events':unresolved,'player_blueprints':2,'profile_events':12,'preserved_audio_files':len(hashes),'errors':0})
    print('FOLEY_VERIFIED',len(report['packages']),checked)
else:
    packages=sorted('/Game/'+p.relative_to(CONTENT).with_suffix('').as_posix() for p in (CONTENT/'Characters/UEFN_Mannequin/Animations').rglob('M_*.uasset'))
    if MODE=='dry-run':
        write('candidates.json',packages)
        write('InputHashes.json',{package:digest(CONTENT/(package[6:]+'.uasset')) for package in packages})
        write('AudioHashes.json',{p.relative_to(CONTENT).as_posix():digest(p) for p in (CONTENT/'Audio/Foley').rglob('*') if p.is_file()})
    else:
        preflight=json.loads((ROOT/'dry-run.json').read_text(encoding='utf-8'))
        assert not preflight['errors'] and preflight['complete']
        packages=[r['package'] for r in preflight['packages'] if r['events']]
        hashes=json.loads((ROOT/'InputHashes.json').read_text(encoding='utf-8'))
        manifest=[]
        for package in packages:
            src=CONTENT/(package[6:]+'.uasset')
            assert digest(src)==hashes[package], 'Target changed after preflight: '+package
            backup=ROOT/'Backup'/(package[6:]+'.uasset')
            assert not backup.exists(),'Backup exists; audit before resuming: '+package
            backup.parent.mkdir(parents=True,exist_ok=True)
            shutil.copy2(src,backup)
            checksum=digest(src)
            assert checksum==digest(backup)
            manifest.append({'package':package,'sha256':checksum,'backup':str(backup)})
        write('BackupManifest.json',manifest)
    report={'errors':0,'replacements':0,'recovered':0,'saved':0,'packages':[],'complete':False}
    for start in range(0,len(packages),12):
        batch=packages[start:start+12]
        result=json.loads(unreal.SWFoleyMigrationLibrary.migrate_gasp_foley(batch,SOURCE,MODE=='apply'))
        if MODE=='apply':
            expected={row['package']:row for row in preflight['packages']}
            for row in result['packages']:
                if 'error' not in row:
                    assert row['before']==expected[row['package']]['before'],row['package']+' changed since preflight'
                    assert row['events']==expected[row['package']]['events'],row['package']+' payload changed'
        for key in ('errors','replacements','recovered','saved'): report[key]+=result[key]
        report['packages'].extend(result['packages'])
        write(MODE+'.json',report)
        assert not result['errors'],[r for r in result['packages'] if 'error' in r]
        unreal.SystemLibrary.collect_garbage()
        print('FOLEY_PROGRESS',MODE,start+len(batch),len(packages),report['replacements'])
    report['complete']=True
    write(MODE+'.json',report)
    print('FOLEY_COMPLETE',MODE,len(packages),report['replacements'],report['recovered'],report['saved'])
    if MODE=='apply': setup()
