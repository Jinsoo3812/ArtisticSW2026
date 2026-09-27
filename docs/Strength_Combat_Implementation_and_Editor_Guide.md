# Strength 전투 통합과 에디터 설정

## 현재 구조

```text
무기 StrengthBonus → 장비 Presenter → EquipmentStatComponent → ASC.Strength
공격 GA (서버 승인) → Strength snapshot Spec
검 Sweep / 화살 충돌 / 보스 판정 → CombatHitResolverComponent
→ GASAttributeDamageExecution → Damage 메타 Attribute → Health
→ 확정 피격·사망 이벤트 → UI / 애니메이션 / GameplayCue
```

직접 무기 피해는 `max(1, 발사·타격창 시작 시 Strength × AttackCoefficient × ChargeMultiplier)`다. 계수와 배율은 유한한 양수만 허용한다. 최종 Damage와 Health 변경은 서버가 결정한다.

| 책임 | 클래스 |
|---|---|
| 장비 Model: 활성 무기 하나의 보너스와 GE Handle, 교체 실패 보존, 파괴·사망·풀 정리 | EquipmentStatComponent |
| 전투 Model: 타격창, 대상별 중복, 진영, 무적, 사망, 정적 장애물 검증 | CombatHitResolverComponent |
| 수치 Model: Strength snapshot 계산, Damage 소모와 Health 감소 | GASAttributeDamageExecution, BaseAttributeSet |
| Presenter: 장착·수납·발사·공격 진행과 취소 | PlayerEquipmentComponent, BaseWeaponComponent, 공격 GA |
| 표시 Presenter: 복제된 Strength 구독과 공격 속도 연출 | CombatPresentationComponent |
| View: 결과 표시 | 기존 Widget, AnimInstance, GameplayCue |

Item과 Projectile은 ArtisticSWCore의 추상 Model 계약을 사용하고 GASCore가 구현한다. 순환 모듈 의존성을 만들지 않는다. 장비·Resolver 컴포넌트는 서버 경로에서 자동 생성하므로 BP에 수동으로 추가하지 않는다.

## 삭제 및 통합 범위

- 무기·화살·보스 공격의 `DamageEffectClass` 선택 필드를 제거했다. 공통 네이티브 `GASAttributeDamageGameplayEffect`를 자동 사용한다.
- 검의 `BaseDamage`, `AttackPowerMultiplier`, `CalculateDamage`, 중복 `BasicMeleeDamageGameplayEffect`를 제거했다.
- `AttackPower` Attribute를 제거하고 상태창을 `Strength` 구독으로 변경했다. 기존 Attribute/함수 참조에는 Core Redirect를 추가했다. 기존 Widget의 `AttackPowerText` 이름은 BP 바인딩 보존을 위한 이름이며 별도 스탯이 아니다.
- 화살의 직접 피해 GE 배열/설정 API, 사용되지 않던 치명타·관통 횟수·원소·태그 필드, 중복 상태이상 배열을 제거했다. 직접 피해 Spec 하나와 `StatusEffects`만 남긴다.
- 검·적 무기·화살·보스의 별도 적중 집합을 제거하고 Resolver가 대상별 1회를 관리한다.
- 보스의 사용하지 않는 고정 `Damage` 필드를 제거했다.
- 실제 수류탄·함정·함선·별도 스킬이 사용하는 고정 피해 및 지속 상태이상 GE는 유지한다. 이것들도 같은 Damage → Health 종착 경로를 사용한다. 해당 스킬의 밸런스 입력까지 Strength로 바꾸는 변경은 하지 않았다.

## 화살·몽타주 문제와 수정

기존 로그의 `GE_Player_InstantArrow_C must contain only one Strength execution and no modifiers`는 구형 GE가 선택되어 새 경로가 화살을 발사 전에 제거한 원인이었다. 이제 사용자 지정 직접 피해 GE를 선택할 수 없어 이 충돌이 사라진다.

