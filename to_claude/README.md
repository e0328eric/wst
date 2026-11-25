# Glosso에서 Visual Studio 네이티브 줄 중단점 지원하기

Claude에게 맡길 구현 작업이다. Windows x64 MSVC 대상의 `.glo` 프로그램을 Glosso로 빌드하고, **Visual Studio IDE에서 원본 `.glo` 파일을 열어 F9를 누른 줄에서 F5 실행이 실제로 멈추게 한다.** 작은 두 파일 예제로 먼저 증명한 뒤 `wst`의 앱 진입점에서도 재현한다.

첫 버전은 `-O0`에서 실행문 단위 중단점, F10 한 줄 진행, 다른 Glosso 함수로 F11 진입까지 다룬다. 지역변수 값·타입 표시, Glosso 식 평가기, 최적화된 인라인 호출 스택은 후속 작업으로 둔다. Visual Studio와 DIA는 검증 도구로 쓰고, 배포용 Glosso는 기존의 자체 코드 생성기·어셈블러·PE 링커로 디버그 빌드를 완성한다.

## 1. 시작 위치와 현재 동작

| 위치 | 실제로 확인된 동작 |
|---|---|
| `../glosso-qbe` | 구현할 컴파일러 체크아웃. `src/backend/gloqbe`, `src/asm`, `src/link/pe.rs`를 갖고 있다. `../glosso`에는 LLVM 계열 코드도 있으므로 저장소 이름만으로 구현 대상을 정하지 않는다. 작업 트리에 진행 중인 수정이 많다. `git status --short`와 해당 파일의 diff부터 확인해 현재 작업 위에 변경을 얹는다. |
| `../glosso-qbe/src/ir_types.rs`, `src/ir.rs` | `Instruction.site`의 `DebugSite { file, line, col }`에 위치가 있다. `site_for`가 노드의 진단용 원본 경로를 우선하고 `with_site`가 해당 위치를 IR 생성 중에 유지한다. |
| `../glosso-qbe/src/backend` | `core.rs`에서 `state.current_site`를 설정하지만 `api.rs`의 `IrSetCurrentDebugLocation2`는 빈 함수이고 getter는 null을 돌려준다. `gloqbe.rs`의 `module_to_object`가 어셈블리 생성 후 자체 어셈블러를 호출한다. |
| `../glosso-qbe/src/asm/mod.rs`, `src/link/pe.rs` | 어셈블러는 현재 `.file`과 `.loc`를 읽고 무시한다. PE 링커는 `/debug`에서 EXE에 `RSDS`를 쓸 수 있지만 PDB 파일을 작성하지 않는다. 설치본에서도 `glosso link -h`로 확인했다. |
| `wst/first.glo` | `build`가 `compile(root, "src/main.glo", "wst", !console)`를 호출한다. 이 함수가 **자식 컴파일**의 `optimization_level`을 `.O3`로 설정한다. `--console`은 PE 서브시스템 선택 옵션이다. |

컴파일러 루트에서 `.\build.exe dev`로 빌드하고 이후 시험에는 그 결과인 `build\bin\glosso.exe`의 절대 경로를 쓴다. `dev`는 Rust로 작성된 컴파일러 자체의 빌드 프로필이다. 아래의 `-g`와 `build --debug`는 **이번 작업으로 제공할 인터페이스**다.

## 2. 첫 번째 이정표 — 두 파일로 결과를 판정한다

같은 디렉터리에 두 파일을 만든다. 줄 번호는 아래 코드 블록의 첫 줄을 1로 센다. 이 예제는 현재 설치된 Glosso에서 `-O0`으로 빌드해 인자 없이 실행했을 때 `input=1`과 `value=3`을 차례로 출력하는 것까지 확인했다.

**main.glo**

```glo
#import "Context";
#import "Format";
#import "Io";
H :: #import "helper.glo";

main :: () -> s32 {
    args := get_cmdline_args();
    value := H.bump(cast(s32) args.count);
    print("value={}\n", value);
    return 0;
}
```

**helper.glo**

```glo
#import "Format";
#import "Io";

#public bump :: (n: s32) -> s32 {
    print("input={}\n", n);
    return n + 2;
}
```

