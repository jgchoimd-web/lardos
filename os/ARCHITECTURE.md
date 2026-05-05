# LardOS 운영체제 전체 구조도

## 0. 디렉터리/파일 구조

```
lardos/
├── os/
│   ├── boot/              boot.s (512B BIOS 부트섹터)
│   ├── kernel/            커널 C/ASM (~60개)
│   ├── include/           공용 헤더
│   ├── scripts/           mkardx, mkimg, mkldll, mklfs, bin2inc 등 호스트 도구
│   ├── lang/              bosla, seed, examples, lib/*.boslib
│   ├── tools/             lil (호스트 LIL)
│   ├── third_party/       mbedtls (TLS), bearhttps (미사용)
│   ├── Makefile           deps.mk, linker.ld
│   └── ARCHITECTURE.md    README.md
└── build/
```

---

## 1. 계층별 구조 개요

```mermaid
flowchart TB
    subgraph Boot [부팅 계층]
        BIOS[BIOS]
        BootSector[boot.s 512B]
        LongMode[Long Mode 전환]
        LoadKernel[kernel.bosx 로드]
        BIOS --> BootSector --> LongMode --> LoadKernel
    end

    subgraph Kernel [커널 핵심]
        Entry[entry64.s]
        Kmain[kernel64.c kmain]
        Entry --> Kmain
    end

    subgraph Core [시스템 기반]
        GDT[gdt64 GDT]
        IDT[idt64 IDT]
        MMU[mmu 페이징]
        Mem[mem kmalloc]
        Syscall[syscall INT 0x80]
        SMP[smp MP테이블/AP 웨이크]
    end

    subgraph Drivers [드라이버]
        PCI[pci]
        RTL[rtl8139 NIC]
        PS2[ps2 키보드/마우스]
        RTC[rtc]
        DRFL[drfl 드라이버 로더]
    end

    subgraph Storage [저장소]
        FS[fs 파일시스템]
        LFS[lfs 볼륨]
        LDLL[ldll 동적라이브러리]
    end

    subgraph Network [네트워크]
        Net[net DHCP/DNS/HTTP/HTTPS]
        MbedTLS[mbedTLS TLS]
    end

    subgraph UserFace [사용자 인터페이스]
        GUI[gui 탭/프레임버퍼]
        LSH[lsh 셸]
        LSS[lss Shrine 호환]
    end

    subgraph VMs [VM/언어]
        BOSL[bosl_vm]
        GASM[gasm_vm]
        LIL[lil]
        LML[lml]
        OSVM[os_vm]
        LafilloVM[lafillo_vm]
    end

    subgraph UserMode [유저 모드]
        LardxLoad[lardx_load]
        Usermode[usermode]
    end

    LoadKernel --> Entry
    Kmain --> Core
    Core --> Drivers
    Core --> Storage
    Core --> Network
    Core --> UserFace
    Core --> UserMode
    LSH --> VMs
    GUI --> LSH
    GUI --> LafilloVM
```

---

## 1.5. 빌드 파이프라인

```mermaid
flowchart LR
    subgraph Sources [소스]
        BootS[boot.s]
        KernelC[kernel/*.c]
        KernelS[kernel/*.s]
        MbedTLS[third_party/mbedtls/*.c]
    end

    subgraph Build [빌드]
        NASM[NASM]
        GCC[gcc]
        LD[ld]
    end

    subgraph Outputs [산출물]
        BootBin[boot.bin]
        KernelElf[kernel.elf]
        KernelBosx[kernel.bosx]
        OsImage[os-image.bin]
    end

    subgraph Tools [호스트 도구]
        MKARDX[mkardx]
        MKIMG[mkimg]
        BIN2INC[bin2inc]
        MKLDLL[mkldll]
    end

    BootS --> NASM --> BootBin
    KernelC --> GCC
    KernelS --> NASM
    MbedTLS --> GCC
    GCC --> KernelElf
    NASM --> KernelElf
    LD --> KernelElf
    KernelElf --> MKARDX --> KernelBosx
    BootBin --> MKIMG
    KernelBosx --> MKIMG
    MKIMG --> OsImage
```

---

## 2. 부팅 → 커널 초기화 흐름

```mermaid
sequenceDiagram
    participant BIOS
    participant boot
    participant kernel
    participant kmain

    BIOS->>boot: LBA 0 로드
    boot->>boot: 16bit Real
    boot->>boot: 32bit Protected
    boot->>boot: Long Mode
    boot->>boot: LARD/BOSX 커널 로드
    boot->>kernel: jmp entry

    kernel->>kmain: call kmain
    kmain->>kmain: gdt64_init, smp_init
    kmain->>kmain: idt64_init, syscall_init, mmu_init
    kmain->>kmain: usermode_init
    kmain->>kmain: gui_demo (데모 출력)
    kmain->>kmain: mem_init, fs_init, lsh_init
    kmain->>kmain: ps2_init, net_init
    kmain->>kmain: 메인 루프 (마우스/키보드, Lafillo URL)
```