플레이어와 적 모두 `SpawnActorDeferred → FinishSpawning → 서버 Strength Spec 초기화 → LaunchArrow` 순서를 사용한다. Blueprint Construction 이후에 payload를 설정한다. Launch는 물리 시뮬레이션을 끄고 CollisionComp를 UpdatedComponent로 지정하고, 이동 활성화·속도·중력·충돌 프로필을 확정한다.

적 근접/원거리와 플레이어 활 Release는 BlendOut 시작을 완료로 취급하지 않는다. 근접은 BlendOut에서 충돌만 닫고, 정상 종료는 OnCompleted까지 기다린다. 블렌드아웃 도중 중단도 취소로 처리하여 GA/BT 대기를 정리한다. 피격·사망·BT Abort에 의한 의도된 취소는 여전히 몽타주를 중단한다.

또한 적중 Context를 복제할 때 `SetContext`로 Strength를 재캡처하던 오류를 수정했다. 발사 후 무기를 바꾸거나 Strength가 변해도 날아가는 화살의 피해는 유지된다.

## 1. 빌드와 재시작

1. PIE와 Unreal Editor를 종료한다. UPROPERTY 삭제가 있어 Live Coding만으로 전환하지 않는다.
2. PowerShell에서 실행한다.

```powershell
& 'C:/Program Files/Epic Games/UE_5.7/Engine/Build/BatchFiles/Build.bat' ArtisticSW2026Editor Win64 Development '-Project=C:/Users/wonkii/Documents/GitHub/ArtisticSW2026/ArtisticSW2026.uproject' -WaitMutex -NoHotReloadFromIDE
```

3. `Succeeded`를 확인하고 프로젝트를 다시 연다.
4. 변경한 무기·공격 BP를 Compile/Save한다. 제거한 필드를 접근하는 사용자 추가 BP 노드가 있다면 삭제한다. 피해를 직접 적용하는 노드를 추가하지 않는다.

## 2. 기본 Attribute

1. 플레이어·적 초기 Attribute GE를 연다.
2. Strength를 초기화한다. C++ 기본값은 10이다. 무기로만 Strength를 얻으려면 기본 Strength를 0으로 설정한다.
3. Health/MaxHealth를 초기화한다. 초기화 GE는 서버에서 ASC 준비 후 적용한다.
4. 장착 후 초기화 GE를 반복 적용하거나 Infinite Override로 Strength를 덮어쓰지 않는다.
5. 진영에 맞게 `Team.Player` 또는 `Team.Enemy` 태그를 부여한다.

기본 Strength가 0이어도 유효한 공격의 최소 피해는 1이다. 미장착 공격 허용 여부는 장비/GA의 실행 조건에서 관리한다.

## 3. 플레이어 무기 보너스

1. 실제 생성하는 검·활 BP를 연다: `BP_BaseSwordA`, `BP_BaseSwordB`, `BP_Bow`.
2. Class Defaults의 `Strength Bonus`를 설정한다. 예: 검 A 5, 검 B 20, 활 8.
3. PlayerEquipmentComponent의 `Strength Equipment Effect Class`는 기본 `GASStrengthEquipmentGameplayEffect`를 사용한다.
4. 사용자 정의 장착 GE는 Infinite, Strength Additive, SetByCaller `Data.StrengthBonus`, Modifier 하나, Execution 없음, Stacking None이어야 한다.
5. 기존 BP Equip/BeginPlay/Notify에서 Strength를 직접 더하는 노드가 있으면 제거한다.
6. Compile/Save 후 Strength 10에서 검 +5 장착 15, 검 +20 교체 30, 수납 10을 확인한다.

에셋 점검 시 저장된 값은 검 A 0, 검 B 10, 활 6이었다. 밸런스 값은 자동으로 덮어쓰지 않았다. 0 보너스는 유효하며 오류가 아니다.

## 4. 플레이어 근접

