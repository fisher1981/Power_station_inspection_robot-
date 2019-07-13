#include "scan_processor.h"
#include <ros/ros.h>
#include <tf/tf.h>
#include "../../map/mapreadandwrite.h"
#include <fcntl.h>
#include <tf/transform_broadcaster.h>


namespace zw{

//be sure a,b <[-M_PI,M_PI]
 static float  angle_diff(float a,float b)
 {
     assert(fabs(a)<=M_PI);
     assert(fabs(b)<=M_PI);

     float d1, d2;
     d1 = a-b;
     d2 = 2*M_PI - fabs(d1);
     if(d1 > 0)
       d2 *= -1.0;
     if(fabs(d1) < fabs(d2))
       return(d1);
     else
       return(d2);
 }

ScanProcessor::ScanProcessor()
{
    poseDiff=0.025;
    angleDiff=0.05;
    numDepth = 2;
    publishScan =false;
    writePose =false;
    maxIterations =6;
    multMap =new map_grid_t[numDepth];

    getSubmap =false;
    subMap.header.frame_id="subMap";
  //  subMap.header.stamp=ros::Time::now();
    subMap.info.resolution=0.1;
    subMap.info.width = 61;
    subMap.info.height= 61;
    subMap.info.origin.position.x=0;
    subMap.info.origin.position.y=0;
    subMap.info.origin.position.z=0;
  //  subMap.data.resize(subMap.info.origin.position.x *subMap.info.origin.position.y,0);
//    tf::Quaternion q;
//    q.setRPY(0,0,subMap.info.origin.position.z);
//    subMap.info.origin.orientation.x=q.x();
//    subMap.info.origin.orientation.y=q.y();
//    subMap.info.origin.orientation.z=q.z();
//    subMap.info.origin.orientation.w=q.w();
//    subMap.info.origin.orientation(q);

    for(int i=0; i<numDepth;i++)
        multMap[i].cell_pbb = nullptr;
}

ScanProcessor::~ScanProcessor()
{
    for(int i=0; i<numDepth;i++)
    if(multMap[i].cell_pbb!=nullptr)
        delete multMap[i].cell_pbb;

    delete multMap;
}


void ScanProcessor::GetSubMap(float factor ,const Eigen::Vector3f& finalPose)
{
   DataContainer d(dataContainer.getSize());
   d.setFrom(dataContainer,factor);


   Eigen::Vector3f lpw(laser_pose.v[0],      //laser pose in world
                       laser_pose.v[1],
                       finalPose[2]);

   Eigen::Affine2f transform(getTransformForState(lpw));  //laser pose in world transform

   const int dsize =d.getSize();
   const int w=subMap.info.width;
   const int h=subMap.info.height;
   int msize = w*h;

   int mi,mj,index;

   subMap.header.stamp=ros::Time::now();
   subMap.data.clear();
   subMap.data.resize(msize,kUnknownGrid);

   std::vector<char> tmp1 ,tmp2;
   tmp1.clear();
   tmp2.clear();
   tmp1.resize(msize,kUnknownGrid);
   tmp2.resize(msize,kUnknownGrid);

   for (int i = 0; i < dsize; i++)
   {
       const Eigen::Vector2f& currPoint(d.getVecEntry(i)*subMap.info.resolution);  //end point in laser pose
       // end point in map pose
       const Eigen::Vector2f endPoint =transform * currPoint;  //end point in world

       mi=floor(endPoint[0] / subMap.info.resolution + 0.5) + w/2 ;
       mj=floor(endPoint[1] / subMap.info.resolution + 0.5) + h/2 ;

       if(mi>=0 && mi<w && mj>=0 && mj<h)
       {
           index = mi + mj*w;
           tmp1[index] ++;
       }
   }

   for(int i=0; i<msize ;i++)
   {
       if(tmp1[i]>0)
           tmp1[i]=kOccGrid;
       else
           tmp1[i]=kUnknownGrid;
   }

#if 1
   int dis=0 ;
   for(int i=0; i<msize ;i++)
   {
       if(tmp1[i]==kOccGrid)
       {
           for(int y= -submapExpandCnt; y<=submapExpandCnt; y++)
           {
               for(int x= -submapExpandCnt; x<=submapExpandCnt; x++)
               {
                   index = i+x+y*w;
                   if(index>=0 && index<msize)
                   {
                        dis = floor(sqrt(x*x +y*y)+0.5);
                        if(dis < submapExpandCnt)
                            tmp2[index] = kOccGrid;
                   }
               }
           }
       }
   }

#else

   for(int k=0;k<submapExpandCnt;k++)
   {
       for(int i=0; i<msize ;i++)
       {

           if(tmp1[i]==kOccGrid)
           {
               tmp2[i]=kOccGrid;
#if 1   //8
               index =i-1;
               if(index >=0)
                   tmp2[index]=kOccGrid;

               index -=w;
               if(index >=0)
                   tmp2[index]=kOccGrid;

               index +=1;
               if(index >=0)
                   tmp2[index]=kOccGrid;

               index +=1;
               if(index >=0)
                   tmp2[index]=kOccGrid;

               index =i+1;
               if(index < msize)
                   tmp2[index]=kOccGrid;

               index +=w;
               if(index < msize)
                   tmp2[index]=kOccGrid;

               index -=1;
               if(index < msize)
                   tmp2[index]=kOccGrid;
               index -=1;
               if(index <msize)
                   tmp2[index]=kOccGrid;
#else    //4
               index =i-1;
               if(index >=0)
                   tmp2[index]=kOccGrid;

               index =i-w ;
               if(index >=0)
                   tmp2[index]=kOccGrid;

               index =i+1;
               if(index <msize)
                   tmp2[index]=kOccGrid;

               index =i+w ;
               if(index <msize)
                   tmp2[index]=kOccGrid;
#endif
           }
       }
       if(submapExpandCnt==k+1)
           break ;
       for(int i=0; i<msize ;i++)
           tmp1[i] =tmp2[i];
   }

#endif

   for(int i=0; i<msize ;i++)
   {
      subMap.data[i]=tmp2[i];
   }

    geometry_msgs :: TransformStamped subMap_trans;
    tf:: TransformBroadcaster subMap_broadcaster;
    geometry_msgs :: Quaternion submap_quat =tf::createQuaternionMsgFromYaw(0);

    subMap_trans.header.stamp = ros::Time::now();
    subMap_trans.header.frame_id = "map";
    subMap_trans.child_frame_id ="subMap";

    subMap_trans.transform.translation.x =finalPose[0] - (w+0.5)*subMap.info.resolution/2.0;
    subMap_trans.transform.translation.y =finalPose[1] - (h+0.5)*subMap.info.resolution/2.0;
    subMap_trans.transform.translation.z =0.0;
    subMap_trans.transform.rotation =submap_quat;
    subMap_broadcaster.sendTransform(subMap_trans);
}

bool ScanProcessor::PoseUpdate(const sensor_msgs::LaserScanConstPtr& scan,
                               const map_t* map,
                               Eigen::Vector3f& finalPose)
{
    LaserScanToDataContainer(scan,1/multMap[0].scale);

    const Eigen::Vector3f AmclPoseHintWorld = finalPose;
    Eigen::Vector3f tmp(AmclPoseHintWorld);

    bool flag =true;

    for(int index = numDepth-1; index>=0; --index)
    {
        if(index==0)
        {
            tmp =matchData(tmp,dataContainer,&(multMap[index]),lastScanMatchCov,maxIterations, flag);
        }else{
            DataContainer d(dataContainer.getSize());
            d.setFrom(dataContainer,1.0/(1<<index));
            tmp = matchData(tmp,d,&(multMap[index]),lastScanMatchCov,2,flag);
        }
        if(!flag)
        {
            tmp = AmclPoseHintWorld;
            break;
        }
    }

    Eigen::Vector3f scanmatch=tmp;
    bool sflag=true;

    float pd = sqrt((tmp[0] -AmclPoseHintWorld[0])*(tmp[0] -AmclPoseHintWorld[0])+
              (tmp[1] -AmclPoseHintWorld[1])*(tmp[1] -AmclPoseHintWorld[1]));
    float pa = angle_diff(tmp[2] , AmclPoseHintWorld[2]);
    pa =fabs(pa);

    if( (pd < poseDiff) &&
        (pa < angleDiff) && false)
    {
        uniformPoseGenerator(scanmatch, map, 0.025, 0.02, 100);

        for(int i=0;i<poseSets.size();i++)
            poseSets[i].grade = getPoseSetGrade(dataContainer,map,poseSets[i],1);

        Eigen::Vector3f best,avg(0.0,0.0,0.0);

        best = getBestSet();

      #if 0

        for(int i=0;i<10;i++)
        {
            ROS_INFO("rp:[%6.3f %6.3f %6.3f],g=%6.6f",
                     poseSets[i].pose[0],poseSets[i].pose[1],
                     poseSets[i].pose[2],poseSets[i].grade);
            avg[0] +=poseSets[i].pose[0] ;
            avg[1] +=poseSets[i].pose[1] ;
            avg[2] +=fabs(poseSets[i].pose[2]);
        }
        avg /=10;
        if(best[2]<0)
            avg[2]=-avg[2];

        ROS_INFO("\na:[%6.3f %6.3f %6.3f]\n"
                   "s:[%6.3f %6.3f %6.3f]\n"
                   "b:[%6.3f %6.3f %6.3f],g=%6.3f/%d\n"
                   "v:[%6.3f %6.3f %6.3f]\n",
                   AmclPoseHintWorld[0],AmclPoseHintWorld[1],AmclPoseHintWorld[2],
                   scanmatch[0],scanmatch[1],scanmatch[2],
                   best[0],best[1],best[2],poseSets[0].grade,dataContainer.getSize(),
                   avg[0],avg[1],avg[2]);

      #endif

        ROS_INFO("\na:[%6.3f %6.3f %6.3f]\n"
                   "s:[%6.3f %6.3f %6.3f]\n"
                   "b:[%6.3f %6.3f %6.3f],g=%6.3f/%d\n",
                   AmclPoseHintWorld[0],AmclPoseHintWorld[1],AmclPoseHintWorld[2],
                   scanmatch[0],scanmatch[1],scanmatch[2],
                   best[0],best[1],best[2],poseSets[0].grade,dataContainer.getSize());
          finalPose = best;
      //  return best;
    }else{
        ROS_INFO("\na:[%6.3f %6.3f %6.3f]\n"
                   "s:[%6.3f %6.3f %6.3f]",
                   AmclPoseHintWorld[0],AmclPoseHintWorld[1],AmclPoseHintWorld[2],
                   scanmatch[0],scanmatch[1],scanmatch[2]);

        poseSet_t tset[2]={{ AmclPoseHintWorld,0},{scanmatch,0}};

        tset[0].grade =  getPoseSetGrade(dataContainer,map, tset[0],1);
        tset[1].grade =  getPoseSetGrade(dataContainer,map, tset[1],1);

        if(!flag){
                  finalPose = AmclPoseHintWorld ;
                  sflag =false;
                  ROS_INFO("scan match failed!\nuse amcl");
        }else if(tset[0].grade>tset[1].grade){
                  finalPose = AmclPoseHintWorld ;
                  sflag =false;
                  ROS_INFO("\nag=%f,sg=%f\nuse amcl",tset[0].grade,tset[1].grade);
        }else if((pd >0.2)||(pa>0.5)){
            if((tset[1].grade>0.7) && (tset[0].grade<tset[1].grade))
            {
                finalPose = scanmatch ;
                sflag =true;
                ROS_INFO("\nscan match change too large!\n"
                         "ag=%f,sg=%f\nuse scan",tset[0].grade,tset[1].grade);
            }else{
                finalPose = AmclPoseHintWorld ;
                sflag =false;
                ROS_INFO("\nscan match change too large!\n"
                         "ag=%f,sg=%f\nuse amcl",tset[0].grade,tset[1].grade);
            }
        }else{
            finalPose = scanmatch ;
            sflag =true;
            ROS_INFO("\nag=%f,sg=%f\nuse scan",tset[0].grade,tset[1].grade);
        }
    }

    if(writePose)
    {
       static int i=0;
    //   writePoseToTxt("../mpt.txt", AmclPoseHintWorld, scanmatch , i);
       writePoseToTxt("../mpt.txt", AmclPoseHintWorld, finalPose, i);
    }
    static int fcnt=submapFilter-1;
    fcnt++;
    if(fcnt % submapFilter ==0){
       fcnt=0;
       GetSubMap(multMap[0].scale/subMap.info.resolution, finalPose);
       getSubmap = true ;
    }

    return sflag;
}

/*
arg1 : t-1时刻机器人在世界坐标系下的位姿
arg2 :  栅格化的激光数据
arg3 : 栅格地图
arg4 : 当前时刻 hassian  矩阵
arg5 : 最大迭代次数
ret : t时刻机器人在世界坐标系下的位姿
*/
Eigen::Vector3f ScanProcessor::matchData(const Eigen::Vector3f& beginEstimateWorld,
                                         const DataContainer& dataPoints,
                                         const map_grid_t* map,
                                         Eigen::Matrix3f& covMatrix,
                                         int maxIterations,
                                         bool & flag )
{    
    if (dataPoints.getSize() != 0) {
        //计算激光坐标系在全局坐标中的坐标
        // Take account of the laser pose relative to the robot
         pf_vector_t pose={beginEstimateWorld[0],beginEstimateWorld[1],beginEstimateWorld[2]};  //robot in world pose
         pose = pf_vector_add(pose,laser_pose);    //laser in world pose

      //   std::cout<<"lwp: [ "<<pose.v[0]<<" "<<pose.v[1]<<" "<<pose.v[2]<<" ]"<<"\n";

         int mi,mj;
         mi=MAP_GXWX(map, pose.v[0]);
         mj=MAP_GYWY(map, pose.v[1]);
         if(!MAP_VALID(map, mi, mj)){
             ROS_ERROR("scan match origin pose not in scale");
             flag= false;
             return beginEstimateWorld;
         }
         Eigen::Vector3f estimate(mi,mj,pose.v[2]);  //laser in map pose

      //   std::cout<<"lmp: [ "<<estimate[0]<<" "<<estimate[1]<<" "<<estimate[2]<<" ]"<<"\n";
         Eigen::Vector3f temp;
         int i=0;
         for(i=0;i<maxIterations+1;i++)
         {
             temp=estimate;
             estimateTransformationLogLh(estimate,dataPoints,map);
             if((fabs(estimate[0]-temp[0])<0.05)&&
                (fabs(estimate[1]-temp[1])<0.05)&&
                (fabs(estimate[2]-temp[2])<0.01))
             {
                 std::cout<<"Iterations="<< i<<"\n";
                 break ;
             }
         }
        // ROS_INFO("Iteration =%d",i);

        //normalize angle
         float angle = fmod(fmod(estimate[2], 2.0f*M_PI) + 2.0f*M_PI, 2.0f*M_PI);
         if(angle>M_PI)
             angle -= 2.0f*M_PI;
         estimate[2]=angle;

         covMatrix = Eigen::Matrix3f::Zero();
         covMatrix = H;

       //  std::cout<<"elmp: [ "<<estimate[0]<<" "<<estimate[1]<<" "<<estimate[2]<<" ]"<<"\n";
         //laser in world pose
         pose={MAP_WXGX(map,estimate[0]),MAP_WYGY(map,estimate[1]),estimate[2]};
     //    std::cout<<"elwp: [ "<<pose.v[0]<<" "<<pose.v[1]<<" "<<pose.v[2]<<" ]"<<"\n";
    //     std::cout<<"map origin = ["<<map->origin_x<<" "<<map->origin_y<<" ]"<<"\n";

         pose= pf_vector_sub(pose,laser_pose);  //robot in world pose

    //     std::cout<<"rwp: [ "<<pose.v[0]<<" "<<pose.v[1]<<" "<<pose.v[2]<<" ]"<<"\n";

         estimate[0]=pose.v[0];
         estimate[1]=pose.v[1];
         estimate[2]=pose.v[2];
         return estimate;
    }
    return beginEstimateWorld;
}


/*计算 hessian 矩阵 ，并估计t时刻，机器人的位姿*/
bool ScanProcessor::estimateTransformationLogLh(Eigen::Vector3f& estimate,
                                 const DataContainer& dataPoints,
                                 const map_grid_t* gridMap)
{
    getCompleteHessianDerivs(estimate, dataPoints,gridMap, H, dTr);

//    std::cout <<"size = "<<dataPoints.getSize()<<" N = "<<n<<"\n";
//    std::cout << "\nH\n" << H  << "\n";
//    std::cout << "\ndTr\n" << dTr  << "\n";

    if ((H(0, 0) != 0.0f) && (H(1, 1) != 0.0f)) {

      //H += Eigen::Matrix3f::Identity() * 1.0f;
      Eigen::Vector3f searchDir (H.inverse() * dTr);

    //  std::cout << "\nsearchdir\n" << searchDir  << "\n";
      if (searchDir[2] > 0.2f) {
        searchDir[2] = 0.2f;
        ROS_INFO("SearchDir angle change too large\n");;
      } else if (searchDir[2] < -0.2f) {
        searchDir[2] = -0.2f;
        ROS_INFO("SearchDir angle change too large\n");;
      }
      estimate += searchDir;
      return true;
    }
    return false;
}

void ScanProcessor::getCompleteHessianDerivs(const Eigen::Vector3f& pose,
                                             const DataContainer& dataPoints,
                                             const map_grid_t* gridMap,
                                             Eigen::Matrix3f& h,
                                             Eigen::Vector3f& dTr)
{
     int size = dataPoints.getSize();
     //laser pose in map transform
  //   Eigen::Affine2f transform(getTransformForState(pose)*Eigen::Rotation2Df(3.1415926));
     Eigen::Affine2f transform(getTransformForState(pose));
     float sinRot = sin(pose[2]);
     float cosRot = cos(pose[2]);

     h = Eigen::Matrix3f::Zero();
     dTr = Eigen::Vector3f::Zero();

     bool pub= false;

     if(publishScan && fabs(gridMap->scale - multMap[0].scale)<0.001)
     {
         ptcloud.points.clear();
         ptcloud.points.resize(size);
         ptcloud.header.frame_id ="map";
         ptcloud.header.stamp=ros::Time::now();
         pub= true;
     }

     for (int i = 0; i < size; ++i) {

       const Eigen::Vector2f& currPoint (dataPoints.getVecEntry(i));  //end point in laser pose
       // end point in map pose
       const Eigen::Vector2f endPoint =transform * currPoint;

       if(pub)
       {
          geometry_msgs:: Point32 p;
          p.x= MAP_WXGX(gridMap, endPoint[0]);
          p.y= MAP_WYGY(gridMap, endPoint[1]);
          p.z= 0;
          ptcloud.points.push_back(p);
       }


       Eigen::Vector3f transformedPointData(interpMapValueWithDerivatives(gridMap,endPoint));

       float funVal = 1.0f - transformedPointData[0];

       dTr[0] += transformedPointData[1] * funVal;
       dTr[1] += transformedPointData[2] * funVal;

       float rotDeriv = ((-sinRot * currPoint.x() - cosRot * currPoint.y()) * transformedPointData[1] +
                         (cosRot * currPoint.x() - sinRot * currPoint.y()) * transformedPointData[2]);

       dTr[2] += rotDeriv * funVal;

       h(0, 0) += transformedPointData[1]*transformedPointData[1];
       h(1, 1) += transformedPointData[2]*transformedPointData[2];
       h(2, 2) += rotDeriv*rotDeriv;

       h(0, 1) += transformedPointData[1] * transformedPointData[2];
       h(0, 2) += transformedPointData[1] * rotDeriv;
       h(1, 2) += transformedPointData[2] * rotDeriv;
     }

     h(1, 0) = h(0, 1);
     h(2, 0) = h(0, 2);
     h(2, 1) = h(1, 2);
}

Eigen::Vector3f ScanProcessor::interpMapValueWithDerivatives(const map_grid_t* gridMap,
                                              const Eigen::Vector2f& coords)
{
  if(!MAP_VALID(gridMap, coords[0],coords[1]))
  {
      return Eigen::Vector3f(0.0f, 0.0f, 0.0f);
  }
  //map coords are always positive, floor them by casting to int
   Eigen::Vector2i indMin(coords.cast<int>());

   //get factors for bilinear interpolation
   Eigen::Vector2f factors(coords - indMin.cast<float>());
   int sizeX = gridMap->size_x;
   int index = indMin[1] * sizeX + indMin[0];
   float intensities[4];

   intensities[0] = gridMap->cell_pbb[index];
   ++index;
   intensities[1] = gridMap->cell_pbb[index];
   index += sizeX-1;
   intensities[2] = gridMap->cell_pbb[index];
   ++index;
   intensities[3] = gridMap->cell_pbb[index];

   float dx1 = intensities[0] - intensities[1];
   float dx2 = intensities[2] - intensities[3];

   float dy1 = intensities[0] - intensities[2];
   float dy2 = intensities[1] - intensities[3];

   float xFacInv = (1.0f - factors[0]);
   float yFacInv = (1.0f - factors[1]);

   return Eigen::Vector3f(
     ((intensities[0] * xFacInv + intensities[1] * factors[0]) * (yFacInv)) +
     ((intensities[2] * xFacInv + intensities[3] * factors[0]) * (factors[1])),
     -((dx1 * yFacInv) + (dx2 * factors[1])),
     -((dy1 * xFacInv) + (dy2 * factors[0]))
   );
}

Eigen::Affine2f ScanProcessor::getTransformForState(const Eigen::Vector3f& transVector)
{
  return Eigen::Translation2f(transVector[0], transVector[1]) * Eigen::Rotation2Df(transVector[2]);
}

void ScanProcessor::LaserScanToDataContainer(const sensor_msgs::LaserScanConstPtr& scan,
                                             float scaleToMap)
{
    size_t size = scan->ranges.size();
    float angle = scan->angle_min;
    dataContainer.clear();
    dataContainer.setOrigo(Eigen::Vector2f::Zero());

    for (size_t i = 0; i < size; ++i)
    {
        float dist = scan->ranges[i];
        if ( (dist > scan->range_min) && (dist < scan->range_max))
        {
            dist *= scaleToMap;
                                                                                       //by hulu
            dataContainer.add(Eigen::Vector2f(cos(angle) * dist, sin(angle) * dist));  //note '-' ,because in urdf laser_link 反转180度   

        }
        angle += scan->angle_increment;
    }
  //  std::cout<<"laser size = "<<size<<"\n";
}

void ScanProcessor::SetLaserPose(const pf_vector_t& p)
{
    this->laser_pose=p;
}


void ScanProcessor::GetMultiMap(const map_t* gridMap)
{

    for(int i=0;i<numDepth;i++)
    {
        multMap[i].origin_x = gridMap->origin_x;
        multMap[i].origin_y = gridMap->origin_y;
        if(i==0)
        {
            multMap[i].scale = gridMap->scale ;
            multMap[i].size_x  = gridMap->size_x ;
            multMap[i].size_y = gridMap->size_y ;
            multMap[i].cell_pbb =new float[multMap[i].size_x * multMap[i].size_y];
            for(int k=0 ; k< multMap[i].size_x * multMap[i].size_y ;k++)
                multMap[i].cell_pbb[k] = gridMap->cells[k].probability;
        }else{
            multMap[i].scale =  (multMap[0].scale)*(1<<i) ;
            multMap[i].size_x = (multMap[0].size_x) >> i;
            multMap[i].size_y = (multMap[0].size_y) >> i ;
            multMap[i].cell_pbb =new float[multMap[i].size_x * multMap[i].size_y];

            int index=0;
            int num = (1<<i);
            for(int y=0; y<multMap[i].size_y; y++)
            {
                for(int x=0; x<multMap[i].size_x; x++)
                {
                   float sum=0;
                   int cnt=0;
                   for(int jj=0;jj<num;jj++)
                   {
                       for(int ii=0;ii<num;ii++)
                       {
                           index= GetGridIndexOfMap(multMap[0].size_x,(x*num+ii),(y*num+jj));
                           sum+= gridMap->cells[index].probability;
                           cnt++;
//                           if(sum<gridMap->cells[index].probability)
//                               sum = gridMap->cells[index].probability;
                       }
                   }
                   multMap[i].cell_pbb[(y*multMap[i].size_x) + x] = sum/cnt ;
                 //  multMap[i].cell_pbb[(y*multMap[i].size_x) + x] = sum ;
                }
            }
        }

//        nav_msgs::OccupancyGrid grid;
//        grid.info.width=multMap[i].size_x;
//        grid.info.height=multMap[i].size_y;
//        grid.info.resolution =multMap[i].scale;
//        grid.data.resize(multMap[i].size_x * multMap[i].size_y,-1);

//        for(int k=0 ; k< multMap[i].size_x * multMap[i].size_y ;k++)
//            grid.data[k] =(char)(multMap[i].cell_pbb[k]*100);
//        std::string filename ="/home/zw/g"+std::to_string(i);
//        OccupancyToPgmAndYaml(grid,filename);
    }
}


void ScanProcessor::uniformPoseGenerator(const Eigen::Vector3f& center,
                                         const map_t* map,
                                         float dwindows ,
                                         float awindows,
                                         int num)
{
    poseSets.clear();
    float delta ;
    poseSet_t p;  
    p.grade=0;

    p.pose=center;
    poseSets.push_back(p);

    for(int i=0;i<num;i++)
    {
       do{
            delta = drand48();
        }while(delta==0.0);
       p.pose[0] = center[0] + (1-delta*2)*dwindows;    //robot in world pose;

       do{
            delta = drand48();
       }while(delta==0.0);
       p.pose[1] = center[1] + (1-delta*2)*dwindows;

       do{
            delta = drand48();
       }while(delta==0.0);
       p.pose[2] = center[2] + (1-drand48()*2)*awindows;

       if ( p.pose[2] > M_PI) {
            p.pose[2] -= M_PI * 2.0f;
       } else if ( p.pose[2] < -M_PI) {
            p.pose[2] += M_PI * 2.0f;
       }

       poseSets.push_back(p);
     //  ROS_INFO("randpose:[%6.3f %6.3f]\n",p.pose[0],p.pose[1]);
    }
}

float ScanProcessor::getPoseSetGrade(const DataContainer& dataPoints,
                                     const map_t* map,
                                     const poseSet_t& set,
                                     int skip)
{
    int size = dataPoints.getSize();
    int index ,mi, mj;

    Eigen::Vector3f lpw(set.pose[0]+laser_pose.v[0],      //laser pose in world
                        set.pose[1]+laser_pose.v[1],
                        set.pose[2]);

    Eigen::Affine2f transform(getTransformForState(lpw));  //laser pose in world transform

    double z=0,p=0,pz=0 ;
    int vcnt=0;

    for (int i = 0; i < size; i+=skip)
    {
        const Eigen::Vector2f& currPoint(dataPoints.getVecEntry(i)*map->scale);  //end point in laser pose
        // end point in map pose
        const Eigen::Vector2f endPoint =transform * currPoint;  //end point in world

        mi=MAP_GXWX(map, endPoint[0]);
        mj=MAP_GYWY(map, endPoint[1]);
#if 1
        if(MAP_VALID(map, mi, mj))
        {
            index = MAP_INDEX(map, mi, mj);
//            if(map->cells[index].occ_dist==0)
//                p +=2;
//            else if(map->cells[index].occ_dist==1)
//                p++ ;
//            else if(map->cells[index].occ_state == -1)
                //grade --;
            p+=map->cells[index].probability;
            vcnt ++;
        }
#else
        if(!MAP_VALID(map, mi, mj))
          z = map->max_occ_dist;
        else
          z = map->cells[MAP_INDEX(map,mi,mj)].occ_dist;
        pz=0;
        // Gaussian model
        // NOTE: this should have a normalization of 1/(sqrt(2pi)*sigma)
        pz +=0.9*exp(-(z * z) / 2*0.05*0.05);
        pz += 0.1* 1.0/3;
        p +=pz*pz*pz;
#endif
    }
    if(vcnt==0)
        return 0;
    else
        return (p/vcnt);
}

