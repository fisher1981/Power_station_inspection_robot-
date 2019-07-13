#!/bin/bash

source ~/ros/zw/distributed_robot_sys/sh/ip.sh
echo $MASTER_IP

source devel/setup.bash
export ROS_MASTER_URI=http://$MASTER_IP
roslaunch a_robot_platform location.launch 