---

## 3. 파일시스템 구조

```mermaid
flowchart LR
    subgraph FS_Layer [fs.c]
        FSOpen[fs_open]
        FSRead[fs_read]
        FSList[fs_list]
        FSWritable[fs_open_writable]
    end

    subgraph Data [데이터 소스]
        Builtin[내장 파일 hello.txt readme.txt sample.bmp]
        LFS_Vol[LFS 볼륨 lfs_info.txt]
        RAM[RAM 파일 notes.txt lafillo_saved.txt]
        LDLL_FS[liblard libgui liblafillo.ldll]
        LardxFS[lafillo_demo.bosx]
    end

    FSOpen --> Builtin
    FSOpen --> LFS_Vol
    FSOpen --> RAM
    FSOpen --> LDLL_FS
    FSOpen --> LardxFS
```

---

## 4. Syscall, 샌드박스 및 유저 모드

- **샌드박스**: `sandbox` 입력 시 `run` 실행 시 제한된 syscall만 허용 (파일/LDLL/네트워크/GUI 쓰기/키 입력 차단). `exitsandbox`로 해제.

```mermaid
flowchart TB
    subgraph User [유저 프로그램]
        Lardx[LARDX .bosx]
        UserCode[사용자 코드]
    end

    subgraph Trampoline [진입]
        Int80[INT 0x80]
        SyscallHandler[syscall_handler]
    end

    subgraph Syscalls [Syscall 핸들러]
        SYS_WRITE[write]
        SYS_EXIT[exit]
        SYS_OPEN[sys_open]
        SYS_READ[sys_read]
        SYS_CLOSE[sys_close]
        SYS_LDLL[ldll_load/sym/close]
        SYS_GUI[gui_put_pixel 등]
        SYS_LAFILLO[SYS_LAFILLO_HTML]
        SYS_KEY[poll_key/get_key]
        SYS_TIME[get_time]
    end

    subgraph KernelServices [커널 서비스]
        FS[fs]
        LDLL[ldll]
        GUI[gui]
        RTC[rtc]
        PS2[ps2]
        Lafillo[lafillo_html]
    end

    User --> Int80
    Int80 --> SyscallHandler
    SyscallHandler --> SYS_WRITE
    SyscallHandler --> SYS_OPEN
    SyscallHandler --> SYS_LDLL
    SyscallHandler --> SYS_GUI
    SyscallHandler --> SYS_LAFILLO

    SYS_OPEN --> FS
    SYS_LDLL --> LDLL
    SYS_GUI --> GUI
    SYS_LAFILLO --> Lafillo
```

---

## 5. VM/언어 레이어 상세

| VM       | 바이트코드   | 용도             | LSH 명령 | 주요 opcode                       |
| -------- | ----------- | ---------------- | ------- | --------------------------------- |
| BOSL     | BOSL        | 범용 스크립트, MMIO | `bosl`  | pushi/add/print/call/ret/peek/poke |
| GASM     | 9B/insn     | accumulator, OOP | (인라인) | load/add/print/new/invoke         |
| LIL      | S-expression| REPL, 수식       | (호스트) | (+ 1 2), (print x)                |
| LML      | 마크업       | 설정/UI          | (커널)  | config.lml                        |
| OS VM    | OVM         | 운영체제용 단순   | `osvm`  | push/add/sub/mul/div/print/halt   |
| Lafillo VM | DVM         | HTML→텍스트      | `lafvm`   | push/lafillo/print/halt             |
| LARSH    | 텍스트+LMD   | 에니메이션/UI/간단프로그램 | `larsh` | obj/key/rect/circle/text/line/lmd |

---

## 6. GUI 탭 구조

```mermaid
flowchart LR
    subgraph GUI_Tabs [gui.c 탭]
        Tab0[LSH]
        Tab1[Calc]
        Tab2[Gallery]
        Tab3[User]
        Tab4[Lafillo]
        Tab9[Lafaelo]
    end

    Tab0 --> LSH
    Tab1 --> CalcLogic[사칙연산]
    Tab2 --> GalleryView[이미지/파일 목록]
    Tab3 --> UserOutput[ldll 출력]
    Tab4 --> DilloBrowser[URL bar + HTTP/file + HTML→텍스트]
    Tab9 --> LafaeloEditor[Code editor: Open/Save/Run]
```

