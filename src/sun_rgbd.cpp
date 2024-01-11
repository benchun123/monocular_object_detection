#include "sun_rgbd.h"
#include "detect_cuboid_bbox/matrix_utils.h"

void dataset_sunrgbd::LoadImageIndex(const string &strFile)
{
    ifstream f;
    f.open(strFile.c_str());
    while(!f.eof())
    {
        string s;
        getline(f,s);
        if(!s.empty())
        {
            stringstream ss;
            ss << s;
            string idx;
            ss >> idx;
            vstrImageIndex.push_back(idx);
        }
    }
}

void dataset_sunrgbd::LoadObjectGT(const string &obj_dim_file)
{
    // std::vector<std::string> obj_name_list;// label: class(1), count(1), dim(3), 
    // Eigen::MatrixXd obj_dim_list(1,4); 
    obj_dim_list.setZero();
    if (!read_obj_detection_txt(obj_dim_file, obj_dim_list, obj_name_list))
        return -1;
    std::cout << "obj_dim_list: \n" << obj_dim_list << std::endl;    
}

    