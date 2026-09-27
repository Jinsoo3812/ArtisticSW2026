# 플레이어 무기 장착·공격 파이프라인

## 1. 단일 설정 경로

```text
DA_ItemData.ItemDefinitions[정확한 ItemTag]
  └─ WeaponDefinition (EquippableWeaponDefinition)
       ├─ ActorClass    : 장착용 무기 BP
       ├─ AbilitySet    : InputTag + Weapon GA + Level
       ├─ AnimationData: 몽타주·콤보 섹션·소켓·애니메이션 레이어
       ├─ CombatData   : StrengthBonus·AttackCoefficient·ProjectileClass
       ├─ AllowedRoleTags: 사용 가능한 역할 (비어 있으면 제한 없음)
       └─ EquipEffects : 장착 중 추가 효과

서버 장착 → EquipmentComponent → PlayerState.ASC에 GA 부여
입력 태그 → ASC의 정확한 Spec 태그 매칭 → 무기 GA → 서버 판정·피해
장착 해제/파괴/사망/EndPlay → 해당 장착의 Spec/Effect 핸들만 회수
```

무기 BP에 GA 목록을 넣지 않는다. BP의 `ItemTag`가 가리키는 아이템 행에서
`WeaponDefinition → AbilitySet`을 열어 공격 GA를 설정한다.
태그는 정확히 일치해야 한다. 상위 아이템 태그의 정의를 자동 상속하지 않는다.

`UEquippableWeaponDefinition`이라는 이름은 Enemy 모듈에 이미 존재하는
`FWeaponDefinition`과 Unreal 리플렉션 이름 충돌을 피하기 위한 것이다.

## 2. 책임 분리

| 구성 요소 | 책임 | 맡지 않는 책임 |
|---|---|---|
| ItemData / ItemSubsystem | 아이템 식별·인벤토리 메타데이터, 정의에 따른 액터 스폰 | 무기 GA 부여·공격 실행 |
| WeaponDefinition / AbilitySet / CombatData | 재사용 가능한 정적 설정 | 런타임 ASC 핸들 보관 |
| PlayerEquipmentComponent | 서버 장착 상태, 설정 검증, 부여/회수, 부착, 클라이언트 연출 복구 | 입력 해석·피해 계산 |
| EquipmentStatComponent | Strength 장착 효과의 유일한 소유자 | GA 입력·추가 장착 효과 |
| WeaponInputAbilitySystemComponent | 정확한 입력 태그 라우팅, Press/Release, GAS 입력 이벤트 | 무기 종류별 분기·무기 스폰 |
| WeaponGameplayAbility | 현재 장착된 SourceObject만 활성화하도록 검증 | 장착·영구 GA 부여 |
| BasicAttack / BowAimFire | 콤보·조준·발사 상태와 타이밍, 피해 Spec 생성 | 아이템 목록/인벤토리 관리 |
| SwordItem / BowComponent / ArrowProjectile | 소켓 연결, 발사 속도·방향, 투사체 이동·충돌 | GA를 플레이어에게 부여 |
| GASCombatLibrary / CombatHitResolver | Strength 기반 피해 계산·대상 판정·GE 적용 | 무기 선택·입력 |
| WeaponAnimationDataAsset | 몽타주·섹션·레이어·부착 소켓 설정 | GA 목록 보관 |

공통 데이터는 ArtisticSWCore, 입력 ASC는 GASCore, 플레이어 장착/GA는 ClassFeature에 둔다.
애니메이션 데이터 클래스를 Core로 옮겼으며 기존 에셋은 `DefaultEngine.ini`의
ClassRedirect/StructRedirect로 읽는다. Player BP의 무기별 애니메이션 매핑은 더 이상 사용하지 않는다.

## 3. 장착 트랜잭션과 수명

1. 서버가 인벤토리 수량·전환 상태·행동 제한을 검사한다.
2. 아이템 정의의 ActorClass로 장착 후보를 스폰한다. 정의가 없는 무기는 BaseItem으로 대체하지 않는다.
3. 장착 몽타주/Attach Notify를 거쳐 정의·역할·GA·몽타주·투사체를 검증한다.
4. 부착을 확인하고 새 GA/추가 GE를 준비한다. 아직 현재 무기가 아니므로 GA 활성화는 차단된다.
5. 기존 EquipmentStatComponent가 새 Strength 효과를 적용한다.
6. 이전 무기의 핸들만 취소/회수하고 액터를 파괴한다. 현재 무기를 새 액터로 바꾼다.
7. 전환 상태를 해제하고 UI/전투 모드를 갱신한다.

