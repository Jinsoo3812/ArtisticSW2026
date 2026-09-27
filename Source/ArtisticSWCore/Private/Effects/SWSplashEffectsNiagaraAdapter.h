#pragma once
class UNiagaraComponent;
class FSWSplashEffectsNiagaraAdapter
{
public:
	static bool Apply(
		UNiagaraComponent* Component,
		float SizeScale,
		float LifetimeScale,
		bool& bOutUsesAuthoredLifetime);
};
