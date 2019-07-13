#include "uartodompthread.h"
#include "uartodom.h"
#include "../common/map_process.h"

namespace zw
{

static NavPara m_navPara = {{0, 0, 0}, {0, 0, 0}, false, false};
static bool hasNewPose = false;
static int reachFinalCount = 0;
static bool reachFinal = false;
static float poseChangePow2=0;
static std::vector<std::vector<char>> maze;
static int map_height=0;
static int map_width=0;
static float map_resolution=0.0;
static float map_frame_originx=0.0;
static float map_frame_originy=0.0;
static bool rviznewgoal_flag=false;
static bool readmapfinish_flag=false;
static bool getPath = false;
static sensor_msgs::PointCloud aStartPath;
static std::vector<zw::CarPose> Gloabplanpoint;
static zw::CarPose rvizGoal{0,0,0};
static bool getlocalDest=true;
static bool isFinalDest=false;

inline void limit(float &v, float left, float right)
{
    assert(left < right);

    if (v > right)
        v = right;
    else if (v < left)
        v = left;
}

bool isValidLine(const std::vector<std::vector<char>> &maze, int x1, int y1, int x2, int y2)
{
    int dx = x2 - x1;
    int dy = y2 - y1;
    int ux = ((dx > 0) << 1) - 1; //x的增量方向，取或-1
    int uy = ((dy > 0) << 1) - 1; //y的增量方向，取或-1
    int x = x1, y = y1, eps;      //eps为累加误差

    eps = 0;
    dx = abs(dx);
    dy = abs(dy);
    if (dx > dy)
    {
        for (x = x1; x != x2; x += ux)
        {
            if (maze[x][y] == kOccGrid)
            {
                return false;
            }
            eps += dy;
            if ((eps << 1) >= dx)
            {
                y += uy;
                eps -= dx;
            }
        }
    }
    else
    {
        for (y = y1; y != y2; y += uy)
        {
            if (maze[x][y] == kOccGrid)
            {
                return false;
            }
            eps += dx;
            if ((eps << 1) >= dy)
            {
                x += ux;
                eps -= dy;
            }
        }
    }
    return true;
}

UartOdomPthread::UartOdomPthread()
{
    pthread_create(&id, NULL, MyPthread, (void *)this);
}

UartOdomPthread::~UartOdomPthread()
{
    pthread_detach(id);
}

void *UartOdomPthread::MyPthread(void *temp)
{
    UartOdomPthread *t = (UartOdomPthread *)temp;
    t->DoPthread();
}

void *UartOdomPthread::DoPthread(void)
{
    ros::NodeHandle n;

    {
        ros::NodeHandle nh("~");

        if (!nh.getParam("maxForwardSpeed", maxForwardSpeed))
            maxForwardSpeed = 0.2;
        if (!nh.getParam("maxBackSpeed", maxBackSpeed))
            maxBackSpeed = -0.2;
        if (!nh.getParam("maxOmega", maxOmega))
            maxOmega = 1.0;
        if (!nh.getParam("maxDisErr", maxDisErr))
            maxDisErr = 0.05;
        if (!nh.getParam("maxAngErr", maxAngErr))
            maxAngErr = 0.05;
        if (!nh.getParam("maxUpAcc", maxUpAcc))
            maxUpAcc = 0.5;
        if (!nh.getParam("maxBackAcc", maxBackAcc))
            maxBackAcc = -1;
        if (!nh.getParam("fabsdfh", fabsdfh))
            fabsdfh = 0.2;
        if (!nh.getParam("poseChange",  poseChange))
             poseChange = 0.1;

        poseChangePow2 =poseChange*poseChange;
    }

    ros::Subscriber key_sub = n.subscribe("cmd_vel", 2, cmd_keyCallback);
    ros::Subscriber pose_sub = n.subscribe<geometry_msgs::PoseWithCovarianceStamped>("amcl_pose", 2, PoseReceived);
    //ros::Subscriber map_sub = n.subscribe("maptwovalue", 2, MapReceived);   //by hulu 订阅二值化、膨胀之后的图像数据 maptwovalue
    //ros::Subscriber submap_sub = n.subscribe("subMap", 2, SubMapReceived); //计划用于局部路径规划，躲避动态障碍物
    //ros::Subscriber navigation_goal_sub = n.subscribe("/move_base_simple/goal", 2, goal_Callback);   //by hulu
    //ros::Subscriber gloab_Astarpath_sub = n.subscribe("gloab_Validpath", 2, Astarpath_Callback);    //订阅全局路径点导航
    ros::Subscriber local_Astarpath_sub = n.subscribe("local_Validpath", 2, Astarpath_Callback);    //订阅局部路径点导航

    ros::Publisher odom_pub = n.advertise<nav_msgs::Odometry>("odom", 100);
    ros::Publisher imu_pub = n.advertise<sensor_msgs::Imu>("imu", 100);
    ros::Publisher vel_pub = n.advertise<geometry_msgs::Twist>("cmd_vel", 2);
    ros::Publisher submap_path = n.advertise<sensor_msgs::PointCloud>("submap_path", 1, true);

    ros::Time starts = ros::Time::now();
    ros::Time ends = ros::Time::now();

    sensor_msgs::Imu imu;
    {
        float linear_acceleration_stddev = 0;
        float linear_velocity_stddev = 0;
        float orientation_stddev = 0;

        imu.linear_acceleration_covariance[0] = linear_acceleration_stddev;
        imu.linear_acceleration_covariance[4] = linear_acceleration_stddev;
        imu.linear_acceleration_covariance[8] = linear_acceleration_stddev;
        imu.angular_velocity_covariance[0] = linear_velocity_stddev;
        imu.angular_velocity_covariance[4] = linear_velocity_stddev;
        imu.angular_velocity_covariance[8] = linear_velocity_stddev;
        imu.orientation_covariance[0] = orientation_stddev;
        imu.orientation_covariance[4] = orientation_stddev;
        imu.orientation_covariance[8] = orientation_stddev;
    }

    double dt;
    double x = 0.0;
    double y = 0.0;
    double th = 0.0;

    double vx;
    double vy;
    double vth;
    double delta_x;
    double delta_y;
    double delta_th;
    tf::TransformBroadcaster odom_broadcaster;

    //ros::Timer timer=n.createTimer(ros::Duration(0.02),timerCallback,false);

    ros::Rate loop_rate(50);
    Paras m_para;
    while (ros::ok())
    {
        ros::spinOnce();
        ends = ros::Time::now();
        dt = (ends - starts).toSec();

        int32_t car_msg[6];

        ParaGetSet car_para = {R_REGISTER, 2, MSG_CONTROL, car_msg};
        m_para.GetAddressValue(car_para);
        zw::Float2Int32 mf;
        mf.i = car_msg[0];
        vx = (double)mf.f;
        vy = 0;
        //  mf.i=car_msg[1];
        //  vth = (double)mf.f;
        car_para = {R_REGISTER, 6, MSG_IMU, car_msg};
        m_para.GetAddressValue(car_para);
        vth = (double)car_msg[5] * Gyro_Gr; //gyr_z

        delta_th = vth * dt;
        th += delta_th;
        delta_x = (vx * cos(th) - vy * sin(th)) * dt;
        delta_y = (vx * sin(th) - vy * cos(th)) * dt;

        x += delta_x;
        y += delta_y;

        geometry_msgs::Quaternion odom_quat = tf::createQuaternionMsgFromYaw(th);

        geometry_msgs::TransformStamped odom_trans;
        {
            odom_trans.header.stamp = ends;
            odom_trans.header.frame_id = "odom";
            odom_trans.child_frame_id = "base_link";

            odom_trans.transform.translation.x = x;
            odom_trans.transform.translation.y = y;
            odom_trans.transform.translation.z = 0.0;
            odom_trans.transform.rotation = odom_quat;
            odom_broadcaster.sendTransform(odom_trans);

            nav_msgs::Odometry odom;
            odom.header.stamp = ends;
            odom.header.frame_id = "odom";

            odom.pose.pose.position.x = x;
            odom.pose.pose.position.y = y;
            odom.pose.pose.position.z = 0.0;
            odom.pose.pose.orientation = odom_quat;
            odom.child_frame_id = "base_link";
            odom.twist.twist.linear.x = vx;
            odom.twist.twist.linear.y = vy;
            odom.twist.twist.angular.z = vth;

            //publish odom
            odom_pub.publish(odom);
        }

        {
            float xf = car_msg[0] * Acc_Mss; //acc_x   m/s^2
            float yf = car_msg[1] * Acc_Mss;
            float zf = car_msg[2] * Acc_Mss;
            imu.linear_acceleration.x = xf;
            imu.linear_acceleration.y = yf;
            imu.linear_acceleration.z = zf;
            xf = car_msg[3] * Gyro_Gr; //gyr_x     rad/s
            yf = car_msg[4] * Gyro_Gr;
            zf = car_msg[5] * Gyro_Gr;
            imu.angular_velocity.x = xf;
            imu.angular_velocity.y = yf;
            imu.angular_velocity.z = zf;
            imu.header.stamp = ends;
            imu.header.frame_id = "imu_link";
            //publish imu
            imu_pub.publish(imu);
        }
        
        if (getPath)
        {
            getPath = false;
            aStartPath.header.frame_id = "map";
            aStartPath.header.stamp = ros::Time::now();
            submap_path.publish(aStartPath);
            aStartPath.points.clear();
        }

        getNavcmd();
       //if(m_navPara.startNav)
        {
            vel_pub.publish(vel);
        }

        starts = ends;
        loop_rate.sleep();
    }
}

void UartOdomPthread::cmd_keyCallback(const geometry_msgs::Twist::ConstPtr &cmd)
{
    Float2Int32 fi;
    int32_t dat[2];
    ParaGetSet packInfo = {R_REGISTER, 1, BTN_SWITCH, dat};
    Paras m_para;
    m_para.GetAddressValue(packInfo);
    if ((dat[0] & KEY_VEL_CTR) == KEY_VEL_CTR)
    {   m_navPara.startNav=false;
        m_navPara.emergeStop=false;
        m_navPara.newGoal = false;
        
        dat[0] &= (~KEY_START_NAV);
        dat[0] &= (~KEY_EME_STOP);
        dat[0] &= (~KEY_NEW_GOAL);
        packInfo.fuc = W_REGISTER;
        m_para.SetAddressValue(packInfo);
        aStartPath.points.clear();
        Gloabplanpoint.clear();
        return;
    }
    else 
    {   
        fi.f = cmd->linear.x;
        dat[0] = fi.i;
        fi.f = cmd->angular.z;
        dat[1] = fi.i;
        packInfo = {W_REGISTER, 2, CONTROL, dat};
        Paras m_para;
        m_para.SetAddressValue(packInfo);
       //ROS_INFO("[%6.2f,%6.2f]",cmd->linear.x,cmd->angular.z);
    }
    //  ROS_INFO("[%6.2f,%6.2f]",cmd->linear.x,cmd->angular.z);
}

void UartOdomPthread::PoseReceived(const geometry_msgs::PoseWithCovarianceStampedConstPtr pose)
{
    int32_t ctr_msg[3];
    zw::ParaGetSet car_para = {W_REGISTER, 3, (ParaAddress)(MSG_CONTROL + 2), ctr_msg};
    zw::Float2Int32 mf;
    m_navPara.current.x = mf.f = (float)pose->pose.pose.position.x;
    ctr_msg[0] = mf.i;
    m_navPara.current.y = mf.f = (float)pose->pose.pose.position.y;
    ctr_msg[1] = mf.i;
    m_navPara.current.h = mf.f = (float)(tf::getYaw(pose->pose.pose.orientation));
    ctr_msg[2] = mf.i;
    zw::Paras m_para;
    m_para.SetAddressValue(car_para);
    hasNewPose = true;
}

void UartOdomPthread::goal_Callback(const geometry_msgs::PoseStamped& msgPose)
{
   rvizGoal.x =(float) msgPose.pose.position.x;
   rvizGoal.y =(float) msgPose.pose.position.y;
   rvizGoal.h =(float)(tf::getYaw(msgPose.pose.orientation));
   ROS_INFO("rviz设定的目标点[%6.2f,%6.2f,%6.2f]",rvizGoal.x,rvizGoal.y,rvizGoal.h);
   rviznewgoal_flag=true;
}

void UartOdomPthread::MapReceived(const nav_msgs::OccupancyGridConstPtr &msg)
{
    readmapfinish_flag=true;

    map_height=msg->info.height;
    map_width=msg->info.width;
    map_resolution=msg->info.resolution;
    map_frame_originx=msg->info.origin.position.x;
    map_frame_originy=msg->info.origin.position.y;

    maze.clear();
    maze.resize(msg->info.height, std::vector<char>(msg->info.width, -1));  //用-1初始化容器值

    for(int i = 0 ; i < msg->info.height; i++)
      for(int j = 0; j < msg->info.width; j++)
       {
        maze[i][j] = msg->data[j + (msg->info.height-1-i) * msg->info.width]; //origin down-left
        //ROS_INFO("[ %d ]",maze[i][j]);
        //maze[i][j] = msg->data[j + i * msg->info.width];
       }
}

void UartOdomPthread::Astarpath_Callback(const sensor_msgs::PointCloudConstPtr &msgpoints)
{     
      Gloabplanpoint.clear();
      
      getlocalDest=true;
      isFinalDest=false;
      reachFinalCount = 0;
      reachFinal = false;   

      int pointsize=msgpoints->points.size();
      ROS_INFO("优化后的路径点个数%d",pointsize);

      for(int iflag=0; iflag < pointsize; iflag++)
      { 
          CarPose worldPose;
          worldPose.x = msgpoints->points[iflag].x;
          worldPose.y = msgpoints->points[iflag].y; //将当前路径点相对于子图中心的距离转换为相
          worldPose.h = msgpoints->points[iflag].z; //相对于map坐标的实际距离
          Gloabplanpoint.push_back(worldPose); //该点云结构体中存储的是规划好的相对于map坐标系的路径点坐标
          ROS_INFO("第%d路径点的:x为%6.2f,y为%6.2f,h为%6.2f",iflag,worldPose.x,worldPose.y,worldPose.h);
      }

      m_navPara.desired.x = msgpoints->points[pointsize-1].x;
      m_navPara.desired.y = msgpoints->points[pointsize-1].y;
      m_navPara.desired.h = msgpoints->points[pointsize-1].z;
      
      int Gloabpointsize=Gloabplanpoint.size();
      ROS_INFO("订阅到的路径点个数%d",Gloabpointsize);

      ROS_INFO("Gloabplanpoint容器已订阅到优化后的路径点消息");
}

void UartOdomPthread::getNavcmd(void)
{
    int32_t ctr_msg[3];
    zw::ParaGetSet car_para;
    zw::Float2Int32 mf;
    zw::Paras m_para;

    car_para = {R_REGISTER, 2, ADD_ERR, ctr_msg};
    m_para.GetAddressValue(car_para);
    maxDisErr = ctr_msg[0] / 1000.0;
    maxAngErr = ctr_msg[1] / 1000.0;
    
    if(Gloabplanpoint.size()<1)
   {
     car_para = {R_REGISTER, 3, (ParaAddress)(CONTROL + 2), ctr_msg};
     m_para.GetAddressValue(car_para);
     mf.i = ctr_msg[0];
     m_navPara.desired.x = mf.f;
     mf.i = ctr_msg[1];
     m_navPara.desired.y = mf.f;
     mf.i = ctr_msg[2];
     m_navPara.desired.h = mf.f;
    //ROS_INFO("Gloabplanpoint NULL!");
   }

    car_para.len = 1;
    car_para.addr = BTN_SWITCH;
    m_para.GetAddressValue(car_para);
    m_navPara.startNav = (bool)(ctr_msg[0] & KEY_START_NAV);
    m_navPara.emergeStop = (bool)(ctr_msg[0] & KEY_EME_STOP);
    m_navPara.newGoal = (bool)(ctr_msg[0] & KEY_NEW_GOAL);
   
    if(rviznewgoal_flag && readmapfinish_flag)
    {
       m_navPara.desired.x = rvizGoal.x;
       m_navPara.desired.y = rvizGoal.y;
       m_navPara.desired.h = rvizGoal.h;
       A_starplan();
       rviznewgoal_flag = false;
    }
 
    if (m_navPara.startNav)
    {
        static int timeout = 0;
        //ROS_INFO("startNav。。。。");
        if (m_navPara.newGoal || hasNewPose)
        {
            if (m_navPara.newGoal)
            {
                reachFinalCount = 0;
                reachFinal = false;
                isFinalDest=false;
                ROS_INFO("New destnation!");
            }
            
            CalNavCmdVel(&m_navPara, vel); // 仅当有新目标或者有新位置才重新计算控制指令          
  
            m_navPara.newGoal = false;
            ctr_msg[0] &= (~KEY_NEW_GOAL);
            car_para.fuc = W_REGISTER;
            m_para.SetAddressValue(car_para);
            timeout = 0;
           //hasNewPose=false;
        }
        else
        {
            // 判定超时
            timeout++;
            if (timeout >= 20)
            {
                // 20次循环都没有新数据则超时,且速度不为零(速度为零说明已到达目标点)
                timeout =0;
                if((int)(vel.linear.x*1000)!=0 && (int)(vel.angular.z*1000) !=0)
                {
                    vel.linear.x = 0;
                    vel.angular.z = 0;
                    ROS_ERROR("Position not updated, timeout!");
                }
            }
        }
    }
    else
    {
        // 停止导航时, 停车
        vel.linear.x = 0;
        vel.angular.z = 0;
    }

    if (m_navPara.emergeStop)
    {
        // 受到急停指令, 则停车
        vel.linear.x = 0;
        vel.angular.z = 0;
        ROS_INFO("Emerge Stop");

        ctr_msg[0] &= (~KEY_START_NAV);
        ctr_msg[0] &= (~KEY_EME_STOP);
        ctr_msg[0] &= (~KEY_NEW_GOAL);
        car_para.fuc = W_REGISTER;
        m_para.SetAddressValue(car_para);
        
        Gloabplanpoint.clear();
        aStartPath.points.clear();
    }
    //  ROS_INFO("%d    %d",m_navPara.startNav ,m_navPara.emergeStop );
}

// 导航, 计算控制指令, 速度, 角速度
// 输入: const NavPara * 导航指令
//       geometry_msgs::Twist & 控制指令
void UartOdomPthread::CalNavCmdVel(const NavPara *nav, geometry_msgs::Twist &ctr)
{
    // 读取上位机发送的PID参数
    int32_t pid[6] = {20, 5, 0, 80, 5, 5};
    zw::ParaGetSet para = {R_REGISTER, 6, (ParaAddress)(ADD_PID + 6), pid};
    zw::Paras m_para;
    m_para.GetAddressValue(para);

    static uint8_t pricnt = 0;
    const uint8_t PCT = 5;
    pricnt++;

    // 计算本次期望位姿
    static CarPose nextDest={0,0,0};
    static uint32_t  Gloabplanpoint_index=1;
    if(getlocalDest && isFinalDest==false)
    {
     if (Gloabplanpoint_index<Gloabplanpoint.size())
     {
        // 若存在期望路径, 则设定期望位姿为下一个路径点
        nextDest.x = Gloabplanpoint[Gloabplanpoint_index-1].x;
        nextDest.y = Gloabplanpoint[Gloabplanpoint_index-1].y;
        isFinalDest = false;
        getlocalDest = false;
        Gloabplanpoint_index++;       

       //if (pricnt % (3 * PCT) == 1)
            ROS_INFO("nextDest=[%6.3f,%6.3f,%6.3f]", nextDest.x, nextDest.y,nextDest.h);
     }else{
        // 否则, 直接将最终目标作为期望位姿

        // 此处需要加防碰撞处理，即A*算法没有求出路径
        Gloabplanpoint_index=1;
        nextDest = nav->desired;
        getlocalDest = false;
        isFinalDest = true;
        ROS_INFO("FinalDest=[%6.3f,%6.3f,%6.3f]", nextDest.x, nextDest.y,nextDest.h);
        Gloabplanpoint.clear();
     }
   }

    // 求位置闭环控制量, 期望速度, 期望角速度
    float dx, dy, dph, dfh, ds;
    dx = nextDest.x - nav->current.x;
    dy = nextDest.y - nav->current.y;
    ds = sqrt(dx * dx + dy * dy);

    dph = angle_diff(nextDest.h, nav->current.h);
    dfh = angle_diff(atan2(dy, dx), nav->current.h);

    if (fabs(dfh) > M_PI / 2)
    {
        if (dfh > M_PI / 2)
            dfh = dfh - M_PI;
        else
            dfh = dfh + M_PI;
        ds = -ds;
    }

    assert(dfh <= M_PI && dfh >= -M_PI);

    static float last_ds = 0, ids = 0;
    static float last_vx = 0, last_az = 0;
    float dds = ds - last_ds;

    ids += ds;
    last_ds = ds;
    limit(ids, -2, 2);
    limit(dds, -0.5, 0.5);
    float vx = (ds * pid[0] + ids * pid[1] + dds * pid[2]) / 100.0;
    limit(vx, maxBackSpeed, maxForwardSpeed);

    static float ldfh = 0, idfh = 0;
    float ddfh = dfh - ldfh;
    idfh += dfh;
    ldfh = dfh;
    limit(idfh, -2, 2);
    limit(ddfh, -1, 1);
    float afz = (dfh * pid[3] + idfh * pid[4] + ddfh * pid[5]) / 100.0;
    limit(afz, -maxOmega, maxOmega);

    static float ldph = 0, idph = 0;
    float ddph = dph - ldph;
    idph += dph;
    ldph = dph;
    limit(idph, -2, 2);
    limit(ddph, -1, 1);
    float apz = (dph * pid[3] + idph * pid[4] + ddph * pid[5]) / 100.0;
    limit(apz, -maxOmega, maxOmega);

    // 导航逻辑
    float az = 0;

    if (!reachFinal)
    {
        // 未停在目标位置
        if (fabs(ds) > maxDisErr)
        {
            // 若未到达期望位置
            if (fabs(dfh) > fabsdfh)
            {
                // 若航向错误, 则转向
                idph = ldph = 0;
                ids = last_ds = 0;
                vx = 0;
                az = afz;
            }else{
                // 航向正确, 则行进
                idph = ldph = 0;
                az = afz;
            }
            reachFinalCount = 0;
        }
        else
        {
            // 若到达最终期望位置, 等待平稳停车
            if(isFinalDest)
            {
                reachFinalCount++;
                if (reachFinalCount > 3)
                {
                    reachFinal = true;
                    ROS_INFO("Reached position!\n"
                            "dis_err=%6.3f/%6.3f ang_err=%6.3f/%6.3f",
                            ds, maxDisErr, dph, maxAngErr);
                }
                vx = 0;
                az = 0;
            }
           getlocalDest=true;
           ROS_INFO("到达目标点[%6.3f,%6.3f]", nextDest.x, nextDest.y);
        }
    }
    else
    {
        // 已停止在最终目标位置
        if (fabs(dph) > maxAngErr)
        {
            // 若未到达期望角度, 则转向
            idfh = ldfh = 0;
            ids = last_ds = 0;
            vx = 0;
            az = apz;
        }else{
            // 到达期望角度
            vx = az = 0;
            ctr.linear.x = 0;
            ctr.angular.z = 0;
            last_ds = ids = 0;
            ldfh = idfh = 0;
            ldph = idph = 0;
            last_vx = last_az = 0;
            if (pricnt % (3 * PCT) == 1)
            ROS_INFO("Reached angle!\n"
                     "dis_err=%6.3f/%6.3f ang_err=%6.3f/%6.3f",
                    ds, maxDisErr, dph, maxAngErr);
            //到达最终目标点初始化所有变量为下次导航做准备
           // initallnavstate();
         
        }
    }

    // 限制最大加速度
    float vdv = vx - last_vx;
    if (vdv > 0)
    {
        if (vdv > (maxUpAcc * 0.02))
            vx = last_vx + maxUpAcc * 0.02;
    }
    else if (vdv < 0)
    {
        if (vdv < (maxBackAcc)*0.02)
            vx = last_vx + maxBackAcc * 0.02;
    }

    limit(vx, maxBackSpeed, maxForwardSpeed);
    limit(az, -maxOmega, maxOmega);

    last_vx = vx;
    last_az = az;

    ctr.linear.x = vx;
    ctr.angular.z = az;
 //   ROS_INFO("%f,%f",vx,az);
}

void UartOdomPthread::initallnavstate(void)
{
    int32_t ctr_msg[3];
    zw::ParaGetSet car_para;
    zw::Paras m_para;

    car_para = {W_REGISTER, 1, BTN_SWITCH, ctr_msg};
    ctr_msg[0] &= (~KEY_START_NAV);
    ctr_msg[0] &= (~KEY_EME_STOP);
    ctr_msg[0] &= (~KEY_NEW_GOAL);
    m_para.SetAddressValue(car_para);

    Gloabplanpoint.clear();
    aStartPath.points.clear();
    
    getlocalDest=true;
    isFinalDest=false;
    reachFinalCount = 0;
    reachFinal = false;

    m_navPara.startNav=false;
    m_navPara.emergeStop=false;
    m_navPara.newGoal = false;
}

void UartOdomPthread::SubMapReceived(const nav_msgs::OccupancyGridConstPtr &msg)
{
    static CarPose lastCarPose={0,0,0};
    float poseChangePow = (lastCarPose.x-m_navPara.current.x)*(lastCarPose.x-m_navPara.current.x) +
                          (lastCarPose.y-m_navPara.current.y)*(lastCarPose.y-m_navPara.current.y);

    lastCarPose = m_navPara.current ;

    if(poseChangePow < poseChangePow2)
        return ;

    float submap_fx = m_navPara.desired.x - m_navPara.current.x;
    float submap_fy = m_navPara.desired.y - m_navPara.current.y;

    float boundx = (msg->info.width - 1) * msg->info.resolution * 0.5;
    float boundy = (msg->info.height - 1) * msg->info.resolution * 0.5;
    float fk = (boundy / boundx);

    zw::CarPose tempGoal{submap_fx, submap_fy, m_navPara.desired.h};

    if ((fabs(submap_fx) > boundx) || (fabs(submap_fy) > boundy))
    {
        if (submap_fx == 0)
        {
            tempGoal.x = 0;
            tempGoal.y = ((submap_fy >= 0) ? boundy : -boundy);
        }
        else if (submap_fy == 0)
        {
            tempGoal.x = ((submap_fx >= 0) ? boundx : -boundx);
            tempGoal.y = 0;
        }
        else
        {
            float k = submap_fy / submap_fx;
            if (fabs(k) <= fk)
            {
                tempGoal.x = ((submap_fx >= 0) ? boundx : -boundx);
                tempGoal.y = k * tempGoal.x;
            }
            else
            {
                tempGoal.y = ((submap_fy >= 0) ? boundy : -boundy);
                tempGoal.x = tempGoal.y / k;
            }
        }
    }
    int mi = floor(tempGoal.x / msg->info.resolution + 0.5) + msg->info.width / 2;
    int mj = floor(tempGoal.y / msg->info.resolution + 0.5) + msg->info.height / 2;

    int ox = msg->info.width / 2;
    int oy = msg->info.height / 2;

    zw::Point start(ox, oy);
    zw::Point end(mi, mj);

    aStartPath.points.clear();

    std::vector<std::vector<char>> maze;
    maze.clear();
    maze.resize(msg->info.height, std::vector<char>(msg->info.width, -1));

    for (int i = 0; i < msg->info.height; i++)
        for (int j = 0; j < msg->info.width; j++)
        {
            maze[j][i] = msg->data[j + i * msg->info.width]; //origin down-left
        }
   // bool getPathflag = true;
    if (start.x == end.x && start.y == end.y)
        return;
    if (maze[end.x][end.y] == zw::kOccGrid)
    {
        // if(fabs(end.x ==)

        ROS_INFO("can not reach");
        return;
    }

    if ( (end.x >= 0) && (end.x < msg->info.width) && (end.y >= 0) && (end.y < msg->info.height))
    {
        zw::Astart astar;
        astar.InitAstart(maze);

        std::list<zw::Point *> path = astar.GetPath(start, end, true);
        int pathSize = path.size();

        if(pathSize==0)
        {
            ROS_INFO("can not find a path");
            return ;
        }

        std::vector<zw::Point *> vpath;

        for (auto &p : path)
        {
            vpath.push_back(p);
        }

        int iflag = pathSize - 1;
        for (; iflag > 0; iflag--)
        {
            if (isValidLine(maze, vpath[iflag]->x, vpath[iflag]->y, vpath[0]->x, vpath[0]->y))
            {
                break;
            }
        }

        for (; iflag < pathSize; iflag++)
        {
            geometry_msgs::Point32 pa;
            pa.x = (vpath[iflag]->x - ox) * msg->info.resolution + m_navPara.current.x;
            pa.y = (vpath[iflag]->y - oy) * msg->info.resolution + m_navPara.current.y;
            pa.z = 0;
            aStartPath.points.push_back(pa);
        }
        getPath = true;
    }
    else
    {
        ROS_ERROR("tempGoal out of bound");
    }
}

void UartOdomPthread:: A_starplan(void)
{
    //将世界坐标系距离转换为地图中的像素点位置
    int rows_desired_x= (int)( map_height - (m_navPara.desired.y -  map_frame_originy)/map_resolution );
    int cols_desired_y= (int)((m_navPara.desired.x - map_frame_originx)/map_resolution );
    //int cols_desired_y= (int)( map_width-(m_navPara.desired.x - map_frame_originx)/map_resolution );    

    int rows_current_x=(int)( map_height - (m_navPara.current.y -  map_frame_originy)/map_resolution );
    int cols_current_y=(int)((m_navPara.current.x - map_frame_originx)/map_resolution );
    //int cols_current_y=(int)(map_width-(m_navPara.current.x - map_frame_originx)/map_resolution );    

    if( rows_desired_x<0 || rows_desired_x >= map_height || cols_desired_y< 0 || cols_desired_y >= map_width)
    {
       ROS_INFO("目标点超出范围can not reach");
       return;
    }
    zw::Point start(rows_current_x, cols_current_y);
    zw::Point end(rows_desired_x, cols_desired_y);
    ROS_INFO("current_x=%6.2f,current_y=%6.2f",m_navPara.current.x,m_navPara.current.y);
    ROS_INFO("rows_current_x=%d,cols_current_y=%d",rows_current_x,cols_current_y);
    ROS_INFO("rows_desired_x=%d,cols_desired_y=%d",rows_desired_x,cols_desired_y);

    aStartPath.points.clear();

   // bool getPathflag = true;
    if (start.x == end.x && start.y == end.y)
    {
        ROS_INFO("当前点与目标点重合，路径规划失败");
        return;
    }
    if (maze[end.x][end.y] == zw::kOccGrid)
    {
        ROS_INFO("目标点是障碍物can not reach");
        return;
    }

    if ( (end.x >= 0) && (end.x < map_height) && (end.y >= 0) && (end.y < map_width))
    {
        zw::Astart astar;
        astar.InitAstart(maze);

        std::list<zw::Point *> path = astar.GetPath(start, end, true);  //目标点在队尾
        int pathSize = path.size();
        ROS_INFO("路径点个数%d",pathSize);

        if(pathSize==0)
        {
            ROS_INFO("can not find a path");
            return ;
        }

        std::vector<zw::Point *> vpath;

        for (auto &p : path)
        {
            vpath.push_back(p);
        }
        
        int iflag = pathSize - 1;
        for (; iflag >=0; iflag--)
        {
            geometry_msgs::Point32 pa;
            pa.x = (vpath[iflag]->y) * map_resolution + map_frame_originx;
            pa.y = (map_height-vpath[iflag]->x) * map_resolution + map_frame_originy; //将当前路径点相对于子图中心的距离转换为相>对于map坐标的实际距离
            pa.z = 0;
            aStartPath.points.push_back(pa); //该点云结构体中存储的是规划好的相对于map坐标系的路径点坐标
           //int size=aStartPath.points.size();
           //ROS_INFO("第%d路径点的x为%6.2f",iflag,pa.x);
           //ROS_INFO("第%d路径点的y为%6.2f",iflag,pa.y);
           //ROS_INFO("aStartPath路径点个数%d",size);
        }
       
       Gloabplanpoint.clear();            
       iflag = pathSize - 1;
       int Validnewstartindex=0; 
       for(;;)
       {
         for (; iflag > Validnewstartindex; iflag--) //从目标点往回遍历规划好的路径点，若当前路径点与起始点之间的连线没有障碍物，则返回true跳出循环，
         {                          //将当前路径点之前的路径点截断,保留当前点到目标点的路径点
            if (isValidLine(maze, vpath[iflag]->x, vpath[iflag]->y, vpath[Validnewstartindex]->x, vpath[Validnewstartindex]->y))  //有效路径
            {
                break;
            }
         }
         if(iflag!=Validnewstartindex)
         {
            CarPose worldPose;
            worldPose.x = (vpath[iflag]->y) * map_resolution + map_frame_originx;
            worldPose.y = (map_height-vpath[iflag]->x) * map_resolution + map_frame_originy; //将当前路径点相对于子图中心的距离转换为相
            worldPose.h = 0;                                                                 //相对于map坐标的实际距离
            Gloabplanpoint.push_back(worldPose); //该点云结构体中存储的是规划好的相对于map坐标系的路径点坐标
            ROS_INFO("第%d路径点的:x为%6.2f,y为%6.2f",iflag,worldPose.x,worldPose.y);           
         }            
         else
            break;
            
         if(iflag!=pathSize-1)
         {
            Validnewstartindex=iflag;
            iflag=pathSize - 1;
            /*if(i==6-1)
            {
              worldPose.x = (vpath[iflag]->y) * map_resolution + map_frame_originx;
              worldPose.y = (map_height-vpath[iflag]->x) * map_resolution + map_frame_originy; //将当前路径点相对于子图中心的距离转换为相
              worldPose.h = 0;                                                                 //相对于map坐标的实际距离
              Gloabplanpoint.push_back(worldPose); //该点云结构体中存储的是规划好的相对于map坐标系的路径点坐标
              ROS_INFO("第%d路径点的:x为%6.2f,y为%6.2f",iflag,worldPose.x,worldPose.y);
            }*/
          }
          else 
              break;
       }

       if(iflag==Validnewstartindex)
       {
         iflag=iflag+1;
         for(; iflag < pathSize; iflag++)
         {
            CarPose worldPose;
            worldPose.x = (vpath[iflag]->y) * map_resolution + map_frame_originx;
            worldPose.y = (map_height-vpath[iflag]->x) * map_resolution + map_frame_originy; //将当前路径点相对于子图中心的距离转换为相
            worldPose.h = 0;                                                                 //相对于map坐标的实际距离
            Gloabplanpoint.push_back(worldPose); //该点云结构体中存储的是规划好的相对于map坐标系的路径点坐标
            ROS_INFO("第%d路径点的:x为%6.2f,y为%6.2f",iflag,worldPose.x,worldPose.y);
         }
       }

       //Gloabplanpoint.clear(); 
       //Gloabplanpoint.resize(pathSize-iflag);   
 
       /* for (; iflag < pathSize; iflag++)
        {
            CarPose worldPose;
            worldPose.x = (map_width-vpath[iflag]->y) * map_resolution + map_frame_originx;
            worldPose.y = (map_height-vpath[iflag]->x) * map_resolution + map_frame_originy; //将当前路径点相对于子图中心的距离转换为相
            worldPose.h = 0;                                                                 //相对于map坐标的实际距离
            Gloabplanpoint.push_back(worldPose); //该点云结构体中存储的是规划好的相对于map坐标系的路径点坐标
            ROS_INFO("第%d路径点的:x为%6.2f,y为%6.2f",iflag,worldPose.x,worldPose.y);
        }*/
        int size=Gloabplanpoint.size();
        ROS_INFO("优化后的路径点个数%d",size);
        getPath = true;
        
      //path.clear();
       //vpath.clear();
    }
    else
    {
        ROS_ERROR("goal out of bound");
    }
}
}