cmake_minimum_required(VERSION 2.8.3)
project(heartbeat_node)

include ("../cmake_modules/CMakeOpt.cmake")

find_package(catkin REQUIRED COMPONENTS
  cmake_modules
  controller_manager
  roscpp
  std_msgs
  ros_control_boilerplate
)

#add_message_files (
#  FILES
#)
#generate_messages(
#  DEPENDENCIES
#  std_msgs
#)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${PROJECT_SOURCE_DIR}/cmake)

## System dependencies are found with CMake's conventions
# find_package(Boost REQUIRED COMPONENTS system)

catkin_package(
  CATKIN_DEPENDS
    controller_manager
    roscpp
    std_msgs
    ros_control_boilerplate
  LIBRARIES
  DEPENDS 
)

include_directories(
  ${catkin_INCLUDE_DIRS}
)




###########
## Build ##
###########


## Add cmake target dependencies of the executable
## same as for the library above. This is needed to force
## talon controller messages to be built before the
## talon controller code tries to use them
add_dependencies(
  ${${PROJECT_NAME}_EXPORTED_TARGETS}
  ${catkin_EXPORTED_TARGETS}
)

add_executable(heartbeat src/heartbeat.cpp)
set_target_properties(heartbeat PROPERTIES OUTPUT_NAME 
heartbeat PREFIX "")
target_link_libraries(heartbeat
  ${catkin_LIBRARIES}
)
## Add cmake target dependencies of the executable
## same as for the library above. This is needed to force
## talon controller messages to be built before the
## talon controller code tries to use them

## TOOLS ------------------------------------------------------

# Install ------------------------------------------------------------

#install(DIRECTORY
#launch
#config
#DESTINATION ${CATKIN_PACKAGE_SHARE_DESTINATION}
#)

install(TARGETS heartbeat heartbeat
   ARCHIVE DESTINATION ${CATKIN_PACKAGE_LIB_DESTINATION}
   LIBRARY DESTINATION ${CATKIN_PACKAGE_LIB_DESTINATION}
   RUNTIME DESTINATION ${CATKIN_PACKAGE_BIN_DESTINATION}
 )


