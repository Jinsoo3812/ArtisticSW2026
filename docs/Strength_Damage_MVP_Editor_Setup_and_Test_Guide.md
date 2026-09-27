# Strength 대미지 설정 가이드

이 문서는 이전 MVP의 기록 경로를 보존하기 위한 안내 파일이다.

현재 구현, 삭제된 구형 필드, 플레이어·적 무기와 화살 설정, 몽타주 설정, 네트워크 검증 절차는 [Strength 전투 구현 및 에디터 설정](Strength_Combat_Implementation_and_Editor_Guide.md)을 따른다.

이전 문서에 있던 `DamageEffectClass`, `DirectDamageEffectClass`, `DamageEffects`, `BaseDamage`, `AttackPower`, 치명타·관통·원소 필드는 현재 무기 직접 피해 파이프라인에서 제거되었다. 해당 필드를 다시 만들거나 Blueprint에 호환 노드를 추가하지 않는다.
