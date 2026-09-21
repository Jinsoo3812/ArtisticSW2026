# 공통 상태이상

## 2026-09-14 빌드 복구 검증

- 상태이상 신규 파일이 참조하는 기존 파일의 연동 코드가 누락되어 컴파일이 실패했다. GASCore의 AIModule 의존성, 네이티브 태그, 공통 적용 정책, 캐릭터/체력 초기화 및 보스 발동 규칙을 연결했다.
- UE 5.7 Win64 Development Editor 컴파일·링크 성공, `git diff --check` 통과.
- `ArtisticSW.GAS.Status` 2개와 `ArtisticSW.Enemy.Boss.StatusTriggers` 통과. 로그: `Saved/BuildFixStatusTests.log`.
- 독 재적용 거절 및 슬로우 대상 정책 테스트 통과. 물폭탄 2개는 기능 assertion 실패는 없었으나 월드 초기화의 QuestItem ResultItemTag/ingredient 오류로 실패 판정됐다. 로그: `Saved/BuildFixRegressionTests.log`.
- 아래 이전 작업의 검증 기록과 구별해야 한다. 이번에는 PIE·멀티플레이·패키징을 검증하지 않았다.

## 책임

| 구성 | 책임 |
|---|---|
| `IStatusReceiver` (ArtisticSWCore) | Actor 또는 Component가 상태를 받을 수 있는지 확인하는 하위 모듈 계약 |
| `UStatusComponent` (GASCore) | 수신할 상태 목록·면역·ASC 초기화·적용 진입점·사망/종료 정리 |
| `UStatusEffectLibrary` (ArtisticSWCore) | 상태 식별, 기존 태그 호환, 대상 검증, 재적용 거절 |
| `UStatusGameplayEffect` (GASCore) | 유한 지속시간, 태그, 수치 효과, 직접 ASC 적용 시에도 수신/중복 검증 |
| `UStunGameplayAbility` (GASCore) | Stun에 대한 특수 반응: 활성 행동 취소, AI 정지, 몽타주, 해제 시 복구 |
| `AShipBossEnemy` (Enemy) | 공격 중 머리 피격/HP 임계값이라는 보스 전용 발동 규칙과 GE 선택 |

`BossStunComponent`는 만들지 않는다. 공통 컴포넌트에는 머리, HP 비율, 보스, 무기 등의 발동 규칙이 없다.

## 태그

- `State.Status.Stun/Poison/Burn/Slow/WaterBomb/Knockback`: 상태 종류. GE 클래스나 시전자와 무관하게 중복 판정한다.
- `State.Control.ActionsBlocked`, `State.Control.MovementBlocked`: 해당 상태가 초래하는 행동 제한. Stun GE가 부여한다.
- `Capability.Status.Receive`: 초기화된 수신 컴포넌트가 소유한다. 실제 적용은 수신 Interface와 SupportedStatuses도 검사한다.
- `Immunity.Status`: 모든 상태 면역. `Immunity.Status.Stun` 등은 해당 종류의 면역. 면역은 새 적용을 거절하며 이미 적용된 상태를 해제하지는 않는다.
- `GameplayAbility.Status.Stun`: 특수 상태 반응 GA의 종류.
- `GameplayCue.Status.Stun`: GE 수명에 연결된 연출 접점.

함선의 `State.Ship.CannonDisabled`, 기존 함선 시간정지 `State.Debuff.TimeStopped`는 캐릭터 상태 영역에 포함하지 않는다. ASC를 가졌다는 이유만으로 독·화상·기절을 받을 수 없다. 기본 수신 컴포넌트는 BaseCharacter에만 추가된다. 다른 Actor는 수신 컴포넌트/Interface를 명시적으로 붙여야 한다. 기본 Stun 반응은 ACharacter를 대상으로 하며, 다른 Actor의 Stun에는 별도 반응 어댑터가 필요하다.

