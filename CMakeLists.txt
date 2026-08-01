cmake_minimum_required(VERSION 3.13)
execute_process(COMMAND uname OUTPUT_VARIABLE uname) # for detecting msys
option(USE_ICONV "Use iconv for converting Shift-JIS to UTF-8." ON)
option(RUN68_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer." OFF)

#add_definitions(-DFNC_TRACE -DENV_FROM_INI)
add_definitions(-DENV_FROM_INI)

if(USE_ICONV)
  if (NOT(uname MATCHES "^MSYS" OR uname MATCHES "^MINGW"))
    add_definitions(-DUSE_ICONV)
  endif()
endif()


project(run68 C CXX)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (EMSCRIPTEN)
set(CMAKE_EXE_LINKER_FLAGS "--embed-file ./fs@ -sFORCE_FILESYSTEM")
endif()


if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
  add_compile_options(
    -fsigned-char
    -Wall
    -Wextra
    -Wno-unused-parameter
    -Wno-unused-variable
    -Wno-unused-but-set-variable
    -Wno-char-subscripts
    -Wno-pointer-sign
    -Wno-switch
    -Wno-invalid-source-encoding
  )
endif()

if (uname MATCHES "^MSYS" OR uname MATCHES "^MINGW")
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} --exec-charset=cp932") # To support utf-8 (cp932 is not a mistake. it works fine.)
endif()

set(SRC 
    src/ansicolor-w32.c
    src/calc.c
    src/conditions.c
    src/cpu_backend.c
    src/debugger.c
    src/datetime.c
    src/disassemble.c
    src/eaaccess.c
    src/exceptions.c
    src/exec.c
    src/getini.c
    src/hostconsole.c
    src/key.c
    src/line0.c
    src/line2.c
    src/line4.c
    src/line5.c
    src/line6.c
    src/line7.c
    src/line8.c
    src/line9.c
    src/lineb.c
    src/linec.c
    src/lined.c
    src/linee.c
    src/linef.c
    src/load.c
    src/mem.c
    src/msm6258.c
    src/pcm8.c
    src/run68.c
    src/x68k_audio.c
    src/x68k_bus.c
    src/iocscall.c
    src/doscall.c
    src/run68.h
)

if(UNIX OR EMSCRIPTEN)
  list(APPEND SRC src/filesearch.c)
endif()

if(APPLE)
  list(APPEND SRC src/x68k_audio_live_apple.c)
endif()

add_executable(run68 ${SRC})

add_library(run68_ymfm STATIC
  src/ym2151_ymfm.cpp
  third_party/ymfm/ymfm_opm.cpp
)
target_include_directories(run68_ymfm PUBLIC
  src
  third_party/ymfm
)

add_library(run68_musashi STATIC
  third_party/musashi/m68kcpu.c
  third_party/musashi/m68kops.c
  third_party/musashi/softfloat/softfloat.c
)
target_include_directories(run68_musashi PUBLIC
  src
  third_party/musashi
  third_party/musashi/softfloat
)
target_compile_definitions(run68_musashi PUBLIC
  MUSASHI_CNF="musashi_config.h"
)

set(LIBS m) # math lib

if(APPLE)
  find_library(AUDIOTOOLBOX_FRAMEWORK AudioToolbox REQUIRED)
  find_library(AUDIOUNIT_FRAMEWORK AudioUnit REQUIRED)
  find_library(COREAUDIO_FRAMEWORK CoreAudio REQUIRED)
  find_library(COREFOUNDATION_FRAMEWORK CoreFoundation REQUIRED)
  list(APPEND LIBS ${AUDIOTOOLBOX_FRAMEWORK} ${AUDIOUNIT_FRAMEWORK}
                   ${COREAUDIO_FRAMEWORK} ${COREFOUNDATION_FRAMEWORK})
endif()

if(USE_ICONV)
  if(APPLE)
    set(LIBS ${LIBS} "iconv")
  endif()
endif()

target_link_libraries(run68 PUBLIC run68_musashi run68_ymfm ${LIBS})