검증·부착·GE 적용 실패 시 새 후보의 부여분을 회수하고 후보를 파괴한다.
기존 장착은 유지하며, 후보 부착 과정에서 바뀌었을 수 있는 기존 활 앵커도 다시 부착한다.
같은 액터에 반복 부여해도 중복 Spec을 생성하지 않는다.

각 장착 기록에는 부여 대상 ASC, AbilitySpecHandle 배열, ActiveGameplayEffectHandle 배열이 있다.
회수는 입력 태그나 GA 클래스 전체를 삭제하지 않는다. 같은 키/클래스를 쓰는 다른 출처의 GA는 보존한다.
파괴·액터 EndPlay·장비 컴포넌트 EndPlay·State.Dead에서도 회수한다.
이는 Pawn보다 오래 살아남는 PlayerState ASC에 이전 무기의 GA가 남는 것을 방지한다.

추가 EquipEffects는 Infinite, 비스택 효과만 허용한다. Strength 장착 GE 및 Strength Modifier는
이 배열에 넣을 수 없다. StrengthBonus는 CombatData에 설정하고 EquipmentStatComponent가 적용한다.
장착 효과에는 일회성 지급·아이템 생성 등 되돌릴 수 없는 실행을 넣지 않는다.

## 4. 입력과 공격

`FGameplayAbilitySpec.InputID`는 사용하지 않는다. 입력은
`Spec.GetDynamicSpecSourceTags()`의 정확한 GameplayTag로 찾는다.
PlayerState는 기존 ASC 서브오브젝트 이름을 유지하면서 태그 입력 ASC를 생성한다.
일반 스킬/상호작용의 GrantAbilityToSlot도 같은 입력 체계를 사용한다.

같은 태그에 아이템과 캐릭터 기본 바인딩이 동시에 있으면 누르기는 아이템 바인딩을 우선한다.
놓기는 관련 Spec에 전달해 입력 상태가 고착되지 않게 한다.
Enhanced Input의 Completed와 Canceled 모두 마우스 해제로 처리한다.
활의 WaitInputRelease가 동작하도록 활성 인스턴스의 prediction key로 GAS 입력 이벤트를 보낸다.

무기 GA의 SourceObject는 장착 무기 액터다. WeaponGameplayAbility는 해당 액터가 유효하고,
플레이어의 현재 EquippedItem이며, 무기 정의가 있고, 장착 전환 중이 아닌지 검사한다.
검과 활은 실행 중에도 캐시한 무기가 현재 장착 무기인지 확인한다.
DefaultGrantedAbilities, DefaultAbilityMap, GrantAbilityToSlot에는 WeaponGameplayAbility 계열을 부여할 수 없다.
AttackerComponent는 역할 태그/역할 입력 컨텍스트만 담당하며 GA 맵은 제거했다.

### 검

좌클릭 → BasicAttack 활성화 → 정의의 공격 몽타주/콤보 섹션 → 몽타주 HitScan 이벤트
→ 서버 SwordItem 트레이스 → Strength 피해 Spec 적용.
공격 계수는 WeaponCombatDataAsset의 AttackCoefficient를 사용한다.
같은 타격 구간의 중복 피해 방지와 상태효과/피드백은 기존 판정 코드를 유지한다.

### 활

우클릭 → BowAimFire 활성화 → 조준/Draw/Hold/Release 사이클 → 좌클릭 발사 요청
→ Nock/Fire Notify 및 검증된 조준 위치 확인 → 서버에서 정의의 ProjectileClass 스폰.
공격 계수는 CombatData, 충전 배율은 기존 BowAimFire 설정을 사용한다.
클라이언트는 조준·몽타주를 예측하고 투사체/피해 생성은 서버만 수행한다.

