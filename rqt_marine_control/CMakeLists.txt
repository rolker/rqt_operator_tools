cmake_minimum_required(VERSION 3.10)
project(rqt_marine_control)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(ament_cmake_ros REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rqt_gui_cpp REQUIRED)
find_package(qt_gui_cpp REQUIRED)
find_package(pluginlib REQUIRED)
find_package(marine_control_interfaces REQUIRED)
find_package(Qt5 COMPONENTS Widgets REQUIRED)
# After Qt5-with-components: marine_control_widgets's exported dependencies
# include a bare Qt5, which only resolves once components are already found.
find_package(marine_control_widgets REQUIRED)
find_package(marine_control_bridge_client REQUIRED)

set(CMAKE_AUTOMOC ON)

set(${PROJECT_NAME}_HEADERS
  include/${PROJECT_NAME}/connections_hub_widget.hpp
  include/${PROJECT_NAME}/marine_control_plugin.hpp
  include/${PROJECT_NAME}/responsive_hub_layout.hpp
  include/${PROJECT_NAME}/tab_manager.hpp
  include/${PROJECT_NAME}/topic_filter.hpp
)
set(${PROJECT_NAME}_SOURCES
  src/connections_hub_widget.cpp
  src/marine_control_plugin.cpp
  src/responsive_hub_layout.cpp
  src/tab_manager.cpp
  src/topic_filter.cpp
)

add_library(${PROJECT_NAME} SHARED
  ${${PROJECT_NAME}_SOURCES}
  ${${PROJECT_NAME}_HEADERS}
)
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
  marine_control_interfaces
  marine_control_widgets
  marine_control_bridge_client
)
target_link_libraries(${PROJECT_NAME} Qt5::Widgets)

pluginlib_export_plugin_description_file(rqt_gui plugin.xml)

install(TARGETS ${PROJECT_NAME}
  EXPORT export_${PROJECT_NAME}
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin
)
install(DIRECTORY include/ DESTINATION include)
install(FILES plugin.xml DESTINATION share/${PROJECT_NAME})
install(PROGRAMS scripts/${PROJECT_NAME} DESTINATION lib/${PROJECT_NAME})
install(FILES resource/${PROJECT_NAME}
  DESTINATION share/ament_index/resource_index/packages)

if(BUILD_TESTING)
  find_package(ament_lint_auto REQUIRED)
  find_package(ament_cmake_gtest REQUIRED)

  ament_add_gtest(test_topic_filter
    test/test_topic_filter.cpp
    src/topic_filter.cpp
  )
  target_include_directories(test_topic_filter PRIVATE include)

  # Node-free tab-lifecycle tests: TabManager drives subscriptions through an
  # injected transport factory, so a counting fake exercises open/close/no-leak/
  # no-cross-talk without a live ROS node.
  ament_add_gtest(test_tab_manager
    test/test_tab_manager.cpp
    src/tab_manager.cpp
  )
  target_include_directories(test_tab_manager PRIVATE include)
  target_link_libraries(test_tab_manager Qt5::Widgets)
  ament_target_dependencies(test_tab_manager
    marine_control_interfaces marine_control_widgets)
  # The widgets render on the CPU; run headless.
  set_tests_properties(test_tab_manager PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

  # Node-free hub tests: the ConnectionsHubWidget drives the bridge through an
  # injected BridgeControlHooks and tabs through an injected TabTransportFactory,
  # so fakes exercise check<->tab sync, local/bridged de-dup, and desired-set
  # reconcile without a live ROS node or a real BridgeControlClient.
  ament_add_gtest(test_connections_hub
    test/test_connections_hub.cpp
    src/connections_hub_widget.cpp
    include/${PROJECT_NAME}/connections_hub_widget.hpp
    src/tab_manager.cpp
  )
  set_target_properties(test_connections_hub PROPERTIES AUTOMOC ON)
  target_include_directories(test_connections_hub PRIVATE include)
  target_link_libraries(test_connections_hub Qt5::Widgets)
  ament_target_dependencies(test_connections_hub
    marine_control_interfaces marine_control_widgets marine_control_bridge_client qt_gui_cpp)
  set_tests_properties(test_connections_hub PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

  # Reparent/hysteresis tests for the responsive layout: prove hub state survives
  # the wide<->narrow reparent and that a width in the dead band does not thrash.
  ament_add_gtest(test_responsive_hub_layout
    test/test_responsive_hub_layout.cpp
    src/responsive_hub_layout.cpp
    include/${PROJECT_NAME}/responsive_hub_layout.hpp
    src/tab_manager.cpp
  )
  set_target_properties(test_responsive_hub_layout PROPERTIES AUTOMOC ON)
  target_include_directories(test_responsive_hub_layout PRIVATE include)
  target_link_libraries(test_responsive_hub_layout Qt5::Widgets)
  ament_target_dependencies(test_responsive_hub_layout
    marine_control_interfaces marine_control_widgets qt_gui_cpp)
  set_tests_properties(test_responsive_hub_layout PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

  ament_lint_auto_find_test_dependencies()
endif()

ament_export_include_directories(include)
ament_export_libraries(${PROJECT_NAME})
ament_export_targets(export_${PROJECT_NAME})
ament_export_dependencies(
  rclcpp rqt_gui_cpp qt_gui_cpp pluginlib
  marine_control_interfaces marine_control_widgets marine_control_bridge_client Qt5)

ament_package()
