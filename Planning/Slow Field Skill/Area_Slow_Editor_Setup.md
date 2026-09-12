# Area Slow 에디터 연결 체크리스트

## 1. Data Asset

Content Browser에서 `Miscellaneous > Data Asset > AreaSlowSkillDataAsset`을 선택하여 `DA_AreaSlow`를 만든다.

우선 다음 값을 권장한다.

| 필드 | 시작값 |
|---|---:|
| Front Gap | 100 |
| Range Length | 800 |
| Range Width | 500 |
| Range Height | 300 |
| Vertical Offset | 0 |
| Move Speed Multiplier | 0.5 |
| Attack Speed Multiplier | 0.5 |
| Slow Duration | 4.0 |
| Confirmed Decal Duration | 0.6 |
| Target Object Types | Pawn |

Required/Blocked Query를 비워 두지 않는다. 기본 생성 값은 다음과 같다.

```text
Required: ALL(Targetable.Skill.AreaSlow, Capability.Effect.MoveSpeedMultiplier, Capability.Effect.AttackSpeedMultiplier)
Blocked:  ANY(Immunity.Debuff.Slow, State.Dead)
```

`Slow Effect Class`, `Targeting Decal Class`, `Confirmed Decal Class`는 네이티브 기본값이 자동 입력된다.

## 2. Decal Material

Targeting용과 Confirmed용 Material 또는 Material Instance를 준비한다.

- Material Domain: Deferred Decal
- Blend Mode: Translucent 또는 프로젝트의 DBuffer 정책에 맞는 모드
- Targeting: 반투명한 조준 색
- Confirmed: 더 강한 pulse/확정 색
- 가장자리 텍스처는 정사각 UV를 사용하고 Actor가 Data Asset의 Length/Width로 늘리게 둔다.

두 Material을 `DA_AreaSlow.TargetingDecalMaterial`과 `ConfirmedDecalMaterial`에 지정한다.

## 3. Gameplay Ability

1. `GA_PlayerAreaSlow` 네이티브 클래스를 부모로 Blueprint를 만든다.
2. 예: `BP_GA_AreaSlow`.
3. Class Defaults의 `Area Slow > Skill Data`에 `DA_AreaSlow`를 지정한다.
4. `BP_Player`의 `Abilities > Area Slow > Area Slow Ability Class`에 `BP_GA_AreaSlow`를 지정한다.

Data Asset이 비어 있거나 유효하지 않으면 Ability는 의도적으로 실행을 거부한다. 이는 잘못된 범위나 전체 대상 적용을 막기 위한 fail-closed 정책이다.

## 4. Input Action과 키

1. Digital 타입 `IA_AreaSlow`를 만든다.
2. `DefaultIMC`에 `IA_AreaSlow`를 추가한다.
3. 프로젝트에서 원하는 전용 능력 키를 매핑한다.
4. `BP_Player.Input > Skills > Area Slow Skill Action`에 `IA_AreaSlow`를 지정한다.
5. `bEnableAreaSlowSkillInput`이 켜져 있는지 확인한다.

Input Action에는 Hold Trigger를 넣지 않아도 된다. C++가 `Started`에서 조준을 시작하고 `Completed/Canceled`에서 취소하기 때문에 키를 누르고 있는 수명 자체를 처리한다. Hold Trigger를 쓸 경우 Started 발생 시점이 임계시간 뒤로 밀릴 수 있으므로 의도한 UX인지 확인한다.

좌클릭과 우클릭은 기존 `Default_Input_Config`의 `Key.Default.Mouse.LeftClick`, `Key.Default.Mouse.RightClick` 경로를 사용한다.

## 5. 영향을 받을 Actor 선택

`ABaseEnemy` Blueprint의 Class Defaults에서 다음을 설정한다.

```text
Ability System > Targeting > Effect Target Tags
  + Targetable.Skill.AreaSlow
```

`ABaseEnemy`는 이동/공격 배율 Capability를 모두 자동으로 소유하므로 위 opt-in만 추가하면 된다. 같은 Enemy C++ 클래스를 사용하더라도 Blueprint별로 태그를 다르게 주어 선택할 수 있다.

