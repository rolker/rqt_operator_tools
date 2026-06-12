cmake_minimum_required(VERSION 3.10)
project(rqt_marine_sonar)

# C++ standard is enforced per-target via target_compile_features below
# (matches the sibling rqt_sonar_waterfall package).

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
# OpenGL is not used here directly, but rqt_sonar_waterfall's exported link
# interface references Qt5::OpenGL (its GPU path), so the target must exist
# before that package is imported.
find_package(Qt5 COMPONENTS Widgets Gui Charts OpenGL REQUIRED)
# After Qt5-with-components: rqt_sonar_waterfall's exported dependencies
# include a bare Qt5, which only resolves once components are already found.
find_package(rqt_sonar_waterfall REQUIRED)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(${PROJECT_NAME}_HEADERS
  include/${PROJECT_NAME}/marine_echogram_plugin.hpp
  include/${PROJECT_NAME}/echogram_widget.hpp
  include/${PROJECT_NAME}/ping.hpp
)

set(${PROJECT_NAME}_SOURCES
  src/marine_echogram_plugin.cpp
  src/echogram_widget.cpp
  src/ping.cpp
)

set(${PROJECT_NAME}_UIS
  src/marine_echogram_plugin.ui
)

add_library(${PROJECT_NAME} SHARED
  ${${PROJECT_NAME}_SOURCES}
  ${${PROJECT_NAME}_HEADERS}
  ${${PROJECT_NAME}_UIS}
)

# Require C99 and C++17 — propagates to anyone who links this library.
target_compile_features(${PROJECT_NAME} PUBLIC c_std_99 cxx_std_17)

# The .ui lives in src/ but is included (as ui_marine_echogram_plugin.h) from a
# header in include/; tell AUTOUIC where to find it.
set_target_properties(${PROJECT_NAME} PROPERTIES
  AUTOUIC_SEARCH_PATHS ${CMAKE_CURRENT_SOURCE_DIR}/src)

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
  rqt_sonar_waterfall
)

target_link_libraries(${PROJECT_NAME} Qt5::Widgets Qt5::Gui Qt5::Charts)

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
  DESTINATION lib/${PROJECT_NAME}
)

install(FILES resource/${PROJECT_NAME}
  DESTINATION share/ament_index/resource_index/packages
)

if(BUILD_TESTING)
  find_package(ament_lint_auto REQUIRED)
  find_package(ament_cmake_gtest REQUIRED)

  ament_add_gtest(test_ping
    test/test_ping.cpp
    src/ping.cpp
  )
  target_include_directories(test_ping PRIVATE include)
  ament_target_dependencies(test_ping marine_acoustic_msgs rqt_sonar_waterfall)

  # Offscreen widget smoke/robustness test. Links the main library so the
  # widget's MOC metaobjects are available; CPU QtCharts rendering needs no GL,
  # only the offscreen QPA platform.
  ament_add_gtest(test_echogram_widget
    test/test_echogram_widget.cpp
  )
  target_include_directories(test_echogram_widget PRIVATE include)
  ament_target_dependencies(test_echogram_widget marine_acoustic_msgs)
  target_link_libraries(test_echogram_widget
    ${PROJECT_NAME} Qt5::Widgets Qt5::Gui Qt5::Charts)
  set_tests_properties(test_echogram_widget PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

  ament_lint_auto_find_test_dependencies()
endif()

ament_export_include_directories(include)
ament_export_libraries(${PROJECT_NAME})
ament_export_targets(export_${PROJECT_NAME})
ament_export_dependencies(
  rclcpp rqt_gui_cpp qt_gui_cpp pluginlib marine_acoustic_msgs
  rqt_sonar_waterfall Qt5)

ament_package()