---

## 7. 주요 파일 경로 요약

| 영역       | 경로 |
| ---------- | --- |
| 부팅       | `os/boot/boot.s` |
| 커널 진입  | `os/kernel/entry64.s`, `os/kernel/kernel64.c` |
| 메모리     | `os/kernel/mem.c`, `os/kernel/mmu.c` |
| SMP        | `os/kernel/smp.c`, `os/include/smp.h`, `os/kernel/ap_trampoline.s`, `os/kernel/aux_kernel.s` |
| 파일시스템 | `os/kernel/fs.c`, `os/kernel/lfs.c` |
| 네트워크   | `os/kernel/net.c`, `os/kernel/rtl8139.c` |
| Syscall    | `os/kernel/syscall.c`, `os/include/syscall.h` |
| 유저 실행체| `os/kernel/lardx_load.c`, `os/kernel/ldll.c` |
| GUI        | `os/kernel/gui.c` |
| 셸         | `os/kernel/lsh.c` |
| Lafillo    | `os/kernel/lafillo.c`, `os/include/lafillo.h` |
| VM들       | `os/kernel/bosl_vm.c`, `os/kernel/os_vm.c`, `os/kernel/lafillo_vm.c`, `os/kernel/larsh.c` |
| 스크립트/빌드 | `os/scripts/mkardx.c`, `os/scripts/mkldll.c`, `os/scripts/mklfs.c` |

---

## 8. 데이터 흐름 (Lafillo 예시)

```mermaid
flowchart LR
    UserInput[URL/파일 입력]
    NetFetch[net_http_get / net_https_get]
    FsOpen[fs_open file://]
    LafilloHTML[lafillo_http_to_text]
    GuiSet[gui_lafillo_set_content]

    UserInput --> NetFetch
    UserInput --> FsOpen
    NetFetch --> LafilloHTML
    FsOpen --> LafilloHTML
    LafilloHTML --> GuiSet
```

---

## 8.5. SMP/멀티코어 구조

코어가 3개 이상일 때 코어 1에서 보조 모놀리식 커널(aux)을 구동.

```mermaid
flowchart TB
    subgraph BSP [BSP 코어 0]
        Kmain[kmain]
        SmpInit[smp_init]
        MainLoop[메인 루프]
        Kmain --> SmpInit
        SmpInit --> MainLoop
    end

    subgraph SMP [코어 3개 이상 시]
        MpTable[MP 테이블 파싱]
        CopyTramp[트램펄린 0x4000 복사]
        CopyAux[보조커널 0x200000 복사]
        InitSIPI[INIT-SIPI-SIPI]
        SmpInit --> MpTable --> CopyTramp --> CopyAux --> InitSIPI
    end

    subgraph AP1 [AP 코어 1]
        Trampoline[ap_trampoline 0x4000]
        AuxKernel[aux_kernel 0x200000]
        Trampoline -->|"APIC ID==1"| AuxKernel
    end

    subgraph AP2 [AP 코어 2+]
        HLT[HLT 루프]
    end

    InitSIPI --> Trampoline
    InitSIPI --> HLT
```

---

## 9. 통합 전체 구조도

```mermaid
flowchart TB
    subgraph Build [빌드]
        BootBin[boot.bin]
        KernelBosx[kernel.bosx]
        OsImage[os-image.bin]
    end

    subgraph Boot [부팅]
        BIOS[BIOS]
        BootSector[boot.s]
        LongMode[Long Mode]
        Entry[entry64]
        Kmain[kmain]
    end

    subgraph Core [핵심]
        GDT[gdt64]
        IDT[idt64]
        MMU[mmu]
        Mem[mem]
        Syscall[syscall]
        SMP[smp]
    end

    subgraph Drivers [드라이버]
        PCI[pci]
        RTL[rtl8139]
        PS2[ps2]
    end

    subgraph Storage [저장소]
        FS[fs]
        LFS[lfs]
        LDLL[ldll]
    end

    subgraph Net [네트워크]
        NetC[net]
        TLS[mbedTLS]
    end

    subgraph UI [사용자]
        GUI[gui]
        LSH[lsh]
    end

    subgraph VMs [VM]
        BOSL[bosl]
        GASM[gasm]
        LafilloVM[lafillo]
    end

    OsImage --> BIOS
    BIOS --> BootSector --> LongMode --> Entry --> Kmain
    Kmain --> Core
    Core --> SMP
    Core --> Drivers
    Core --> Storage
    Core --> Net
    Core --> UI
    LSH --> VMs
```