초기 계획의 Aim/Fire GA 분리는 이번에 적용하지 않았다. 기존 단일 몽타주 사이클,
WaitInputRelease, 충전·발사 검증을 유지하면서 **부여/활성화 출처를 단일화**했다.
좌클릭/릴리즈 GameplayEvent는 실행 중 GA의 콤보·발사 명령이지 두 번째 GA 부여 경로가 아니다.
이 이벤트는 다른 스킬도 사용하므로 삭제하지 않았다.
기존 SwordItem 트레이스도 별도 범용 실행기 클래스로 재작성하지 않고 기존 판정 책임을 유지했다.

## 5. 실제 이관 에셋

경로: `/Game/Blueprints/DAs/Weapon_DAs/Definitions/`
각 무기는 `WD_*`, `WAS_*`, `WCD_*` 3개 에셋을 갖는다.

| 아이템 태그 끝부분 | Actor BP | AnimationData | 입력/GA |
|---|---|---|---|
| SwordA1 | BP_BaseSwordA | WDA_SwordA | 좌클릭 / BPGA_BasicAttack |
| SwordA2 | BP_BaseSwordB | WDA_SwordB | 좌클릭 / BPGA_BasicAttack |
| ShortBow1 | BP_Bow | WDA_Bow | 우클릭 / BPGA_Bow_AimShoot |
| LongBow1 | BP_Bow | WDA_Bow | 우클릭 / BPGA_Bow_AimShoot |

SwordA2 → BP_BaseSwordB는 실제 기존 콘텐츠의 태그 연결이다. 이름만 보고 SwordB 태그로 바꾸지 않았다.
끊어진 과거 검 BP 경로는 현재 BP_BaseSwordA/B로 수정했다.
기존 BP의 StrengthBonus, 검 계수/화살 DamageData 계수, 투사체 클래스와 사용 역할을 복사했다.
이관한 아이템 행의 GrantedAbilityClass/UseKeyTag/SpawnClass/SpawnClassByCrafting/CanUseClassList는 비웠다.
소비 아이템과 도구의 기존 필드는 유지한다.

나머지 미설정 무기 티어의 밸런스나 GA는 추정해서 만들지 않았다.
이들은 WeaponDefinition을 작성해야 장착할 수 있다. 태그만 있다고 공격 가능한 무기가 되는 것은 아니다.
사용자가 수정 중이던 Player_Woman·적·레벨 에셋은 이 작업에서 저장하지 않았다.

재실행 가능한 이관 스크립트: `Scripts/Python/migrate_weapon_definitions.py`.
기존 WD를 발견하면 디자이너가 수정한 정의/수치를 덮어쓰지 않고 연결만 확인한다.

## 6. 새 무기 추가 순서

1. 무기 BP를 만든다. 검은 SwordItem, 활은 BowItem 기반으로 메시·트레이스·소켓을 설정한다.
2. WeaponCombatDataAsset을 만들고 StrengthBonus·공격 계수·필요한 투사체 클래스를 설정한다.
3. WeaponAnimationDataAsset을 만들고 공격/조준 몽타주, 섹션, 소켓과 레이어를 설정한다.
4. WeaponAbilitySet에 입력 태그와 WeaponGameplayAbility 파생 GA를 연결한다. 한 Set에서 입력 태그 중복은 금지한다.
5. EquippableWeaponDefinition에 위 에셋과 ActorClass를 연결한다. 필요하면 역할/추가 장착 효과를 설정한다.
6. DA_ItemData의 정확한 아이템 행에 WeaponDefinition을 지정하고 BP ItemTag와 일치시킨다.
7. 인벤토리에 해당 아이템을 넣고 퀵슬롯 장착을 통해 테스트한다. Player/Attacker/AnimationData에 GA를 추가하지 않는다.

새 공격 유형은 WeaponGameplayAbility에서 파생하고 `GetSourceWeapon()`으로 출처를 얻는다.
장착 컴포넌트에 무기 이름별 GA 분기를 추가하지 않는다. 무기 실행/판정 구현이 필요하면 해당 액터/컴포넌트로 확장한다.

## 7. 검증

추가 자동화 테스트는 `ArtisticSW.Weapon.DefinitionAssets`, `ArtisticSW.Weapon.EquipmentLifecycle`이다.
에셋 연결·Player BP 로드·ASC 타입·중복 부여·SourceObject·태그 입력·기존 부여 경로 차단·실패 시 기존 핸들 보존·
정확한 회수·파괴·사망 정리를 확인한다. 수명주기 단위 테스트는 관련 없는 제작 테이블을 임시 분리한다.

