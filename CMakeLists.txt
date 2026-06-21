# Must be set before cmake_minimum_required so sub-projects with old min-version don't error
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
cmake_minimum_required(VERSION 3.20)
project(q1config CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# ── nlohmann/json ────────────────────────────────────────────────────────────
FetchContent_Declare(nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG        v3.11.3
  GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(nlohmann_json)

# ── hidapi ───────────────────────────────────────────────────────────────────
FetchContent_Declare(hidapi
  GIT_REPOSITORY https://github.com/libusb/hidapi.git
  GIT_TAG        hidapi-0.14.0
  GIT_SHALLOW    TRUE
  OVERRIDE_FIND_PACKAGE
)
FetchContent_MakeAvailable(hidapi)

# ── GLFW ─────────────────────────────────────────────────────────────────────
FetchContent_Declare(glfw
  GIT_REPOSITORY https://github.com/glfw/glfw.git
  GIT_TAG        3.4
  GIT_SHALLOW    TRUE
)
set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glfw)

# ── Dear ImGui ───────────────────────────────────────────────────────────────
FetchContent_Declare(imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG        v1.90.9
  GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(imgui)

find_package(OpenGL REQUIRED)

add_library(imgui_lib STATIC
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui_lib PUBLIC
  ${imgui_SOURCE_DIR}
  ${imgui_SOURCE_DIR}/backends
)
target_link_libraries(imgui_lib PUBLIC glfw OpenGL::GL)

# ── nativefiledialog-extended ─────────────────────────────────────────────────
FetchContent_Declare(nfd
  GIT_REPOSITORY https://github.com/btzy/nativefiledialog-extended.git
  GIT_TAG        v1.2.1
  GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(nfd)

# ── q1config ──────────────────────────────────────────────────────────────────
add_executable(q1config
  src/main.cpp
  src/hid.cpp
  src/protocol.cpp
  src/keycodes.cpp
  src/layout.cpp
  src/preset.cpp
  src/gui.cpp
  src/cli.cpp
)

target_include_directories(q1config PRIVATE src)

# hidapi exports platform-specific targets; pick the right one
if(WIN32)
  set(HIDAPI_TARGET hidapi::winapi)
elseif(APPLE)
  set(HIDAPI_TARGET hidapi::darwin)
else()
  # Linux hidraw backend (needs libudev) — matches the hidraw udev rule below
  set(HIDAPI_TARGET hidapi::hidraw)
endif()

target_link_libraries(q1config PRIVATE
  imgui_lib
  ${HIDAPI_TARGET}
  nlohmann_json::nlohmann_json
  nfd
  OpenGL::GL
)

if(WIN32)
  # setupapi needed by hidapi on Windows
  target_link_libraries(q1config PRIVATE setupapi)
  # Windows subsystem (no console ever created); CLI re-attaches via AttachConsole
  set_target_properties(q1config PROPERTIES WIN32_EXECUTABLE TRUE)
  target_link_options(q1config PRIVATE /ENTRY:mainCRTStartup)
  # copy hidapi.dll next to the exe after build
  add_custom_command(TARGET q1config POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      $<TARGET_FILE:hidapi::winapi>
      $<TARGET_FILE_DIR:q1config>
  )
elseif(APPLE)
  # hidapi on macOS needs IOKit and CoreFoundation
  target_link_libraries(q1config PRIVATE
    "-framework IOKit"
    "-framework CoreFoundation"
  )
  # Suppress macOS OpenGL deprecation warnings (OpenGL still works, just deprecated since 10.14)
  target_compile_definitions(q1config PRIVATE GL_SILENCE_DEPRECATION)
else()
  # Linux: hidapi uses the hidraw backend (install libudev-dev)
  # nfd uses GTK3 (install libgtk-3-dev) or xdg-desktop-portal
  # HID device access without root requires a udev rule, e.g.:
  #   /etc/udev/rules.d/70-keychron.rules:
  #   SUBSYSTEM=="hidraw", ATTRS{idVendor}=="3434", ATTRS{idProduct}=="0610", MODE="0666"
endif()
