cmake_minimum_required(VERSION 3.8)
project(marine_control_widgets)

if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 17)
endif()

find_package(ament_cmake REQUIRED)
find_package(marine_control_interfaces REQUIRED)
find_package(Qt5 COMPONENTS Widgets REQUIRED)

set(CMAKE_AUTOMOC ON)

add_library(${PROJECT_NAME} SHARED
  # The header is listed so AUTOMOC generates the Q_OBJECT meta-object for it.
  include/${PROJECT_NAME}/control_set_widget.hpp
  src/control_set_widget.cpp
)
target_include_directories(${PROJECT_NAME} PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
ament_target_dependencies(${PROJECT_NAME} marine_control_interfaces)
target_link_libraries(${PROJECT_NAME} Qt5::Widgets)

install(DIRECTORY include/ DESTINATION include)
install(
  TARGETS ${PROJECT_NAME}
  EXPORT export_${PROJECT_NAME}
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin)

ament_export_targets(export_${PROJECT_NAME} HAS_LIBRARY_TARGET)
# Export Qt5 with the component a downstream linker needs (the exported target
# links Qt5::Widgets), so consumers don't have to find Qt5 components first.
ament_export_dependencies(marine_control_interfaces Qt5 Qt5Widgets)

if(BUILD_TESTING)
  find_package(ament_lint_auto REQUIRED)
  ament_lint_auto_find_test_dependencies()

  find_package(ament_cmake_gtest REQUIRED)
  ament_add_gtest(test_control_set_widget test/test_control_set_widget.cpp)
  target_link_libraries(test_control_set_widget ${PROJECT_NAME} Qt5::Widgets)
  ament_target_dependencies(test_control_set_widget marine_control_interfaces)
  # The widget renders on the CPU; run headless.
  set_tests_properties(test_control_set_widget PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()

ament_package()