검증 결과 (UE 5.7.4 / Win64 Development Editor):

- 에디터 C++ 빌드 성공.
- DefinitionAssets, EquipmentLifecycle, DamageFormula, DamageSpecSnapshot: 4개 통과.
- 최종 보완 후 재실행에서도 4개 모두 통과. 로그: `Saved/WeaponRefactorFinalTests.log`.
- 전체 Strength 실행은 통과하지 않았다. `Saved/WeaponRefactorTests.log`에서 QuestItem 제작 행의
  유효하지 않은 태그/재료 오류와 MeleePayload의 상태효과 기대값 불일치(기대 85, 실제 90)를 확인했다.
  이번 변경에서 해당 제작 콘텐츠나 기존 상태효과 테스트의 기대값은 수정하지 않았다.
- 멀티플레이 PIE·패키지 Cook 검증은 수행하지 않았다.

PIE에서 추가 확인할 항목:

- Listen Server와 원격 클라이언트 각각 검 콤보 및 활의 우클릭 유지/해제, 좌클릭 발사.
- 장착 몽타주 중 교체 요청, 공격 중 교체 요청, 공격 중 사망/리스폰.
- 늦게 접속한 클라이언트에서 무기 소켓·활 앵커·레이어 복원.
- 네트워크 지연 상태에서 중복 투사체/중복 피해가 없는지 확인.
- 소비 아이템, GravityVortex/AreaSlow의 마우스 점유와 F 상호작용 회귀.

자동화 결과는 위 멀티플레이 수동 검증을 대신하지 않는다.


## 플레이어 활: 화면 중앙 조준과 소켓 발사

- `PlayerAimComponent`는 실제 로컬 뷰의 중앙을 월드 조준선으로 변환한다. 분할 화면과 화면 비율 제약을 반영하며 조준선 원점·방향을 전용 `FGameplayAbilityTargetData_ViewRay`로 입력 해제 이벤트에 함께 담는다. `BasePlayer`는 입력 전달만 담당한다.
- `GA_BowAimFire`는 같은 입력 이벤트에서 조준점과 발사 속도를 확정한다. 서버는 시선 원점 거리·방향을 검증하고 `WeaponAim` 트레이스로 첫 충돌 지점 또는 최대 거리 끝점을 결정한다. 조준 데이터에 별도 RPC나 클라이언트 피격 결과를 사용하지 않는다.
- 발사 노티파이는 확정된 조준 데이터가 준비된 Release 구간에서만 소비한다. 취소·장비 교체·능력 종료 시 발사 정보를 지운다. 버튼 해제 이후 카메라를 움직여도 해당 발사의 조준점은 유지된다.
- `BowItem`은 캐릭터 `Arrow_socket`을 장전 표시와 발사 위치로 제공한다. `BowComponent`는 현재 소켓 위치에서 확정된 조준점으로 향하는 초기 속도·회전을 만든다. 소켓 +X 방향 고정 발사와 탄도 역산은 없다. 뒤쪽/너무 가까운 조준점은 확정된 시선 전방으로 발사한다.
- 표시용 DrawAlpha 초기화는 저장한 발사 속도와 피해 배율에 영향을 주지 않는다. 투사체 충돌 루트에는 캐릭터 스케일을 전달하지 않는다.
- `ArrowProjectile`은 자신의 실제 충돌 박스·프로파일로 발사 위치 겹침을 검사한다. 소유자·활은 무시하고, 장애물과 겹치면 그 발사는 차단한다. 정상 발사 뒤에는 투사체 이동·충돌·피해만 처리하며 목표 추적은 없다.
- 중력은 `FlightGravityScale`에서만 설정한다. 0이면 목표 방향으로 직진하고 양수이면 자연 낙하한다. 중력을 보상해 조준점에 명중시키는 기능은 포함하지 않는다.

에디터 수동 확인: 근거리·원거리·허공을 중앙으로 조준해 발사하고, 벽 모서리/소켓 겹침, 상하 조준/줌, 버튼 해제 후 시점 이동, 취소·장비 교체를 확인한다. PIE 2명에서 원격 플레이어도 같은 규칙으로 한 발씩 발사되는지 확인한다. 이번 변경에서는 자동화 테스트를 실행하지 않는다.
