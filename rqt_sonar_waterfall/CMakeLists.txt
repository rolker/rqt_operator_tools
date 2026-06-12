cmake_minimum_required(VERSION 3.10)
project(rqt_sonar_waterfall)

# C++ standard is enforced per-target via target_compile_features below
# (matches the sibling rqt_camera_grid package).

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(ament_cmake_ros REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rqt_gui_cpp REQUIRED)
find_package(qt_gui_cpp REQUIRED)
find_package(pluginlib REQUIRED)
find_package(marine_acoustic_msgs REQUIRED)
find_package(marine_radar_control_msgs REQUIRED)
find_package(marine_colormap REQUIRED)
find_package(ament_index_cpp REQUIRED)
find_package(Qt5 COMPONENTS Widgets Gui OpenGL REQUIRED)

set(CMAKE_AUTOMOC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(${PROJECT_NAME}_HEADERS
  include/${PROJECT_NAME}/sonar_waterfall_plugin.hpp
  include/${PROJECT_NAME}/waterfall_widget.hpp
  include/${PROJECT_NAME}/waterfall_model.hpp
  include/${PROJECT_NAME}/row_extractor.hpp
  include/${PROJECT_NAME}/waterfall_buffer.hpp
  include/${PROJECT_NAME}/color_map.hpp
  include/${PROJECT_NAME}/gpu_color_map.hpp
  include/${PROJECT_NAME}/ping_pairer.hpp
  include/${PROJECT_NAME}/topic_filter.hpp
  include/${PROJECT_NAME}/control_panel.hpp
)

set(${PROJECT_NAME}_SOURCES
  src/sonar_waterfall_plugin.cpp
  src/waterfall_widget.cpp
  src/waterfall_model.cpp
  src/row_extractor.cpp
  src/waterfall_buffer.cpp
  src/color_map.cpp
  src/gpu_color_map.cpp
  src/ping_pairer.cpp
  src/topic_filter.cpp
  src/control_panel.cpp
)

add_library(${PROJECT_NAME} SHARED
  ${${PROJECT_NAME}_SOURCES}
  ${${PROJECT_NAME}_HEADERS}
)

# Require C99 and C++17 — propagates to anyone who links this library.
target_compile_features(${PROJECT_NAME} PUBLIC c_std_99 cxx_std_17)

target_include_directories(${PROJECT_NAME} PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)

ament_target_dependencies(${PROJECT_NAME}
  rclcpp
  rqt_gui_cpp
  qt_gui_cpp
  pluginlib
  marine_acoustic_msgs
  marine_radar_control_msgs
  marine_colormap
  ament_index_cpp
)

target_link_libraries(${PROJECT_NAME} Qt5::Widgets Qt5::Gui Qt5::OpenGL)

pluginlib_export_plugin_description_file(rqt_gui plugin.xml)

install(TARGETS ${PROJECT_NAME}
  EXPORT export_${PROJECT_NAME}
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin
)

install(DIRECTORY include/
  DESTINATION include
)

install(FILES plugin.xml
  DESTINATION share/${PROJECT_NAME}
)

install(PROGRAMS
  scripts/${PROJECT_NAME}
  scripts/synthetic_sidescan.py
  DESTINATION lib/${PROJECT_NAME}
)

install(FILES resource/${PROJECT_NAME}
  DESTINATION share/ament_index/resource_index/packages
)

if(BUILD_TESTING)
  find_package(ament_lint_auto REQUIRED)
  find_package(ament_cmake_gtest REQUIRED)

  ament_add_gtest(test_decode_samples
    test/test_decode_samples.cpp
    src/waterfall_model.cpp
  )
  target_include_directories(test_decode_samples PRIVATE include)
  ament_target_dependencies(test_decode_samples marine_acoustic_msgs)

  ament_add_gtest(test_combine_rows
    test/test_combine_rows.cpp
    src/waterfall_model.cpp
  )
  target_include_directories(test_combine_rows PRIVATE include)
  ament_target_dependencies(test_combine_rows marine_acoustic_msgs)

  ament_add_gtest(test_single_beam_extractor
    test/test_single_beam_extractor.cpp
    src/row_extractor.cpp
    src/waterfall_model.cpp
  )
  target_include_directories(test_single_beam_extractor PRIVATE include)
  ament_target_dependencies(test_single_beam_extractor marine_acoustic_msgs)

  ament_add_gtest(test_waterfall_buffer
    test/test_waterfall_buffer.cpp
    src/waterfall_buffer.cpp
  )
  target_include_directories(test_waterfall_buffer PRIVATE include)
  ament_target_dependencies(test_waterfall_buffer marine_acoustic_msgs)

  ament_add_gtest(test_color_map
    test/test_color_map.cpp
    src/color_map.cpp
  )
  target_include_directories(test_color_map PRIVATE include)
  ament_target_dependencies(test_color_map marine_acoustic_msgs marine_colormap)

  ament_add_gtest(test_waterfall_widget
    test/test_waterfall_widget.cpp
  )
  target_include_directories(test_waterfall_widget PRIVATE include)
  ament_target_dependencies(test_waterfall_widget marine_acoustic_msgs marine_colormap)
  # Link the main library so the widget's MOC metaobjects are available.
  # Gui/OpenGL for QOpenGLWidget + the offscreen GL context the test creates.
  target_link_libraries(test_waterfall_widget
    ${PROJECT_NAME} Qt5::Widgets Qt5::Gui Qt5::OpenGL)
  set_tests_properties(test_waterfall_widget PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;LIBGL_ALWAYS_SOFTWARE=1")

  ament_add_gtest(test_ping_pairer
    test/test_ping_pairer.cpp
    src/ping_pairer.cpp
    src/waterfall_model.cpp
  )
  target_include_directories(test_ping_pairer PRIVATE include)
  ament_target_dependencies(test_ping_pairer marine_acoustic_msgs)

  ament_add_gtest(test_topic_filter
    test/test_topic_filter.cpp
    src/topic_filter.cpp
  )
  target_include_directories(test_topic_filter PRIVATE include)

  ament_add_gtest(test_control_panel
    test/test_control_panel.cpp
  )
  target_include_directories(test_control_panel PRIVATE include)
  ament_target_dependencies(test_control_panel marine_radar_control_msgs)
  # Link the main library so the panel's MOC metaobjects are available.
  target_link_libraries(test_control_panel ${PROJECT_NAME} Qt5::Widgets)
  set_tests_properties(test_control_panel PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

  # Offscreen-GL CPU/GPU colormap parity test. Self-skips when no GL context can
  # be created, so it never fails on a headless runner without software GL.
  ament_add_gtest(test_gpu_color_map
    test/test_gpu_color_map.cpp
  )
  target_include_directories(test_gpu_color_map PRIVATE include)
  ament_target_dependencies(test_gpu_color_map marine_colormap)
  target_link_libraries(test_gpu_color_map
    ${PROJECT_NAME} Qt5::Widgets Qt5::Gui Qt5::OpenGL)
  set_tests_properties(test_gpu_color_map PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;LIBGL_ALWAYS_SOFTWARE=1")

  ament_lint_auto_find_test_dependencies()
endif()

ament_export_include_directories(include)
ament_export_libraries(${PROJECT_NAME})
ament_export_targets(export_${PROJECT_NAME})
# marine_colormap must be exported: the installed export set links the
# marine_colormap::marine_colormap target into this library's link interface,
# so any downstream consumer (e.g. rqt_marine_sonar) needs the find_package
# pulled in by its Config -- without it, importing this package fails with
# "target marine_colormap::marine_colormap not found".
ament_export_dependencies(
  rclcpp rqt_gui_cpp qt_gui_cpp pluginlib marine_acoustic_msgs
  marine_radar_control_msgs ament_index_cpp marine_colormap Qt5
)

ament_package()
