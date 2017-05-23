cmake_minimum_required(VERSION 2.8.3)
project(ubr_pickup)

find_package(catkin REQUIRED COMPONENTS
  ubr1_grasping
  ubr1_gazebo
  rospy
  roscpp
)

catkin_package(
)

include_directories(
  ${catkin_INCLUDE_DIRS}
)
