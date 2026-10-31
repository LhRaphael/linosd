cmake_minimum_required(VERSION 3.16)
project(linosd VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# LINOSD_HOOK_ONLY: compila apenas as bibliotecas de hook (usado no build 32-bit).
option(LINOSD_HOOK_ONLY "Compilar somente os hooks (Vulkan/OpenGL)" OFF)
option(LINOSD_BUILD_APP "Compilar o aplicativo Qt" ON)

include(GNUInstallDirs)
include(CheckCXXSourceCompiles)

# Arquitetura dos hooks: x86_64 ou i686 (build 32-bit com -m32). O linosd-run usa
# o token $PLATFORM do ld.so para escolher a versão correta em cada processo.
if(CMAKE_SIZEOF_VOID_P EQUAL 4)
    set(LINOSD_ARCH "i686")
else()
    set(LINOSD_ARCH "x86_64")
endif()
set(LINOSD_HOOK_LIBDIR "lib/linosd/${LINOSD_ARCH}" CACHE STRING "Diretório de instalação dos hooks")

# libstdc++ estática nos hooks evita conflitos com a libstdc++ do jogo / Steam Runtime.
set(CMAKE_REQUIRED_LINK_OPTIONS "-static-libstdc++;-static-libgcc")
check_cxx_source_compiles("#include <string>\nint main(){std::string s(\"x\");return (int)s.size()-1;}" LINOSD_HAVE_STATIC_LIBSTDCXX)
unset(CMAKE_REQUIRED_LINK_OPTIONS)
if(LINOSD_HAVE_STATIC_LIBSTDCXX)
    set(LINOSD_STATIC_RT -static-libstdc++ -static-libgcc)
else()
    message(WARNING "libstdc++ estática indisponível (instale libstdc++-static); hooks usarão a dinâmica.")
    set(LINOSD_STATIC_RT "")
endif()

add_compile_options(-Wall -Wextra)

# ---------------------------------------------------------------------------
# Hooks (sem Qt, carregados dentro do jogo)
# ---------------------------------------------------------------------------
find_path(VULKAN_INCLUDE_DIR vulkan/vulkan.h)
if(NOT VULKAN_INCLUDE_DIR)
    message(FATAL_ERROR "Headers do Vulkan não encontrados (instale vulkan-headers / libvulkan-dev).")
endif()

add_library(linosd_recorder STATIC src/hook/frame_recorder.cpp)
set_target_properties(linosd_recorder PROPERTIES POSITION_INDEPENDENT_CODE ON CXX_VISIBILITY_PRESET hidden)
target_link_libraries(linosd_recorder PRIVATE rt pthread)

# Camada implícita Vulkan
add_library(linosd_vk SHARED src/hook/vk_layer.cpp)
target_include_directories(linosd_vk PRIVATE ${VULKAN_INCLUDE_DIR})
set_target_properties(linosd_vk PROPERTIES CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)
target_link_libraries(linosd_vk PRIVATE linosd_recorder)
# Exporta apenas os símbolos da camada; libstdc++ estática fica oculta para não
# interferir no processo do jogo.
target_link_options(linosd_vk PRIVATE -Wl,--no-undefined -Wl,-z,nodelete ${LINOSD_STATIC_RT}
    -Wl,--exclude-libs,ALL -Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/src/hook/vk_layer.map)
set_target_properties(linosd_vk PROPERTIES LINK_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/hook/vk_layer.map)

# Hook OpenGL (LD_PRELOAD)
add_library(linosd_gl SHARED src/hook/gl_hook.cpp)
set_target_properties(linosd_gl PROPERTIES CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)
target_link_libraries(linosd_gl PRIVATE linosd_recorder dl)
target_link_options(linosd_gl PRIVATE -Wl,--no-undefined ${LINOSD_STATIC_RT}
    -Wl,--exclude-libs,ALL -Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/src/hook/gl_hook.map)
set_target_properties(linosd_gl PROPERTIES LINK_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/hook/gl_hook.map)

install(TARGETS linosd_vk linosd_gl LIBRARY DESTINATION ${LINOSD_HOOK_LIBDIR})

if(LINOSD_HOOK_ONLY OR NOT LINOSD_BUILD_APP)
    return()
endif()

# ---------------------------------------------------------------------------
# Núcleo de métricas (sem Qt)
# ---------------------------------------------------------------------------
add_library(linosd_core STATIC
    src/core/sysfs.cpp
    src/core/system_monitor.cpp
    src/core/gpu_monitor.cpp
    src/core/frame_source.cpp
)
target_link_libraries(linosd_core PUBLIC dl rt)

# ---------------------------------------------------------------------------
# Aplicativo Qt (overlay + interface de configuração)
# ---------------------------------------------------------------------------
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets Network)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets Network)
set(CMAKE_AUTOMOC ON)

add_executable(linosd
    src/app/main.cpp
    src/app/config.cpp
    src/app/metrics.cpp
    src/app/osd_renderer.cpp
    src/app/overlay_window.cpp
    src/app/config_window.cpp
    src/app/widgets.cpp
)
target_include_directories(linosd PRIVATE src)
target_link_libraries(linosd PRIVATE linosd_core Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::Network)
target_compile_definitions(linosd PRIVATE LINOSD_VERSION="${PROJECT_VERSION}")

install(TARGETS linosd RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