구현 후 예제 디렉터리에서 아래처럼 실행한다. PowerShell 변수 `$glo`는 방금 빌드한 컴파일러의 절대 경로로 설정한다.

```powershell
& $glo compile -O0 -g --backend object main.glo -o probe.obj
& $glo compile -O0 -g main.glo -o probe.exe
.\probe.exe
```

기대 결과는 `probe.obj` 안의 두 원본 파일에 대한 CodeView 줄 정보, 그리고 두 번째 명령이 생성하는 `probe.exe`와 `probe.pdb` 한 쌍이다. 첫 중단점은 `main.glo` 8행의 `H.bump(...)` 호출, 두 번째는 `helper.glo` 5행의 `print(...)`다. 두 줄이 다 바인딩되고 각각의 파일에서 멈춰야 한다.

## 3. 컴파일러와 백엔드 — 파일 ID를 명령에 붙여 끝까지 보낸다

1. `src/driver.rs`의 `DriverOptions`와 옵션 파서·도움말, `src/main.rs`의 `emit_options_for`, `src/backend/mod.rs`의 `EmitOptions`에 줄 정보 모드를 전달한다. `-g`가 디버그 정보를 요청하고 `-O0`이 최적화 수준을 정하게 한다. Windows x64 MSVC의 object 출력에서는 CodeView를, exe 출력에서는 CodeView와 최종 PDB를 요청한다. 최종 링크 요청은 `src/linker.rs`의 기존 링크 계획을 통과시킨다. 지원 범위 밖 조합은 도움말과 진단에 명시한다.
2. `src/backend/core.rs`의 `lower_instruction` 호출 직전, `instruction.site.known()`이면 해당 위치를 빌더에 설정하고 미상이면 해제한다. `src/backend/ir.rs`의 빌더·백엔드 명령·종결 명령까지 위치를 연결한다. 경로는 파일 테이블에 한 번만 저장한다. 런타임 헬퍼, 프롤로그, 컴파일 타임 전용 코드에서 직전 사용자 코드의 위치가 새어 나가지 않게 한다. 헬퍼 생성 중 빌더를 전환하는 기존 save/clear/restore 호출도 실제로 동작해야 한다.
3. `src/backend/lower.rs`에서 살아 있는 백엔드 명령의 위치가 바뀔 때만 gloqbe에 위치를 전달한다. `src/backend/gloqbe/parse.rs`의 `dbgfile`·`dbgloc`, `driver.rs`의 `Driver::func`, `emit.rs`의 `emitdbgfile`·`emitdbgloc`, `amd64/emit.rs`의 `Odbgloc` 처리를 따라간다. **파일 ID를 위치마다 보존한다.** 현재 드라이버는 파싱 시 전역 `curfile`을 갱신하고 함수는 큐에 쌓아 나중에 출력한다. 마지막 파일의 번호가 모든 함수에 붙는 경우를 테스트한다. 확장된 IL을 쓴다면 기존 `dbgloc` 문법도 계속 읽게 한다.
4. 기계 명령을 생성하지 않는 선언·주석에 가짜 주소를 주지 않는다. 같은 위치에서 나온 연속 명령은 한 줄 범위로 표현하고 다른 파일·함수·주소 범위까지 합치지 않는다. `#insert` 등 생성 코드의 경로는 실제로 조회 가능한 원본 또는 보존된 생성 파일과 연결할 때만 기록한다.

## 4. 어셈블러 — 최종 기계어 오프셋으로 CodeView 오브젝트를 만든다

