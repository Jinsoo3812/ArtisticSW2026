#pragma once

class UNiagaraComponent;

/** Parameter adapter for the Shooter VFX Pack used by the ship combat effects. */
class FSWShooterVFXNiagaraAdapter
{
public:
	/** Returns true when at least one Shooter VFX Pack size parameter was applied. */
	static bool Apply(UNiagaraComponent* Component, float SizeScale, float LifetimeScale,
		bool& bOutAppliedLifetime);
};
