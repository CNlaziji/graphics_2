cmake_minimum_required(VERSION 3.15)
project(GraphicsLab_ShadowMapping C CXX)

# 设置 C++17 标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 生成 compile_commands.json 供 IntelliSense 使用
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 包含头文件目录
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/include/custom
    ${CMAKE_CURRENT_SOURCE_DIR}/glm
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/backends
    ${CMAKE_CURRENT_SOURCE_DIR}/include/GLFW
)

# 搜集源文件（main.cpp 和 glad.c 必须联合编译）
set(SOURCE_FILES
    main.cpp
    src/glad.c
    src/stb_image_impl.cpp  # 成员 C：stb_image 实现编译单元
    include/custom/ShadowPipeline.hpp
    include/custom/Model.hpp
    # 成员 E：ImGui 库
    third_party/imgui.cpp
    third_party/imgui_draw.cpp
    third_party/imgui_widgets.cpp
    third_party/imgui_tables.cpp
    third_party/backends/imgui_impl_glfw.cpp
    third_party/backends/imgui_impl_opengl3.cpp
)

# 创建可执行文件
add_executable(${PROJECT_NAME} ${SOURCE_FILES})

# 链接本地 GLFW 库配置
if(WIN32)
    # Windows 环境下链接 lib/ 目录下的静态库
    target_link_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/lib)
    
    # 检测编译器类型：MinGW 使用 libglfw3.a，MSVC 使用 glfw3.lib
    if(MINGW)
        target_link_libraries(${PROJECT_NAME} PRIVATE glfw3 opengl32 gdi32)
    else()
        target_link_libraries(${PROJECT_NAME} PRIVATE glfw3 opengl32)
    endif()
    
    # 自动拷贝 dll 文件到执行目录，防止运行时提示找不到 glfw3.dll
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/lib/glfw3.dll")
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/lib/glfw3.dll"
            $<TARGET_FILE_DIR:${PROJECT_NAME}>
        )
    endif()
else()
    # macOS / Linux 环境（如果组员有其他系统，通常直接链接系统全局 GLFW）
    find_package(GLFW3 REQUIRED)
    find_package(OpenGL REQUIRED)
    target_link_libraries(${PROJECT_NAME} PRIVATE GLFW3::GLFW3 OpenGL::GL)
endif()