기존 `State.Poisoned`, `State.Debuff.Slow`, `State.Debuff.WaterBomb`, `State.CrowdControl.Knockback`은 저장된 BP/UI 참조를 유지한다. 공통 적용 함수는 이 태그를 대응하는 `State.Status.*`로 정규화하고 새 태그도 GE Spec에 추가한다.

## 적용과 재적용

서버에서 `StatusComponent.ApplyStatus(EffectClass, SourceASC, Context)`를 사용한다. 이미 Spec이 있는 무기/발사체는 기존 `ApplyDurationDamageEffectSpecToTarget`을 사용한다. 기존 함수의 `RefreshGrantedTag` 인자는 BP 호환을 위해 이름을 보존했으며, 이제 갱신 그룹이 아닌 상태 종류 힌트이다.

같은 종류가 존재하면 invalid handle을 반환한다. 기존 GE 제거, 기간 갱신, 주기 리셋, 스택/수치 교체를 하지 않는다. 다른 시전자나 다른 GE 클래스에도 동일하다. 다른 종류의 상태는 공존한다. GE가 만료되거나 해제된 다음에는 새 적용이 가능하다. 상태 하나에는 하나의 종류 태그를 사용하고 여러 상태를 적용하려면 여러 GE로 구성한다.

새 상태 GE는 `UStatusGameplayEffect`를 상속한다. 직접 ASC로 적용해도 Custom Application Requirement가 동일 정책을 검사한다. 기존 Blueprint GE는 공통 적용 함수를 통해 호환되며, 새 코드에서 이를 직접 ASC에 적용하지 않는다. 독 화살·검, 광역 슬로우, 물폭탄의 캐릭터 디버프, 보스 넉백 적용 경로를 통합했다. 보스 은신/대시 상태와 공격 쿨다운은 상태이상이 아니므로 이 정책을 적용하지 않는다.

## 에디터 설정

보스 클래스의 `Boss | Status`:

- `HeadHitStunEffect`: 기본 `BossHeadHitStunEffect`, 2초. 이 클래스를 상속한 GE Blueprint로 Duration 등을 조정한다.
- `HealthThresholdStunEffect`: 기본 `BossHealthThresholdStunEffect`, 3초. 별도의 GE Blueprint를 지정할 수 있다.
- `StunHealthThreshold`: 기본 0.5(50%). 0 또는 GE 클래스 None으로 비활성화한다.
- `StunHeadBones`: 기본 `head`. 실제 PhysicsAsset의 BoneName 목록으로 조정한다. BoneName이 없는 캡슐 타격은 머리 피격으로 인정하지 않는다.

StatusComponent의 `StunMontage`에 상태 반응 몽타주를 연결한다. 지속시간과 관계없이 루프/유지되는 몽타주를 쓰는 것이 좋다. 몽타주 종료가 GE를 끝내지는 않는다. `GameplayCue.Status.Stun`에 원하는 VFX/SFX Cue를 연결할 수 있다. 별도 몽타주나 Cue 에셋이 없어도 행동 제한은 동작한다.

독/화상은 `PoisonStatusGameplayEffect`, `BurnStatusGameplayEffect`의 GE Blueprint를 만들면 된다. 기본값은 5초, 1초 주기, 회당 2 피해, 적용 즉시 주기 피해 없음이다. 각 GE의 Duration/Period/Modifier를 데이터로 조절한다. 피해는 기존 Damage 메타 Attribute 파이프라인으로 전달한다.

플레이어와 적은 기존 HealthComponent의 ASC 초기화 시 StatusComponent도 초기화된다. 직접 만든 Actor는 ASC ActorInfo를 먼저 초기화한 다음 StatusComponent의 `InitializeWithAbilitySystem`을 호출한다. 사망 시 모든 캐릭터 상태 GE가 제거된다.

## 발동 및 제어 규칙