1. 검 BP에서 `Attack Coefficient`를 설정한다. 기본 1.0.
2. TraceStartPoint/TraceEndPoint를 칼날 양 끝에 두고 Trace Object Types에 Pawn을 포함한다.
3. 공격 몽타주 각 콤보 타격 구간에 `ANS_HitScanWindow`를 배치한다.
4. 동일 구간을 BP 이벤트와 Notify로 이중 시작하지 않는다.
5. 실제 피해 이후 적용할 상태이상만 `Status Effect Classes`에 넣는다.
6. Damage Effect Class를 지정하는 단계는 없다.

플레이어는 콤보 섹션당 같은 대상 1회다. 독립 다단 타격은 섹션을 나눈다.

## 5. 플레이어·적 화살

1. `/Game/GameplayAbilitySystem/Weapon/BP_Arrow`와 `/Game/GameplayAbilitySystem/Enemy/Weapon/BP_EnemyProjectile`를 연다.
2. `Damage Data > Attack Coefficient`를 설정한다. 기본 1.0. 직접 피해 GE 필드는 제거되었다.
3. 상태이상은 `Damage Data > Status Effects` 한 배열에 설정한다. Refresh Granted Tag는 해당 상태이상 태그와 일치시킨다.
4. 활/적의 Projectile Class가 위 공통 ArrowProjectile 계열인지 확인한다.
5. 플레이어 활 소켓과 Nock/Release Notify를 확인한다. 적은 캐릭터 Mesh의 `Arrow_socket`과 공격 몽타주의 발사 GameplayEvent Notify를 확인한다.
6. 적 발사 이벤트 태그는 해당 GA가 기다리는 `RangedAttack` 이벤트 설정과 일치시킨다. 몽타주가 없는 적은 즉시 발사하며, Notify가 없으면 완료 시 한 번 발사한다.
7. 화살 루트 Box의 Mobility는 Movable로 유지한다. Mesh에 물리 시뮬레이션을 켜거나 BP Tick에서 위치/속도를 덮어쓰지 않는다. 네이티브 Launch가 이동과 `ArrowProjectile` 충돌 프로필을 설정한다.
8. 플레이어 활 GA의 FullDrawTime, DrawAlphaStartDelay, Min/MaxChargeDamageMultiplier를 설정한다. 최소 배율은 양수, 최대는 최소 이상이어야 한다.
9. Team Damage Filtering은 기본 활성화다. 디버그 목적 외에는 끄지 않는다.

차지 배율은 서버의 당김 시작부터 Release 입력까지의 시간으로 계산한다. 발사 후 스탯 변경은 이미 만든 Spec에 영향을 주지 않는다.

## 6. 적 무기·보스

1. Enemy WeaponComponent의 Weapon Registry가 실제 `DA_Weapon`을 참조하는지 확인한다.
2. 선택되는 WeaponTag의 Weapon Definitions 항목에서 `Stats > Strength Bonus`, `Combat Data > Attack Coefficient`를 설정한다.
3. WeaponActorClass, EquipSocketName, BackSocketName, GrantedAbilities, 공격 범위를 확인한다.
4. `Combat Data > Damage Effect Class` 설정은 제거되었다.
5. 보스 BasicAttackSet의 패턴 Attack Coefficient를 설정한다. 최종 계수는 무기 계수 × 패턴 계수다.
6. Notify 방식에서는 독립 타격에 각각 다른 `ANS_HitScanWindow` 인스턴스를 사용한다.
7. Use Timed Hit Scan Window를 켜면 Start/Duration으로 판정하고 Notify 창은 무시한다.
8. DashSlash/Knockback BP에는 Attack Coefficient만 설정한다. 기본값은 각각 2.0/1.5이며 고정 Damage/GE 선택은 없다.
9. 몽타주 Slot이 AnimBP Output Pose에 연결되어 있는지 확인한다. 공격 섹션이 루프하거나 잘못된 다음 섹션을 가리키지 않아야 한다.
10. BP에서 BlendOut 이벤트로 EndAbility/StopMontage를 호출하는 별도 노드가 있다면 제거한다. 정상 완료는 네이티브 OnCompleted가 관리한다.

