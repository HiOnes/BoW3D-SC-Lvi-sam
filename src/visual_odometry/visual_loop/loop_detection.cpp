#include "loop_detection.h"

LoopDetector::LoopDetector(){}


void LoopDetector::loadVocabulary(std::string voc_path)
{
    voc = new BriefVocabulary(voc_path);
    db.setVocabulary(*voc, false, 0);
}

//处理当前关键帧
//1.如果不回环，addKeyFrameIntoVoc,将当前帧加入词典
//2.如果回环，detectloop,检测回环并获得回环候选帧索引;之后根据分数检查回环可靠性，如果成功，pub_match_msg
//3.添加关键帧到keyframelist
void LoopDetector::addKeyFrame(KeyFrame* cur_kf, bool flag_detect_loop)
{
    int loop_index = -1;
    //一、回环检测，返回回环候选帧的索引，若不回环将当前帧加入词典
    if (flag_detect_loop)
    {
        loop_index = detectLoop(cur_kf, cur_kf->index); //返回回环候选帧的索引
    }
    else
    {
        addKeyFrameIntoVoc(cur_kf); //当前帧加入词典
    }

    //二、使用ransan和pnp检查回环 check loop if valid using ransan and pnp
	if (loop_index != -1)
	{
        //1）获取回环候选帧
        KeyFrame* old_kf = getKeyFrame(loop_index); 

        //2）当前帧与回环候选帧进行描述子匹配
        if (cur_kf->findConnection(old_kf))
        {
            // std::cout << "loop findConnection success" << std::endl;
            std_msgs::Float64MultiArray match_msg;
            match_msg.data.push_back(cur_kf->time_stamp);
            match_msg.data.push_back(old_kf->time_stamp);
            pub_match_msg.publish(match_msg); //发布回环匹配信息
        }
        else
        {
            // std::cout << "loop findConnection failed" << std::endl;
        }
	}

    //三、添加关键帧add keyframe
	keyframelist.push_back(cur_kf);
}

KeyFrame* LoopDetector::getKeyFrame(int index)
{
    list<KeyFrame*>::iterator it = keyframelist.begin();
    for (; it != keyframelist.end(); it++)   
    {
        if((*it)->index == index)
            break;
    }
    if (it != keyframelist.end())
        return *it;
    else
        return NULL;
}

