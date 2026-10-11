# 보스 기절 VFX

머리 위에서 금빛 별 3개가 회전하고 조금씩 부유하며 크기가 변한다. 카메라를 향하는 평면 메시와 절차적 별 머티리얼로 구현하여 외부 텍스처나 Niagara 시스템이 필요하지 않다.

## 동작과 책임

- 기존 `UBossHeadHitStunEffect` / `UBossHealthThresholdStunEffect`와 상태 컴포넌트가 기절 판정, 지속 시간, 해제를 담당한다. 머리 피격 기절 기본 시간은 4초이며, 현재 판정은 공격 중 `head`에 들어온 직접 피해에만 적용된다. 체력 임계치 기절 기본 시간은 4초다. 두 시간은 보스 Blueprint의 Boss / Status에서 개별 조정한다. 자세한 설정은 Samurai_Boss_Timing.md를 참고한다.
- `BossStunGameplayCue.h/.cpp`: 표현만 담당한다. 기존 `GameplayCue.Status.Stun`의 시작/지속 이벤트에서 보스인지 확인하고 별을 표시한다. 머리 위치를 매 프레임 따라가며, Cue 해제/재사용 또는 보스 사망/소멸 시 숨긴다. 일반 캐릭터의 기절에는 별을 표시하지 않는다.
- `Content/GameplayCues/Boss/Stun/GC_Boss_Stun.uasset`: 위 네이티브 클래스의 Blueprint. 기존 Stun Cue 태그와 머티리얼, 표시 기본값을 보관한다.
- `Content/GameplayCues/Boss/Stun/M_BossStunStars.uasset`: 양면 Masked / Unlit 머티리얼. UV에서 5각 별 모양을 만들고 색과 발광을 적용한다.
- `Scripts/Editor/author_boss_stun_vfx.py`: 두 에셋 생성용 Editor Python 스크립트. 다시 실행하면 별 머티리얼 그래프와 Cue 연결을 재설정하므로 수동으로 수정한 머티리얼에는 실행하지 않는다.

기존 `/Game/GameplayCues` 탐색 경로를 사용하므로 보스 Blueprint, BT, 기절 Ability에 추가 연결할 필요는 없다. 실제 기절 상태를 표현하므로 머리 피격 외에 기존 체력 임계치 기절에도 같은 별이 표시된다.

## Editor 설정 및 수동 확인

1. 새 C++ 클래스가 반영되도록 빌드된 Editor를 다시 실행한다.
2. Content Browser에서 `/Game/GameplayCues/Boss/Stun/GC_Boss_Stun`을 열고 Class Defaults의 `Stun VFX` 항목을 확인한다.
   - `Star Material`: `M_BossStunStars`
   - `Head Bone`: `head` (다른 보스의 뼈/소켓 이름이 다르면 해당 이름으로 변경)
   - `Height Above Head`: 38 cm
   - `Orbit Radius`: 32 cm
   - `Star Size`: 18 cm
   - `Orbit Speed`: 160 deg/s
   - `Star Color`: 금색 / `Glow Intensity`: 5
3. 모자와 겹치면 `Height Above Head`를 늘리고, 너무 크면 `Star Size` 또는 `Orbit Radius`를 줄인다. 별의 색/발광은 Cue 기본값에서 조절한다. Bloom이 꺼져 있어도 별 자체는 표시되지만 주변의 빛 번짐은 줄어든다.
4. PIE에서 Samurai가 **공격 중일 때 머리**에 직접 피해를 준다. 별이 머리를 따라 움직이고 기본 4초 기절 종료와 함께 사라지는지 확인한다. 공격 중이 아닐 때는 기존 판정에 따라 머리 피격 기절이 발생하지 않을 수 있다.
5. 기절 중 사망 시 즉시 사라지는지, 재기절 시 중복되지 않는지, 체력 임계치 기절 동안에도 유지되는지 확인한다. 일반 적의 기절에는 별이 없어야 한다. 멀티플레이 사용 시 서버/클라이언트 양쪽에서도 확인한다.

## 확인 범위

Editor Development 빌드 성공. 에셋 생성 및 새 Editor 프로세스에서 재로드하여 Stun Cue 등록 이름, 머티리얼 연결, 표시 기본값을 확인했다. 자동화 테스트 및 PIE 실행은 진행하지 않았다. Null RHI에서 에셋을 작성했으므로 실제 화면의 발광과 배치는 위 수동 절차로 확인한다.