- 머리 피격: 확정된 양수 피해, 살아 있음, 피격 직전 `State.Attacking`, head bone 일치. 주기 피해는 제외한다.
- HP: 이전 HP가 임계값보다 높고 새 HP가 임계값 이하인 첫 하향 돌파. 직접 HP 변경도 처리한다. 회복 후 다시 떨어져도 재발동하지 않는다.
- 한 타격에서 둘 다 만족하면 HP 임계값 GE가 먼저 적용된다. 이미 Stun 중이면 새 조건은 무시한다. 임계값은 소비되어 만료 후 지연 발동하지 않는다.
- 치명타는 Stun을 적용하지 않고 사망을 처리한다.
- 보스에 일반 HitReaction GA를 추가하지 않는다. Stun 태그가 특수 반응 GA만 실행한다.
- Stun은 활성 프로젝트 행동 GA를 취소한다. 기본 GA의 `Allow During Control Block`은 false이고, 사망/상태 반응은 true이다. 필요한 수동 효과 GA도 이 옵션으로 예외를 명시할 수 있다.
- 취소 시 각 공격 GA의 EndAbility가 히트 윈도우·이동 태스크·타이머·예약을 정리한다. BT Abort 생존 옵션은 GAS 취소를 막지 않는다.
- AI Brain을 잠그고 진행 중 경로를 중단한다. 만료 시 Brain을 풀어 BT가 다시 판단한다. 입력 이동/점프/몸 회전은 상태 태그로 차단한다. 중력과 배의 기반 이동은 유지한다.
- Stun 반응 GA는 일반 취소로 끝나지 않는다. GE 태그 제거/만료가 종료 시점을 결정하며, 사망 정리가 이동을 재개하지 않는다.

## 검증

자동 테스트: `ArtisticSW.GAS.Status`, `ArtisticSW.Enemy.Boss.StatusTriggers`, `ArtisticSW.Enemy.RangedEnemy.StatusEffectIgnoreReapplication`, `ArtisticSW.AreaSlow.RangeAndTargetPolicy`, `ArtisticSW.WaterBomb.GameplayEffectApplication`, `ArtisticSW.WaterBomb.ProjectileHitIntegration`.

PIE에서는 실제 무기 Trace의 BoneName, 각 보스 몽타주·돌진/은신 취소 정리, 배 위 기반 이동, 플레이어 네트워크 입력 차단과 서버/클라이언트 태그·Cue 동기화를 확인한다.

### 이번 작업의 검증 상태

- UE 5.7 Development Editor 빌드와 `git diff --check` 통과.
- 최초 테스트 실행에서 GE 생성자 서브오브젝트 생성 오류를 발견하여 수정했고, 이후 엔진 시작 및 상태 적용까지 확인했다.
- 중단 지점에서 검증을 재개하여 위 자동 테스트 7개 모두 통과했다. 기절의 고정 수명·재적용 거절, 상태 수신 대상·독/화상 공존, 보스 머리 피격/HP 임계값별 GE 선택, 기존 독·슬로우·물폭탄 적용 경로를 확인했다. 실행 로그: `Saved/StatusResumeTests.log`.
- 테스트 월드의 시간 진행에는 `GFrameCounter` 증가를 적용했다. 보스 테스트는 기존 네이티브 AttributeSet을 ASC에 등록하고, BeginPlay 없는 월드에서도 Actor의 동적 HP 델리게이트가 실행되도록 `FEditorScriptExecutionGuard`를 사용한다. 직접 생성 ASC에도 StatusComponent 초기화를 연결했다.
- 테스트 월드 초기화 시 발생하는 기존 제작 데이터의 QuestItem ResultItemTag/ingredient 오류만 예상 로그로 지정했다. 제작 데이터 자체를 수정한 것은 아니다.
- 넓은 최초 실행에는 AreaSlow 입력/물폭탄 GA 대포 연동 테스트 실패도 있었다. 해당 입력·아이템 데이터 경로는 이번 수정 범위에서 해결하지 않았으며, 이 실패들이 기존부터 있었는지를 별도 기준 빌드로 검증하지는 않았다.
- 실제 PIE 및 멀티플레이 연출 검증은 수행하지 않았다. StunMontage 및 GameplayCue 시각 에셋은 별도로 지정해야 한다.
