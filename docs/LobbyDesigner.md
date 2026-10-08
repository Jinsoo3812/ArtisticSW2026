# 로비 WBP 편집

콘텐츠 브라우저에서 `/Game/Blueprints/02_UI/UI_Lobby/WBP_ConnectionLobby`를 엽니다.
부모 클래스는 `SWConnectionLobbyWidget`입니다. C++에서 Slate 레이아웃을 생성하던 코드를 제거했고, 실제 WBP Widget Tree가 화면에 표시됩니다.

## 디자이너에서 수정하는 것

- 배경: `LobbyBackdrop`의 Brush Color / Brush Image. 기존 배치가 있었다면 `LobbyDesignerRoot` 아래에 보존됩니다.
- 전체 폭: `LobbyContentWidth`의 Width Override.
- 제목·안내·버튼 문구: 해당 Text Block의 Text / Font / Color.
- 버튼·입력창·패널: 위치·크기·패딩·스타일. 필요하면 Canvas 등 다른 부모 컨테이너로 옮겨도 됩니다.
- 공통 상태 문구: `StatusText`의 모양과 위치. Text 내용은 방 상태에 따라 자동 갱신됩니다.

## 자동 연결되는 이름

| 이름 | 기능 |
|---|---|
| LobbySwitcher | 홈·입력·확인 화면 전환. C++이 각 패널을 포함하는 직계 자식을 선택 |
| HomePanel | 처음 보이는 메뉴 |
| FormPanel | 방 만들기·이어하기·참가 입력 화면 |
| ConfirmPanel | 기존 저장을 새 방으로 교체하기 전 확인 화면 |
| NameSection | 이름 입력 영역. 이어하기에서는 숨김 |
| JoinSection | 참가 코드·입장 버튼 영역 |
| HostSection | 호스트용 시작·서버 접속 영역 |
| ManualIPSection | 자동 IP 조회 실패 시 표시 |
| HostedRoomSection | 방 코드가 준비되면 표시 |
| CreateButton / ContinueButton / JoinButton | 각 입력 화면으로 이동 |
| QuitButton | 게임 종료 |
| AutoHostButton | 새 방 생성 또는 이어하기. 실패 후 자동 재시도 |
| ManualHostButton | 입력한 공인 IP로 새 방 생성 또는 이어하기 |
| JoinSubmitButton | 이름과 참가 코드로 방 입장 |
| ConnectHostButton | 준비된 호스트 서버에 접속 |
| ConfirmCreateButton / ConfirmCancelButton | 새 방 교체 확인 / 취소 |
| BackButton | 진행 중인 작업 취소 후 홈 메뉴 |
| NameInput / CodeInput / PublicIPInput | Editable Text Box 입력 필드 |
| RoomCodeOutput | 복사 가능한 읽기 전용 참가 코드 |
| StatusText | 서버 준비·오류 등 상태 메시지 |

위 이름과 위젯 타입을 유지하면 Event Graph 작업 없이 연결됩니다. 버튼의 자식 Text Block 이름이나 문구는 자유롭게 변경해도 됩니다. 자동 연결된 버튼에 같은 기능의 OnClicked 이벤트를 추가하면 중복 실행되므로 추가하지 마세요.

`LobbySwitcher`에서 홈·FormPanel·ConfirmPanel을 전환합니다. ConfirmPanel과 내부 ConfirmPanel1은 기본 Hidden이며 확인 화면 진입 시 표시됩니다. 디자이너에서 편집할 때 해당 Switcher 페이지를 선택하고 패널을 Visible로 바꿔 확인할 수 있습니다. 실행하면 C++에서 현재 메뉴에 맞춰 표시 상태를 다시 설정합니다. 각 Section은 해당 Panel 아래에 유지하세요.

이름 입력은 방 만들기·방 들어가기에서 같은 위치에 표시하고 이어하기에서만 숨깁니다. HostSection과 JoinSection은 동일한 위치에 배치되어 필요한 섹션만 표시됩니다. 섹션 크기와 내부 요소는 유지합니다.

## 보존 및 검증

마이그레이션 전 에셋은 `Saved/DesignerBackups`에 백업합니다. 기존 루트는 새 Overlay 아래에 보존하며 기존 위젯·그래프를 삭제하지 않습니다. `SWLobbyDesignerSetup` 커맨드릿은 초기 배치를 추가하고 컴파일, 이름 바인딩, 참가/뒤로 화면 전환 및 입력값 보존을 검증합니다. 이미 변환한 에셋의 레이아웃은 다시 만들지 않습니다.

게임 내 확인: ConnectionLobby에서 Standalone 실행 → 방 만들기/뒤로 → 방 들어가기/뒤로 → 방 생성 및 서버 접속. 이어하기는 저장된 방이 있을 때 표시됩니다. 기존 저장 교체 확인과 수동 IP 입력은 해당 조건에서 표시됩니다.