사망·풀 반환 시 보너스와 타격창을 정리하고 복원 시 한 번만 부여한다. 화면 밖 서버에서도 공격 중 Mesh Pose/Bone 갱신을 유지한다.

## 7. UI

상태창은 Strength를 표시한다. 커스텀 UI는 HealthComponent 초기화 이후 CombatPresentationComponent의 GetStrength로 초기값을 읽고 OnStrengthChanged를 구독한다. Widget 종료 시 해제한다. OnRep/UI에서 GE를 적용하지 않는다.

## 8. 에셋 점검 도구

`Scripts/audit_strength_combat.py`는 읽기 전용 점검 도구다. 직접 피해 GE를 선택하는 필드 자체가 없어 자동 마이그레이션 단계는 필요하지 않다.

Unreal Output Log의 Python 모드에서 실행:

```python
exec(open(r"C:\Users\wonkii\Documents\GitHub\ArtisticSW2026\Scripts\audit_strength_combat.py", encoding="utf-8").read())
```

무기/공격 Blueprint를 메모리에서 컴파일하고 수치와 구형 GE 참조를 `Saved/StrengthCombatAudit.json`에 기록한다. 에셋을 저장/삭제하지 않는다. Python Editor Script Plugin이 필요하다. 실제 Unreal Python 실행을 확인했다.

## 9. 네트워크·플레이 검증

1. Listen Server와 클라이언트 2개에서 장착·교체·수납 후 Strength 수렴을 확인한다.
2. Dedicated Server에서 Player→Enemy, Enemy→Player 근접/화살 피해를 확인한다.
3. Strength 30, 계수 1.5, 차지 1이면 피해 45인지 확인한다.
4. 발사 직후 Strength를 변경해도 기존 화살의 피해가 유지되는지 확인한다.
5. 여러 충돌 부위에 겹쳐도 타격창/화살당 대상 1회인지 확인한다.
6. 자기 자신·아군·무적·사망 대상과 벽 뒤 대상은 피해를 받지 않아야 한다.
7. 적 몽타주 마지막 구간까지 재생하고 정상 종료 뒤 BT가 다음 행동을 하는지 확인한다. 피격/사망 취소에서는 대기와 타격창이 즉시 정리되어야 한다.
8. 지연/손실, 늦은 합류, 사망·풀 복귀, 무기 파괴에서 중복 보너스/잔존 창이 없는지 확인한다.

서버 콘솔 `sw.Combat.Strength.Debug 1`은 확정 피해와 체력 변화를 출력한다. 검증 후 0으로 끈다. 판정은 서버 현재 위치 기준이며 과거 위치 되감기는 포함하지 않는다.

## 실행 검증 기록

- UE 5.7 Development Editor 전체 C++ 빌드: 성공.
- `ArtisticSW.GAS.Strength`, `ArtisticSW.Enemy.RangedEnemy`, `ArtisticSW.Enemy.MeleeEnemy`: 전투 관련 17개 성공.
- 네이티브 Arrow, `BP_Arrow`, `BP_EnemyProjectile`은 Construction 이후 이동 복구, 전진 이동, 클라이언트 재발사 거부를 통과했다.
- Strength 스냅샷, 장비 수명, 근접/원거리 payload, 상태이상, 진영 필터, 중복 적중, UI Presenter를 통과했다.
- 같은 실행에서 `CombatTreeContract` 1개는 원거리 Behavior Tree의 `Strafe` 속도 노드 순서가 달라 실패했다. 대미지·투사체·몽타주 경로와 독립된 기존 AI 에셋 계약이다.
- 무기/공격 Blueprint 21개를 Unreal Python으로 로드·컴파일했고 오류가 없었다.
- 참조가 없던 보스 DashSlash GE와, 실제 장비가 사용하지 않던 Blueprint 기반 구형 근접 GA/화살 GE를 삭제했다. 현재 Content에서 세 레거시 에셋 이름을 참조하는 패키지는 없다.

실제 다중 클라이언트 PIE와 렌더링된 몽타주 육안 검증은 에디터에서 9절 순서대로 수행한다.
