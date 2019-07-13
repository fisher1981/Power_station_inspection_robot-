#ifndef UARTODOMPTHREAD_H
#define UARTODOMPTHREAD_H

#include <pthread.h>
#include <ros/ros.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <geometry_msgs/Twist.h>
#include <geometry_msgs/PoseStamped.h>
#include <geometry_msgs/PoseWithCovarianceStamped.h>
#include <nav_msgs/Odometry.h>
#include <sensor_msgs/Imu.h>
#include <tf/transform_broadcaster.h>
#include <math.h>
#include <termios.h>
#include "../nav/nav.h"
#include <nav_msgs/OccupancyGrid.h>
#include <sensor_msgs/PointCloud.h>


namespace zw {


class UartOdomPthread
{
private:
  pthread_t id;
  geometry_msgs::Twist vel;




private:
  float maxForwardSpeed;
  float maxBackSpeed;
  float maxOmega;
  float maxDisErr;
  float maxAngErr;
  float maxUpAcc;
  float maxBackAcc;
  float fabsdfh;
  float poseChange;


public:
  UartOdomPthread();
  ~UartOdomPthread();

private:
  static void cmd_keyCallback(const geometry_msgs::Twist::ConstPtr & cmd);
//  static void PoseReceived(const geometry_msgs::PoseStampedConstPtr pose);
  static void PoseReceived(const geometry_msgs::PoseWithCovarianceStampedConstPtr pose);
  static void goal_Callback(const geometry_msgs::PoseStamped& msgPose);
  static void SubMapReceived(const nav_msgs::OccupancyGridConstPtr& msg);
  static void MapReceived(const nav_msgs::OccupancyGridConstPtr &msg);
  static void Astarpath_Callback(const sensor_msgs::PointCloudConstPtr& msgpoints);
  static void *MyPthread(void *temp);
  virtual void *DoPthread(void);
  void getNavcmd(void);
  void CalNavCmdVel(const NavPara *nav ,geometry_msgs::Twist& ctr);
  void A_starplan(void);
  void initallnavstate(void);
};

}
#endif // UARTODOMPTHREAD_H