- `src/asm/mod.rs`에서 Windows 디버그용 `.file`과 `.loc`를 수집한다. 위치는 명령 앞의 코드 조각과 그 내부 위치로 보관하고, `layout`의 x64 분기 길이 선택과 정렬이 끝난 후 `finish`에서 실제 바이트 오프셋을 확정한다. 함수 시작·끝을 명시해 다음 함수 앞 패딩이나 점프 테이블이 이전 줄의 범위로 잡히지 않게 한다. 리프 함수도 포함한다.
- `src/asm/coff.rs`에서 C13 서명 `4`를 가진 `.debug$S`를 출력한다. `DEBUG_S_STRINGTABLE`에는 원본 전체 경로, `DEBUG_S_FILECHKSMS`에는 해당 경로와 **컴파일한 원본 바이트**의 체크섬, `DEBUG_S_LINES`에는 함수 코드 크기와 파일별 줄 표를 쓴다. 줄 블록의 파일 참조는 체크섬 하위 섹션의 **바이트 오프셋**이다. 같은 이름의 다른 파일, UTF-8 한글·공백 경로, CRLF를 구분한다. 줄은 1부터 세고 실제 실행문 행을 표시한다. 하위 섹션 길이·패딩과 함수 범위를 검증한다.
- 줄 표 기여분의 시작 주소와 함수 심볼의 코드 주소에는 `IMAGE_REL_AMD64_SECREL`(4바이트)과 `IMAGE_REL_AMD64_SECTION`(2바이트)을 쓴다. 현재 `FixupKind::SecRel32`는 있다. `SECTION`에 대응하는 fixup과 **정확히 2바이트**만 쓰는 암시적 addend 처리를 추가한다. 기존 COFF writer가 8바이트 외의 값을 4바이트로 기록하는 곳을 함께 수정한다. 함수별 COMDAT에는 그 코드와 연관된 디버그 기여분을 사용한다.
- `DEBUG_S_SYMBOLS`에 컴파일 단위와 함수의 이름, 범위, 끝 레코드를 쓴다. 예를 들어 `S_COMPILE3`, `S_GPROC32`/`S_LPROC32`와 `S_END`를 사용할 수 있다. 참조한 타입·ID는 실제로 해석 가능해야 한다. `*_ID` 형식을 택하면 `.debug$T`의 함수 ID와 링크 단계의 타입 인덱스 재매핑까지 맞춘다. 기존 `tests/lld_tests/COFF/Inputs/generic.yaml`에는 실제 줄 블록과 relocation 쌍이, `duplicate-cv.s`에는 연관 COMDAT 예제가 있다. [Microsoft CodeView 레코드 정의](https://github.com/microsoft/microsoft-pdb/blob/master/include/cvinfo.h) · [PE/COFF 형식과 재배치](https://learn.microsoft.com/en-us/windows/win32/debug/pe-format)

**여기서 먼저 교차 검증한다.** `llvm-readobj --sections --relocations --codeview probe.obj`로 함수·두 경로·예상 행·relocation을 확인한다. 같은 오브젝트를 독립적인 링커로 연결해 만든 PDB에서 해당 줄이 조회되면 이후 실패를 내장 링커 쪽으로 좁힐 수 있다. 컴파일 드라이버의 기존 `GLOSSO_COFF_LINKER` override로 Developer PowerShell의 실제 `link.exe` 또는 별도로 설치된 `lld-link.exe`를 지정하고 `/DEBUG:FULL`로 비교한다. 비교 빌드마다 별도 출력·캐시 경로를 쓰고, 내장 링커 검증 전 override를 원래대로 돌린다. `glosso lld-link`는 내장 링커의 별칭이다. [LLVM 오브젝트 검사 도구](https://llvm.org/docs/CommandGuide/llvm-readobj.html)

## 5. 내장 PE 링커 — 실제 PDB를 쓰고 EXE와 짝을 맞춘다

1. `src/link/coff.rs`의 오브젝트 파싱과 `src/link/pe.rs`의 `add_object`, `keeps_section`를 확인한다. 입력 파일별 `.debug$S`와 필요한 `.debug$T`를 일반 PE 섹션 배치와 별도로 읽어 둔다. 아카이브에서 실제로 선택한 멤버와 COMDAT의 관계를 유지한다. 디버그 relocation 때문에 원래 제거될 함수가 살아나는 일을 막는다.
2. 섹션 배치·GC·ICF 이후 기존 링크 결과로 `(입력 obj, 섹션, 심볼, addend)`를 최종 **1부터 시작하는 출력 섹션 번호와 섹션 상대 오프셋**으로 해석한다. PE의 기존 `SECTION`/`SECREL` 계산을 재사용한다. C13 행 오프셋은 기여분 시작 기준이다. DBI 섹션 헤더로 계산한 RVA가 EXE의 실행 가능한 코드에 들어가는지 확인한다. 제거된 COMDAT 행은 빼고, 병합된 코드는 실제 대표 주소에만 연결한다.
3. 자체 링커에 MSF 7.0 PDB writer를 연결한다. PDB Info(스트림 1), TPI(2), DBI(3), IPI(4)의 올바른 헤더와 스트림 디렉터리, `/names`, DBI 모듈·소스 파일 목록·섹션 기여분·섹션 맵·최종 PE 섹션 헤더, 모듈별 심볼과 C13 줄/체크섬 데이터를 작성한다. 오브젝트별 문자열·체크섬 오프셋을 PDB의 테이블에 맞춰 갱신하고 프로시저의 끝 오프셋과 스트림 크기를 다시 계산한다. 실제 타입 참조가 없을 때는 유효한 빈 타입 스트림부터 시작할 수 있다. 이름 조회용 public/global 인덱스는 내보낸 심볼과 일치시킨다. MSF 블록 경계를 넘는 파일도 테스트한다. [LLVM PDB와 MSF 형식](https://llvm.org/docs/PDB/index.html)
4. `pe.rs`의 `debug_layout`, `write_image`, `output_name_for_pdb`를 잇는다. `/PDB:path`는 실제 출력 위치, `/PDBALTPATH:path`는 EXE에 새기는 경로로 처리한다. 기본값은 EXE 옆 같은 이름의 `.pdb`다. PE 디버그 디렉터리의 type 2 `RSDS`와 PDB Info의 **GUID와 age가 일치**해야 한다. `/Brepro`의 재현성을 유지하되 PDB 식별자 산출에는 디버그 내용도 반영한다. 소스 앞에 빈 줄 하나만 추가하면 기계어는 같아도 옛 PDB를 로드해서는 안 된다. 기존 `/build-id`, `/debug:dwarf`, `/debug:symtab` 의미를 보존하고 링크 도움말을 갱신한다.
5. 최종 산출물을 임시 파일에 완성하고 일관된 쌍으로 게시한다. PDB 쓰기 실패나 동시 빌드에서 서로 다른 빌드의 EXE/PDB가 성공으로 보고되지 않게 한다. `src/main.rs`의 `artifact_cache_identity`와 `src/artifact_cache.rs`의 `restore`는 **단일 산출물**을 전제로 한다. 디버그 모드·형식·경로 조건을 키에 반영하고 두 파일을 함께 저장/복원하거나, 첫 버전에서는 PDB가 필요한 최종 링크에 한해 해당 캐시를 우회한다. 같은 출력 이름으로 디버그↔일반 빌드를 전환하고 PDB만 지운 뒤 재빌드하는 경우도 확인한다.

## 6. wst의 실제 빌드 경로에 연결한다

`first.glo`의 `usage`와 빌드 인자 처리에 `build --debug`를 추가한다. `compile`이 만드는 **wst 자식 프로필**에 `options.optimization_level = .O0`와 `options.add_compiler_arg("-g")`를 함께 전달한다. 기존 GUI/콘솔 선택, `tools/gui_std.glo`의 SDK 준비, 아이콘·네이티브 의존성 링크는 같은 빌드 경로를 사용한다. 디버그 산출물은 예를 들어 `build/debug/wst.exe`와 `build/debug/wst.pdb`에 두고 생성한 경로를 출력한다. 기존 기본 빌드는 `build/wst.exe`의 `-O3` 릴리스 경로를 계속 사용한다. `std/Compiler.glo`에는 이미 `add_compiler_arg`가 있어 첫 구현에 프로필 ABI 변경이 필요하지 않다.

wst 루트에서 구현 후 실행할 예시다.

```powershell
$glo = (Resolve-Path '..\glosso-qbe\build\bin\glosso.exe').Path
& $glo compile first.glo --cache-dir=build/cache -- build --debug
```

Visual Studio의 **파일 → 열기 → 프로젝트/솔루션**에서 `build\debug\wst.exe`를 연다. **파일 → 열기 → 파일**로 현재 체크아웃의 `src\main.glo`를 열고 `main` 안의 `args := get_cmdline_args();`에 F9를 설정한 뒤 F5를 누른다. 해당 소스가 표시된 채 해당 줄에서 멈춰야 한다. **디버그 → 창 → 모듈**에서 `wst.exe`에 실제 로드된 PDB 경로를 확인한다. 구 SDK는 빌드 중 패치한 표준 라이브러리를 임시 디렉터리에 둘 수 있으므로 이 인수 시험은 프로젝트 원본 `src\main.glo`에서 수행한다. [Microsoft 외부 EXE 디버깅](https://learn.microsoft.com/ko-kr/visualstudio/debugger/how-to-debug-an-executable-not-part-of-a-visual-studio-solution?view=visualstudio) · [Visual Studio 심볼 로드](https://learn.microsoft.com/ko-kr/visualstudio/debugger/specify-symbol-dot-pdb-and-source-files-in-the-visual-studio-debugger?view=visualstudio)

## 7. 검증과 인수 기준

| 검증 | 통과 조건 |
|---|---|
| 오브젝트 | `llvm-readobj --sections --relocations --codeview probe.obj`에서 두 원본 파일의 체크섬, 예상 실행문 행, 함수별 범위와 relocation 쌍이 보인다. 정상 분기와 긴 분기, 정렬이 들어간 함수에서도 행 주소가 명령 경계에 있다. |
| EXE/PDB | `llvm-readobj --coff-debug-directory probe.exe`와 `llvm-pdbutil dump -summary -modules -files -l -symbols -section-map -section-contribs probe.pdb`의 결과를 비교한다. EXE와 PDB의 GUID/age가 일치하고 두 파일의 행이 유효한 코드 범위에 있다. LLVM 도구가 없는 Windows 환경에서는 `dumpbin /headers`와 Visual Studio DIA SDK로 확인할 수 있다. [llvm-pdbutil 옵션](https://llvm.org/docs/CommandGuide/llvm-pdbutil.html) |
| Windows 자동 검증 | DIA SDK에서 EXE의 RSDS를 읽어 `IDiaDataSource::loadAndValidateDataFromPdb`에 GUID·age를 전달한다. `openSession` 후 `findFile`로 두 원본을 찾고 각 compiland에서 `findLinesByLinenum`을 호출한다. 이 API는 가까운 줄도 반환하므로 결과의 **실제 행 번호**와 파일이 요청값과 같은지 검사한다. `findLinesByRVA`로 역조회해 같은 파일/줄을 확인한다. [Microsoft DIA 줄 조회](https://learn.microsoft.com/en-us/visualstudio/debugger/debug-interface-access/idiasession-findlinesbylinenum?view=visualstudio) |
| IDE 인수 | Visual Studio의 디버거 유형은 **네이티브**를 사용한다. 예제에서 F9/F5로 `main.glo` 8행에 멈추고 F11로 `helper.glo`에 진입한다. `helper.glo` 5행에서 F10으로 `return`까지 진행하고 호출 스택에서 원래 호출 줄로 이동한다. 별도 실행에서는 `helper.glo` 5행에 직접 건 중단점과 wst의 중단점이 각각 적중해야 한다. |
| 회귀 | 같은 이름의 다른 경로 파일, 공백·한글·CRLF, 여러 오브젝트/함수, 제거된 함수, 디버그 정보 없는 네이티브 라이브러리 혼합, `.glo`에 빈 줄만 추가한 재빌드를 검사한다. 이전 PDB를 새 EXE와 조합하면 DIA 검증이 실패해야 한다. 콜드·웜 캐시와 PDB 유실 복구, 디버그 옵션 없는 기존 빌드·테스트·릴리스도 점검한다. |

기존 테스트 위치는 `tests/unit/driver.rs`, `tests/unit/asm.rs`, `tests/unit/backend/gloqbe.rs`, `tests/unit/link.rs`, `tests/native_artifact_cache.py`다. `tests/lld_tests/COFF/pdb.test`와 `rsds.test`도 참고한다. 프로젝트 기본 테스트는 컴파일러 루트의 `.\build.exe test`다. 완료 보고에 사용한 컴파일러·링커 경로, 재현 명령, 두 파일의 `파일·줄 → RVA` 예시, VS에서 실제로 적중한 줄과 로드된 PDB 경로를 남긴다. IDE를 실행할 수 없는 환경에서는 자동 검증 결과와 남은 수동 검증을 구분해 기록한다.