감속 면역이 필요한 대상에는 런타임 ASC Tag로 `Immunity.Debuff.Slow`를 부여한다. 죽은 대상은 `State.Dead` 때문에 제외된다.

Enemy가 아닌 Actor를 지원하려면 다음 계약을 모두 만족시킨다.

- ASC 보유
- `UBaseAttributeSet::MoveSpeedMultiplier`, `AttackSpeedMultiplier` 보유
- 이동 계산과 기본 공격 계산이 각 Attribute를 실제로 소비
- ASC에 `Capability.Effect.MoveSpeedMultiplier`, `Capability.Effect.AttackSpeedMultiplier` 부여
- 명시적 opt-in용 `Targetable.Skill.AreaSlow` 부여
- Data Asset의 Collision Object Type에 포함

Player Character는 위 조건을 만족시켜도 코드에서 항상 제외된다.

## 6. 기존 스킬 해금/소모 정책

Area Slow는 프로젝트의 `UPlayerSkillGameplayAbility` 정책을 따르며 `UPlayerSkillComponent`에 등록되어 있다.

- 스킬 태그: `GameplayAbility.Skill.AreaSlow`
- 스킬 식별 태그: `Item.Id.Skill.AreaSlow`
- 1회 사용 재료: `Item.Id.Material.SkillMaterial.RareSkill`
- 기본 해금 상태: 잠김

실제 플레이에서는 기존 진행 시스템이 서버에서 `UnlockSkill(GameplayAbility.Skill.AreaSlow)`를 호출하도록 연결한다. 개발 중에는 `BP_Player.bBypassSkillRequirementsForTesting`을 켜면 해금과 재료 소모를 우회할 수 있다.

이 스킬을 제작 해금 UI에도 노출하려면 `DA_ItemData`의 Area Slow 스킬 정의와 제작 Recipe/UI 항목을 별도로 추가한다. 이 콘텐츠는 게임 진행 기획에 종속되므로 C++에서 임의의 Recipe를 만들지 않는다.

## 7. PIE 네트워크 확인

Dedicated Server, 2 Clients로 확인한다.

1. Client 1이 능력 키를 Hold한다.
2. 조준 Decal이 Client 1에만 보이고 Client 2에는 보이지 않는지 확인한다.
3. Client 1이 좌클릭한다.
4. 확정 Decal이 Client 1과 Client 2에 모두 보이고 설정 시간 후 사라지는지 확인한다.
5. 범위 안 opt-in Enemy의 이동과 기본 공격 몽타주가 각각 설정한 배율로 느려지는지 확인한다.
6. Client 2의 Player가 범위 안에 있어도 느려지지 않는지 확인한다.
7. 감속된 Enemy를 범위 밖으로 이동시켜도 개인 지속시간까지 감속되는지 확인한다.
8. 확정 후 범위에 들어온 Enemy는 느려지지 않는지 확인한다.
9. `Net PktLag=150`, `Net PktLoss=3`에서도 빠른 좌클릭 확정이 유실되지 않는지 확인한다.
10. `bDrawServerDebugBox`를 잠시 켜 Decal Length/Width와 서버 판정 Box가 일치하는지 확인한 뒤 다시 끈다.

## 8. 흔한 설정 오류

- Preview도 다른 클라이언트에 보임: Targeting Decal Blueprint에서 Replicates를 켜지 않았는지 확인
- 확정 Decal이 안 보임: Confirmed Material의 Material Domain과 DA 할당 확인
- Tag를 넣었는데 대상이 안 맞음: Capability Tag, ASC Attribute, Collision Object Type을 함께 확인
- 감속 Tag는 생기는데 움직임이 안 느려짐: 해당 Actor 이동 코드가 `MoveSpeedMultiplier`를 소비하지 않음
- 감속 Tag는 생기는데 공격이 안 느려짐: 해당 Actor의 공격 Ability가 `AttackSpeedMultiplier`를 소비하지 않거나 기본 공격 Tag를 소유하지 않음
- 좌클릭 시 공격도 같이 나감: Area Slow 활성 Tag가 실제 ASC에 생기는지, 마우스 입력이 `ABasePlayer::OnMouseInputPressed`를 통하는지 확인
- Ability가 즉시 종료됨: GA Blueprint의 `SkillData` 지정과 Data Asset 유효성 로그 확인