//对当前帧进行回环检测
//1.将图像放到image_pool可视化
//2.查询字典数据库，得到与每一帧的相似度评分ret，并添加到字典
//3.通过相似度评分ret判断是否存在回环候选帧,find_loop
//4.判断当前帧的索引值是否大于50，即系统开始的前50帧不进行回环；
//  返回评分大于0.015的最早的关键帧索引min_index，如果不存在回环或判断失败则返回-1
//ps:打开DEBUG_IMAGE，将回环的两帧图像放到一起做对比。
int LoopDetector::detectLoop(KeyFrame* keyframe, int frame_index)
{
    //一、将图像放到图像池可视化 put image into image_pool; for visualization
    cv::Mat compressed_image;
    if (DEBUG_IMAGE)
    {
        int feature_num = keyframe->keypoints.size();
        cv::resize(keyframe->image, compressed_image, cv::Size(376, 240));
        putText(compressed_image, "feature_num:" + to_string(feature_num), cv::Point2f(10, 10), cv::FONT_HERSHEY_SIMPLEX, 0.4, cv::Scalar(255));
        image_pool[frame_index] = compressed_image;
    }
    //二、查询字典数据库，得到与每一帧的相似度评分ret，并添加到字典 first query; then add this frame into database!
    QueryResults ret;
    db.query(keyframe->brief_descriptors, ret, 4, frame_index - 200);
    //printf("query time: %f", t_query.toc());
    //cout << "Searching for Image " << frame_index << ". " << ret << endl;

    db.add(keyframe->brief_descriptors);
    //printf("add feature time: %f", t_add.toc());
    // ret[0] is the nearest neighbour's score. threshold change with neighour score
    
    cv::Mat loop_result;
    if (DEBUG_IMAGE)
    {
        loop_result = compressed_image.clone();
        if (ret.size() > 0)
            putText(loop_result, "neighbour score:" + to_string(ret[0].Score), cv::Point2f(10, 50), cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(255));
    }
    // visual loop result 
    if (DEBUG_IMAGE)
    {
        for (unsigned int i = 0; i < ret.size(); i++)
        {
            int tmp_index = ret[i].Id;
            auto it = image_pool.find(tmp_index);
            cv::Mat tmp_image = (it->second).clone();
            putText(tmp_image, "index:  " + to_string(tmp_index) + "loop score:" + to_string(ret[i].Score), cv::Point2f(10, 50), cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(255));
            cv::hconcat(loop_result, tmp_image, loop_result);
        }
    }
    //三、通过相似度评分判断是否存在回环候选帧
    // a good match with its nerghbour
    bool find_loop = false;
    if (ret.size() >= 1 && ret[0].Score > 0.05)
    {
        for (unsigned int i = 1; i < ret.size(); i++)
        {
            //if (ret[i].Score > ret[0].Score * 0.3)
            if (ret[i].Score > 0.015)
            {          
                find_loop = true;
                
                if (DEBUG_IMAGE && 0)
                {
                    int tmp_index = ret[i].Id;
                    auto it = image_pool.find(tmp_index);
                    cv::Mat tmp_image = (it->second).clone();
                    putText(tmp_image, "loop score:" + to_string(ret[i].Score), cv::Point2f(10, 50), cv::FONT_HERSHEY_SIMPLEX, 0.4, cv::Scalar(255));
                    cv::hconcat(loop_result, tmp_image, loop_result);
                }
            }

        }
    }
    
    if (DEBUG_IMAGE)
    {
        cv::imshow("loop_result", loop_result);
        cv::waitKey(20);
    }

    // std::cout <<"frame_index: "<< frame_index << " ret.size(): " << ret.size() << std::endl;
    // if (ret.size() >=1)
    // {
    //     std::cout << "ret[0].Score: " << ret[0].Score << std::endl;
    // }
    // std::cout << "find_loop: " << find_loop << std::endl;

    //四、判断当前帧的索引值是否大于50，即系统开始的前50帧不进行回环；
    //返回评分大于0.015的最早的关键帧索引min_index，如果不存在回环或判断失败则返回-1
    if (find_loop && frame_index > 50)
    {
        int min_index = -1;
        for (unsigned int i = 0; i < ret.size(); i++)
        {
            if (min_index == -1 || ((int)ret[i].Id < min_index && ret[i].Score > 0.015))
                min_index = ret[i].Id;
        }
        return min_index;
    }
    else
        return -1;

}

void LoopDetector::addKeyFrameIntoVoc(KeyFrame* keyframe)
{
    // put image into image_pool; for visualization
    cv::Mat compressed_image;
    if (DEBUG_IMAGE)
    {
        int feature_num = keyframe->keypoints.size();
        cv::resize(keyframe->image, compressed_image, cv::Size(376, 240));
        putText(compressed_image, "feature_num:" + to_string(feature_num), cv::Point2f(10, 10), cv::FONT_HERSHEY_SIMPLEX, 0.4, cv::Scalar(255));
        image_pool[keyframe->index] = compressed_image;
    }

    db.add(keyframe->brief_descriptors);
}

void LoopDetector::visualizeKeyPoses(double time_cur)
{
    if (keyframelist.empty() || pub_key_pose.getNumSubscribers() == 0)
        return;

    visualization_msgs::MarkerArray markerArray;

    int count = 0;
    int count_lim = 10;

    visualization_msgs::Marker markerNode;
    markerNode.header.frame_id = "vins_world";
    markerNode.header.stamp = ros::Time().fromSec(time_cur);
    markerNode.action = visualization_msgs::Marker::ADD;
    markerNode.type = visualization_msgs::Marker::SPHERE_LIST;
    markerNode.ns = "keyframe_nodes";
    markerNode.id = 0;
    markerNode.pose.orientation.w = 1;
    markerNode.scale.x = 0.3; markerNode.scale.y = 0.3; markerNode.scale.z = 0.3; 
    markerNode.color.r = 0; markerNode.color.g = 0.8; markerNode.color.b = 1;
    markerNode.color.a = 1;

    for (list<KeyFrame*>::reverse_iterator rit = keyframelist.rbegin(); rit != keyframelist.rend(); ++rit)
    {
        if (count++ > count_lim)
            break;

        geometry_msgs::Point p;
        p.x = (*rit)->origin_vio_T.x();
        p.y = (*rit)->origin_vio_T.y();
        p.z = (*rit)->origin_vio_T.z();
        markerNode.points.push_back(p);
    }

    markerArray.markers.push_back(markerNode);
    pub_key_pose.publish(markerArray);
}