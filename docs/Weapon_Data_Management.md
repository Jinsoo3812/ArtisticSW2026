# 무기 데이터 관리

무기별 수치는 `/Game/Blueprints/Item/Data/DT_WeaponBalance`의 20행에서 편집한다. 공통 외형·어빌리티·애니메이션은 `/Game/Blueprints/DAs/Weapon_DAs/Profiles`의 `WPF_SwordA`, `WPF_SwordB`, `WPF_ShortBow`, `WPF_LongBow` 네 프로필에서 관리한다.

| DT 열 | 의미 |
| --- | --- |
| ItemTag | 기존 인벤토리·드랍·저장이 사용하는 아이템 식별자 |
| Profile | 공유할 액터·어빌리티·애니메이션·투사체 프로필 |
| StrengthBonus | 캐릭터 힘에 더하는 무기 공격력 보너스 |
| AttackCoefficient | 피해 계산에 전달하는 공격 계수 |
| BasicAttackPlayRate | 칼 기본 공격 몽타주 배속 |
| AimCyclePlayRate | 활 당기기·발사 몽타주 배속; 입력 대기 상태는 유지 |

`DataTable/WeaponBalance.csv`는 같은 데이터의 편집·리뷰용 CSV이다. 런타임은 DT 에셋을 읽는다. CSV를 수정하면 DT에 다시 가져와야 하고, 에디터에서 DT를 수정했다면 CSV도 다시 내보내 함께 갱신한다. 새 플레이 세션을 시작할 때 월드의 무기 캐시를 새로 만든다. 진행 중인 전투에 수치를 실시간 적용하는 기능은 아니다.

2026-10-28 수치는 실제 적 HP·플레이어 기본 힘·GAS 공식·공격 모션으로 조정했다. 기준, 단계별 타격 수와 처치 시간, 계산 한계는 [전투 밸런스 보고서](Weapon_Combat_Balance_2026-10-28.md)를 참고한다.

## 직접 참조와 런타임

- `DA_ItemData`의 무기 항목은 `WeaponBalanceRow`로 DT 행을 직접 참조한다. 개별 WD를 거치지 않는다.
- 월드별 `UItemSubsystem`이 공통 프로필과 DT 행을 조합하고 생성한 런타임 전용 객체를 소유한다. 서버·클라이언트 및 새 PIE 세션마다 별도의 캐시를 만든다.
- 기존 장착·공격·UI는 `ABaseItem::GetWeaponDefinition()` API를 유지하며 내부에서 월드 캐시를 조회한다. 반환 타입 이름은 같지만 개별 DA 파일이 아니다.
- DT 행의 ItemTag가 카탈로그 항목과 다르거나 데이터가 잘못되면 무효화하고 오류를 남긴다.
- 개별 WD 20개, WCD 20개, WDA 16개는 참조 이관·외부 참조 확인 후 삭제했다. `Definitions` 폴더의 WAS 4개는 공통 프로필에서 재사용하는 어빌리티 세트이므로 유지한다. 공유 애니메이션 DA 3개도 유지한다.
- 아이템 태그·등급·제작식·소비 아이템·스킬 재료 드랍·저장 구조는 변경하지 않았다. 카탈로그에서는 무기 데이터 참조만 바뀌었다.

기존 `SwordA2`는 이름과 달리 칼 B 액터·콤보를 사용하고 있었다. 동작 보전을 위해 이 행만 `WPF_SwordB`를 가리킨다. 나머지 행은 이름에 해당하는 공통 프로필을 사용한다. 공격력과 속도는 모두 이관 전 값 그대로다.

## 새 무기 추가

1. DT에 고유 행과 ItemTag, 프로필, 수치를 추가한다.
2. `DA_ItemData`의 아이템 정의에서 WeaponBalanceRow를 그 행으로 지정한다. 개별 WD·WCD·WDA 파일은 생성하지 않는다.
3. 새로운 외형·동작이 필요한 종류만 공통 프로필을 추가한다. 제작·드랍 등록은 기존 시스템에서 별도로 관리한다.
