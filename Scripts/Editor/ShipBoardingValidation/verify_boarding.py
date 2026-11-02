source=(OUT.parent.parent/'LiveScripts/run_validation.py').read_text(encoding='utf-8-sig')
source=source.replace("run('setup_pie.py');scope=run('validation_sequence.py')","scope=run('boarding_roll.py')")
source=source.replace('ArtisticSW.Cannon.Operator+ArtisticSW.Animation.JumpAir+ArtisticSW.Animation.Stop+ArtisticSW.GAS.Roll+ArtisticSW.GAS.HitReaction+ArtisticSW.GAS.Strength+ArtisticSW.PlayerBow','ArtisticSW.Swimming.ExternalExit+ArtisticSW.Swimming.Cabin+ArtisticSW.Swimming.Network+ArtisticSW.GAS.Roll')
exec(compile(source,'boarding-validation','exec'),globals())
