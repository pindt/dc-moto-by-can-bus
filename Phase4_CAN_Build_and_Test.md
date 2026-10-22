# Phase 4 build and CAN-only test instructions

## Paths and preserved configuration

Project directory:
`C:\Users\Admin\workspaceS32DS.3.4\dc-moto-by-can-bus`.
The existing `.project` names the Eclipse/S32DS project **dc_moto_tt**.
The current Debug_FLASH configuration still builds the original Phase 3 main.
It does not automatically discover the new application/protocol/example folders.
Do not use its ordinary Build button and assume it selected Phase 4.

The supported Phase 4 build entry is `tools/build_can_only.ps1`. It uses the
installed S32DS GCC/RTD and existing startup/linker script, explicitly selects the
separate main and excludes SPI driver/LPSPI/SPI configuration sources. It does
not change `.cproject`, generated recipes, RTD, pin mappings or regression code.

## Build and host tests (verified)

In PowerShell:

```powershell
Set-Location -LiteralPath 'C:\Users\Admin\workspaceS32DS.3.4\dc-moto-by-can-bus'
.\tests\phase4\run.ps1
.\tests\phase4\verify_preserved.ps1
.\tools\build_can_only.ps1
```

Expected: `PASS: 14 simplified Phase 4 test groups`, preservation of 138 protected
files and identical Phase 3 fixture, then a successful CAN-only link/SPI-symbol
check. Outputs: `tests/phase4/build/test_phase4.exe`, `build_phase4/can_only.elf`
and `build_phase4/can_only.map`. The scripts do not flash a board.

Default installed tools used in the verified build:

- Host GCC: `C:\msys64\ucrt64\bin\gcc.exe`.
- ARM toolchain: `C:\NXP\S32DS.3.4\S32DS\build_tools\gcc_v6.3\gcc-6.3-arm32-eabi`.
- RTD: `C:\NXP\S32DS.3.4\S32DS\software\PlatformSDK_S32K1_2022_02\SW32K1_RTD_4_4_1_0_1_D2202`.

For another installation, pass `-Compiler` to the host runner and `-Toolchain`
(toolchain root) / `-Rtd` (RTD root) to the ARM script. The script checks every
compiler/linker exit code. Inspect `g_CanOnlyInitStage` on hardware; success is 6,
`g_CanOnlyReady=true` and CAN init/start statuses OK.

## Run that build from S32DS without changing .cproject

The command-line build above was performed. These IDE launch steps are provided
for the user; no IDE configuration or physical debug session was changed here.

1. Open the existing project in S32DS 3.4. Select **dc_moto_tt** in Project Explorer.
2. Choose **Run > External Tools > External Tools Configurations... > Program**,
   create a launch named `Phase4 CAN-only build`.
3. Use:
   - Location: `C:\Windows\System32\WindowsPowerShell\v1.0\powershell.exe`
   - Working Directory: `${workspace_loc:/dc_moto_tt}`
   - Arguments:
     ```text
     -NoProfile -ExecutionPolicy Bypass -File "${workspace_loc:/dc_moto_tt}/tools/build_can_only.ps1"
     ```
4. Run this external tool, check its console success, then refresh the project
   (F5). If the imported project name differs, use its actual project name in
   both workspace variables. Do not rename/change the preserved project files.
5. When ready for a **motor-disconnected** bench, duplicate the existing working
   S32 Debugger/PEmicro debug launch in **Run > Debug Configurations...**, naming
   the copy `Phase4 CAN-only`. Keep its proven board/probe/device settings.
6. In that copy, set Project to `dc_moto_tt` and C/C++ Application to
   `build_phase4/can_only.elf`. The preserved regression launch instead points to
   `Debug_FLASH/dc_moto_tt.elf`; explicitly replace that path in the copy.
7. Disable **Build before launch** for the copy (run the external tool first).
   Save the copy as a local workspace launch, leaving the existing shared
   `.launch` file unchanged. Check the selected ELF again before any flash.

This external-tool path is the exact supported S32DS build selection. No new
managed build configuration or source-root edits are required. Keep the original
Debug_FLASH/Phase 3 launch for regression and never compile two main functions.

## CAN-only bench (prepared, hardware execution pending)

Use only the CAN-only ELF; the Phase 3 main actively sends SPI demo packets.
Keep motor power/enable disconnected. Use the existing S32 CAN transceiver,
correct enable/standby wiring, two properly terminated bus ends and an
acknowledging CAN peer. The CAN-only main initializes clocks, only the existing
PTE4/PTE5 CAN pin entries, interrupt framework and CAN0; it does not initialize
SPI pads or SpiDrv. Failed initialization returns to the foreground fault service
rather than waiting in an error loop.

Settings: Classic CAN, standard 11-bit IDs, 500 kbit/s, 48 MHz CAN peripheral
clock, command **0x100**, response **0x180**, DLC **8**. Reset S32 before a new
sequence session. Send one request at a time and wait for its response; GET_STATUS
has both ACK and STATUS. Retry the same sequence/bytes if no response arrives.

| Action | 0x100 command bytes | Expected 0x180 reply bytes |
| --- | --- | --- |
| GET_STATUS | 00 05 00 00 00 00 00 00 | ACK: 01 00 00 05 00 00 00 01; STATUS: 02 00 00 00 00 00 00 01 |
| SET_MAX_DUTY 500 (software only) | 01 04 F4 01 00 00 00 00 | 01 01 00 04 F4 01 00 00 01 |
| START_FORWARD | 02 01 00 00 00 00 00 00 | 01 02 83 01 00 00 00 01 (NOT_CONFIGURED) |
| SET_DUTY 100 | 03 03 64 00 00 00 00 00 | 01 03 83 03 64 00 00 01 (NOT_CONFIGURED) |
| STOP | 04 00 00 00 00 00 00 00 | 01 04 01 00 00 00 00 01 (PENDING) |
| GET_STATUS after STOP | 05 05 00 00 00 00 00 00 | ACK: 01 05 00 05 00 00 00 01; STATUS: 02 00 00 20 00 00 00 01 |

No response should claim RUNNING, CONFIGURED, SAFE_OFF or COMPLETED without a
real backend. STOP latches immediately but remains physically unconfirmed.
The main passes Service(0,false) with timeout zero, so there is no fabricated
millisecond lease and physical START/duty remains disabled.

The existing `tests/esp32s3_bench/README.md` describes the separate ESP-IDF 5.4.4
bench, required board GPIO selection and vectors for malformed/duplicate/stale
commands. Its ESP32 build/flash/hardware run remains pending. Host tests exercise
BUSY, STOP/fault priority, matching results, overrun and bus-off through doubles;
actual bus faults and physical motor operation are not claimed as tested.
