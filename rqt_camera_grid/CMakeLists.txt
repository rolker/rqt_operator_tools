cmake_minimum_required(VERSION 3.10)
project(rqt_camera_grid)

# C++ standard is enforced per-target via target_compile_features below
# (matches the current ros2 pkg create template on Rolling, which replaced
# the older `if(NOT CMAKE_CXX_STANDARD) set(... 17) endif()` guard).

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(ament_cmake_ros REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rqt_gui_cpp REQUIRED)
find_package(qt_gui_cpp REQUIRED)
find_package(pluginlib REQUIRED)
find_package(image_transport REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(cv_bridge REQUIRED)
find_package(ament_index_cpp REQUIRED)
find_package(yaml-cpp REQUIRED)
find_package(Qt5 COMPONENTS Widgets REQUIRED)

set(CMAKE_AUTOMOC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(${PROJECT_NAME}_HEADERS
  include/${PROJECT_NAME}/camera_grid_plugin.hpp
  include/${PROJECT_NAME}/camera_grid_widget.hpp
  include/${PROJECT_NAME}/camera_pane_widget.hpp
  include/${PROJECT_NAME}/config_dialog.hpp
  include/${PROJECT_NAME}/thumbnail_cell.hpp
)

set(${PROJECT_NAME}_SOURCES
  src/camera_grid_plugin.cpp
  src/camera_grid_widget.cpp
  src/camera_pane_widget.cpp
  src/staleness_tracker.cpp
  src/grid_layout.cpp
  src/config_model.cpp
  src/config_dialog.cpp
  src/thumbnail_cell.cpp
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
  image_transport
  sensor_msgs
  cv_bridge
  ament_index_cpp
)

target_link_libraries(${PROJECT_NAME} Qt5::Widgets yaml-cpp)

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

install(DIRECTORY config launch
  DESTINATION share/${PROJECT_NAME}
)

install(PROGRAMS scripts/demo_transform.py
  DESTINATION lib/${PROJECT_NAME}
)

install(FILES resource/${PROJECT_NAME}
  DESTINATION share/ament_index/resource_index/packages
)

if(BUILD_TESTING)
  find_package(ament_lint_auto REQUIRED)
  find_package(ament_cmake_gtest REQUIRED)

  ament_add_gtest(test_staleness_tracker
    test/test_staleness_tracker.cpp
    src/staleness_tracker.cpp
  )
  target_include_directories(test_staleness_tracker PRIVATE include)
  ament_target_dependencies(test_staleness_tracker rclcpp)

  ament_add_gtest(test_grid_layout
    test/test_grid_layout.cpp
    src/grid_layout.cpp
  )
  target_include_directories(test_grid_layout PRIVATE include)

  ament_add_gtest(test_config_model
    test/test_config_model.cpp
    src/config_model.cpp
  )
  target_include_directories(test_config_model PRIVATE include)
  target_link_libraries(test_config_model yaml-cpp)

  ament_add_gtest(test_pane_lifecycle
    test/test_pane_lifecycle.cpp
  )
  target_include_directories(test_pane_lifecycle PRIVATE include)
  ament_target_dependencies(test_pane_lifecycle
    rclcpp image_transport sensor_msgs cv_bridge)
  # Link against the main library so MOC-generated metaobjects are present.
  target_link_libraries(test_pane_lifecycle ${PROJECT_NAME} Qt5::Widgets)
  set_tests_properties(test_pane_lifecycle PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

  ament_add_gtest(test_config_dialog_helpers
    test/test_config_dialog_helpers.cpp
  )
  target_include_directories(test_config_dialog_helpers PRIVATE include)
  target_link_libraries(test_config_dialog_helpers Qt5::Core)

  ament_lint_auto_find_test_dependencies()
endif()

ament_export_include_directories(include)
ament_export_libraries(${PROJECT_NAME})
ament_export_targets(export_${PROJECT_NAME})
ament_export_dependencies(
  rclcpp rqt_gui_cpp qt_gui_cpp pluginlib image_transport sensor_msgs cv_bridge
  ament_index_cpp Qt5 yaml-cpp
)

ament_package()