set(RUN68_HAS "${CMAKE_CURRENT_SOURCE_DIR}/tests/HAS.X")
set(RUN68_HLK "${CMAKE_CURRENT_SOURCE_DIR}/tests/hlk.r")
if(EXISTS "${RUN68_HAS}" AND EXISTS "${RUN68_HLK}")
  file(RELATIVE_PATH RUN68_BUILD_REL
    "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}")
  add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/MXPLAY.X"
    BYPRODUCTS "${CMAKE_CURRENT_BINARY_DIR}/mxplay.o"
    COMMAND $<TARGET_FILE:run68> --cpu=musashi tests/HAS.X
      -o "${RUN68_BUILD_REL}/mxplay.o" guest/mxplay.s
    COMMAND $<TARGET_FILE:run68> --cpu=musashi tests/hlk.r
      -o "${RUN68_BUILD_REL}/MXPLAY.X"
      "${RUN68_BUILD_REL}/mxplay.o"
    DEPENDS run68 "${RUN68_HAS}" "${RUN68_HLK}"
      "${CMAKE_CURRENT_SOURCE_DIR}/guest/mxplay.s"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    VERBATIM
  )
  add_custom_target(mxplay DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/MXPLAY.X")
endif()

include(CTest)

if(BUILD_TESTING)
  add_executable(run68_test_musashi_backend
    tests/test_musashi_backend.c
    src/cpu_backend.c
  )
  target_include_directories(run68_test_musashi_backend PRIVATE src)
  target_link_libraries(run68_test_musashi_backend PRIVATE run68_musashi m)
  add_test(NAME musashi_backend COMMAND run68_test_musashi_backend)

  add_executable(run68_test_mem
    tests/test_mem.c
    src/mem.c
    src/exceptions.c
    src/x68k_bus.c
  )
  target_include_directories(run68_test_mem PRIVATE src)
  add_test(NAME memory_access COMMAND run68_test_mem)

  add_executable(run68_test_ym2151
    tests/test_ym2151_ymfm.cpp
    src/x68k_bus.c
  )
  target_include_directories(run68_test_ym2151 PRIVATE src)
  target_link_libraries(run68_test_ym2151 PRIVATE run68_ymfm)
  add_test(NAME ym2151_ymfm COMMAND run68_test_ym2151)

  add_executable(run68_test_audio_wav
    tests/test_x68k_audio.c
    src/msm6258.c
    src/x68k_audio.c
    src/x68k_bus.c
  )
  if(APPLE)
    target_sources(run68_test_audio_wav PRIVATE src/x68k_audio_live_apple.c)
  endif()
  target_include_directories(run68_test_audio_wav PRIVATE src)
  target_link_libraries(run68_test_audio_wav PRIVATE run68_ymfm ${LIBS})
  add_test(NAME audio_wav COMMAND run68_test_audio_wav
    ${CMAKE_CURRENT_BINARY_DIR}/run68-test-opm.wav)

  add_executable(run68_test_msm6258
    tests/test_msm6258.c
    src/msm6258.c
  )
  target_include_directories(run68_test_msm6258 PRIVATE src)
  add_test(NAME msm6258_adpcm COMMAND run68_test_msm6258)

  add_executable(run68_test_pcm8
    tests/test_pcm8.c
    src/pcm8.c
  )
  target_include_directories(run68_test_pcm8 PRIVATE src)
  add_test(NAME pcm8_trap_hle COMMAND run68_test_pcm8)

  add_executable(run68_test_line8
    tests/test_line8.c
    src/line8.c
  )
  target_include_directories(run68_test_line8 PRIVATE src)
  add_test(NAME line8_division COMMAND run68_test_line8)

  add_executable(run68_test_compat
    tests/test_compat.c
    src/datetime.c
  )
  target_include_directories(run68_test_compat PRIVATE src)
  add_test(NAME host_compatibility COMMAND run68_test_compat)

  add_executable(run68_test_load
    tests/test_load.c
    src/load.c
  )
  target_include_directories(run68_test_load PRIVATE src)
  add_test(NAME executable_loader COMMAND run68_test_load)

  add_executable(run68_test_conditions
    tests/test_conditions.c
    src/conditions.c
    src/calc.c
  )
  target_include_directories(run68_test_conditions PRIVATE src)
  add_test(NAME condition_codes COMMAND run68_test_conditions)

  add_executable(run68_test_linee
    tests/test_linee.c
    src/linee.c
  )
  target_include_directories(run68_test_linee PRIVATE src)
  add_test(NAME shift_rotate COMMAND run68_test_linee)

  add_executable(run68_test_linec
    tests/test_linec.c
    src/linec.c
  )
  target_include_directories(run68_test_linec PRIVATE src)
  add_test(NAME multiply_abcd COMMAND run68_test_linec)

  add_executable(run68_test_line4_nbcd
    tests/test_line4_nbcd.c
    src/line4.c
    src/exceptions.c
  )
  target_include_directories(run68_test_line4_nbcd PRIVATE src)
  add_test(NAME line4_core COMMAND run68_test_line4_nbcd)

  add_executable(run68_test_eaaccess
    tests/test_eaaccess.c
    src/eaaccess.c
  )
  target_include_directories(run68_test_eaaccess PRIVATE src)
  add_test(NAME effective_address COMMAND run68_test_eaaccess)

  add_executable(run68_test_branches
    tests/test_branches.c
    src/line5.c
    src/line6.c
    src/conditions.c
    src/calc.c
  )
  target_include_directories(run68_test_branches PRIVATE src)
  add_test(NAME branches COMMAND run68_test_branches)

  add_executable(run68_test_addsubx
    tests/test_addsubx.c
    src/lined.c
    src/line9.c
    src/conditions.c
    src/calc.c
  )
  target_include_directories(run68_test_addsubx PRIVATE src)
  add_test(NAME addsubx COMMAND run68_test_addsubx)

  add_executable(run68_test_linef
    tests/test_linef.c
    src/linef.c
  )
  target_include_directories(run68_test_linef PRIVATE src)
  target_link_libraries(run68_test_linef PRIVATE m)
  add_test(NAME fefunc_arithmetic COMMAND run68_test_linef)

  add_executable(run68_test_line0
    tests/test_line0.c
    src/line0.c
  )
  target_include_directories(run68_test_line0 PRIVATE src)
  add_test(NAME immediate_and_bit_ops COMMAND run68_test_line0)

  add_executable(run68_test_lineb
    tests/test_lineb.c
    src/lineb.c
    src/conditions.c
    src/calc.c
  )
  target_include_directories(run68_test_lineb PRIVATE src)
  add_test(NAME compare_instructions COMMAND run68_test_lineb)

  add_executable(run68_test_exceptions
    tests/test_exceptions.c
    src/exceptions.c
  )
  target_include_directories(run68_test_exceptions PRIVATE src)
  add_test(NAME exception_frames COMMAND run68_test_exceptions)

  add_executable(run68_test_iocscall
    tests/test_iocscall.c
    src/iocscall.c
    src/datetime.c
    src/hostconsole.c
    src/exceptions.c
  )
  target_include_directories(run68_test_iocscall PRIVATE src)
  target_link_libraries(run68_test_iocscall PRIVATE ${LIBS})
  add_test(NAME iocs_cli_calls COMMAND run68_test_iocscall)

  if(UNIX OR EMSCRIPTEN)
    add_executable(run68_test_filesearch
      tests/test_filesearch.c
      src/filesearch.c
    )
    target_include_directories(run68_test_filesearch PRIVATE src)
    add_test(NAME dos_filesearch COMMAND run68_test_filesearch)
  endif()
endif()

if(RUN68_ENABLE_SANITIZERS AND CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
  set(RUN68_SANITIZER_FLAGS -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_compile_options(run68 PRIVATE ${RUN68_SANITIZER_FLAGS})
  target_link_options(run68 PRIVATE ${RUN68_SANITIZER_FLAGS})
  target_compile_options(run68_musashi PRIVATE ${RUN68_SANITIZER_FLAGS})
  if(BUILD_TESTING)
    foreach(test_target
            run68_test_musashi_backend
            run68_test_mem
            run68_test_ym2151
            run68_test_audio_wav
            run68_test_msm6258
            run68_test_pcm8
            run68_test_line8
            run68_test_compat
            run68_test_load
            run68_test_conditions
            run68_test_linee
            run68_test_linec
            run68_test_line4_nbcd
            run68_test_eaaccess
            run68_test_branches
            run68_test_addsubx
            run68_test_linef
            run68_test_line0
            run68_test_lineb
            run68_test_exceptions
            run68_test_iocscall)
      target_compile_options(${test_target} PRIVATE ${RUN68_SANITIZER_FLAGS})
      target_link_options(${test_target} PRIVATE ${RUN68_SANITIZER_FLAGS})
    endforeach()
    target_compile_options(run68_ymfm PRIVATE ${RUN68_SANITIZER_FLAGS})
    if(UNIX OR EMSCRIPTEN)
      target_compile_options(run68_test_filesearch PRIVATE ${RUN68_SANITIZER_FLAGS})
      target_link_options(run68_test_filesearch PRIVATE ${RUN68_SANITIZER_FLAGS})
    endif()
  endif()
endif()

INSTALL(TARGETS run68 RUNTIME DESTINATION bin)
