#!/usr/bin/env bash

# Setup ROS for Local Development
source /opt/ros/kinetic/setup.bash
source ~/2018RobotCode/zebROS_ws/devel/setup.bash
export ROS_MASTER_URI=http://localhost
export ROS_IP=10.9.0.11 #`/bin/hostname -I | tr -d ' ' | tr -d '\n'`
export LD_LIBRARY_PATH=/usr/local/lib:$LD_LIBRARY_PATH

exec "$@"
