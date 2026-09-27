# Platform API — boot ordering constraints

The verified `pc_main.c` → `boot.c` → `jsyswrap.cpp` init sequence and the six
hard ordering rules `dc/` must obey. Read before writing or reordering any
`dc/` init code. Split out of `kb/design-platform-api.md` §1. Legend and dispositions: `kb/platform-api-overview.md`. Index: `kb/design-platform-api.md`.

## 1. Ordering constraints (from `pc_main.c` → `boot.c` → `jsyswrap.cpp`)

Verified boot order. Anything that reads a value must come after whatever
writes it; the ✱ items are the hard constraints.

```
main()                                       [pc_main.c]
 1. setvbuf(stdout), parse argv
 2. ✱ compute image base/end (pc_image_base/_end)   ← seg2k0 pointer heuristic
 3. pc_settings_load()                              ← must precede window creation
 4. pc_keybindings_load()
 5. pc_platform_init()
      SDL_Init → window → GL context
      pc_overlay_init(); boot splash
      ✱ pc_gx_init()        ← must precede any GX call
      pc_texture_pack_init()
 6. pc_check_disc_or_die()  → pc_disc_init()   ✱ must precede pc_assets_init
 7. ✱ pc_assets_init()      ← fills every .inc asset the linked game references,
                              BEFORE any game code runs
 8. ac_entry()              (src/main.c main(); sets HotStartEntry = &entry)
 9. boot_main(argc, argv)   (src/static/boot.c main())
      ReconfigBATs()
      InitialStartTime = osGetTime()      ✱ time source live before OSInit()
      OSInit()                            ✱ allocates the arena, writes *(arena+0x28)
      OSInitAlarm()
      ✱ OSGetStackPointer() / OSGetCurrentThread()->stackBase/stackEnd
        are DEREFERENCED here to paint the stack with 0xFD
      bzero(osAppNMIBuffer, 64B); OSGetResetCode()
      __osInitialize_common(); OSGetConsoleType()
      DVDGetCurrentDiskID()               ✱ gameVersion drives zurumode flags
      adjustOSArena()   → OSGetArenaLo/Hi, OSSetArenaHi, bzero(whole arena)
      JW_Init()                           [jsyswrap.cpp]
        SystemHeapSize = arenaHi - arenaLo - 0xD0
        JFWSystem::setAramAudioBufSize(0x810000)     ← 8.44 MB
        JFWSystem::setAramGraphBufSize(0x6A3780)     ← 6.96 MB
        JFWSystem::setFifoBufSize(0x10001)
        JFWSystem::init()                 [JFWSystem.cpp]
          firstInit(): OSInit(); DVDInit();
                       ✱ JKRExpHeap::createRoot → JKRHeap::initArena
                         → OSInitAlloc, OSPhysicalToCached(0),
                           mMemorySize = *(u32*)(start + 0x28)      ← §3.1
          ✱ JKRAram::create → ARInit(), ARQInit(), ARGetSize(), ARAlloc ×2/3
          JKRThread(OSGetCurrentThread(), 4)
          ✱ JUTVideo::createManager → VIInit, VISetBlack, VIFlush,
             VIGetRetraceCount, OSGetTick, VISetPre/PostRetraceCallback,
             OSInitMessageQueue, GXSetDrawDoneCallback
          ✱ JUTCreateFifo(0x10001) → GXInit(base, size)   ← first GX call
          ✱ JUTGamePad::init() → PADInit()
          JUTDirectPrint / JUTException / fonts / consoles
        JFWDisplay::createManager(&GXNtsc480IntDf, …)
      fault_Init() + 6 fault clients (incl. DisplayArena)
      sound_initial()   → Na_InitAudio(…) → AIInit / AIInitDMA / DSPInit
                        → msleep(2500)                ✱ 2.5 s dead wait
      initial_menu_init(); dvderr_init()
      sound_initial2()  → loop { VIWaitForRetrace(); Na_GameFrame(); }
                          until Na_CheckNeosBoot()    ✱ first frame loop
      JKRDvdToMainRam("/COPYDATE"); LoadStringTable("/static.str")
      … JW_Init2() (forest_1st.arc → ARAM), MallocInit(gameheap),
        JW_Init3() (forest_2nd.arc → ARAM) … → HotStartEntry → game loop
10. pc_disc_shutdown(); pc_platform_shutdown()
```

Hard ordering rules for `dc/`:

1. **Asset table before game code.** `pc_assets_init()` must complete before
   `ac_entry()`; the linked-in `.inc` arrays are empty until it runs.
2. **Arena before JKRHeap.** `OSInit()` must have written the memory-size word
   at `OSPhysicalToCached(0) + 0x28` before `JKRExpHeap::createRoot` runs, or
   every heap is created with a garbage `mMemorySize`.
3. **ARAM before archives.** `ARInit`/`ARAlloc` run inside `JFWSystem::init()`,
   long before `JW_Init2()` mounts `forest_1st.arc` into ARAM.
4. **`GXInit` before any other GX call**, and it happens *inside*
   `JFWSystem::init()` — i.e. after the heap exists, so the PVR/GLdc
   initialisation cannot be done lazily on first draw without care.
5. **VI callbacks are registered before the first frame** and
   `VIWaitForRetrace()` is first called from `sound_initial2()`, i.e. before
   the game loop — the frame pacing path must be live at that point.
6. **Time source before `OSInit()`** — `InitialStartTime = osGetTime()` is the
   very first line of `boot_main`, one statement ahead of `OSInit()`.

---