 Eigen::Vector3f ScanProcessor::getBestSet(void)
 {
  std::sort(poseSets.begin(),poseSets.end(), cmp_grade);
  return poseSets[0].pose ;
 }

 void ScanProcessor:: writePoseToTxt(const char *filePath,
                                     const Eigen::Vector3f& p1,
                                     const Eigen::Vector3f& p2,
                                     int& i)
   {
     if(i==0)
     {
         if(access(filePath,F_OK)==0)
         {
             remove(filePath);
         }
     }
     std::string str=std::to_string(i++)+" "+
                     std::to_string((int)(p1[0]*1000))+" "+
                     std::to_string((int)(p1[1]*1000))+" "+
                     std::to_string((int)(p1[2]*1000))+" "+
                     std::to_string((int)(p2[0]*1000))+" "+
                     std::to_string((int)(p2[1]*1000))+" "+
                     std::to_string((int)(p2[2]*1000))+"\n";
     int len =str.length();
     const char * buf = str.c_str();

     int fd=open(filePath,O_CREAT|O_WRONLY|O_APPEND,S_IRWXU|S_IRWXG|S_IRWXO);
     if(-1==fd)
     {
       printf("test.txt not exist !\n");
     }
     write(fd,buf,len);
     close(fd);
 }

 bool cmp_grade(const poseSet_t& s1,const poseSet_t& s2)
 {
     return s1.grade >s2.grade ;
 }



}

