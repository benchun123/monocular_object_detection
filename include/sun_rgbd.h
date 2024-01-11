// std c
#include <stdio.h>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>

// Eigen
#include <Eigen/Dense>
#include <Eigen/Core>

using namespace std;

class dataset_sunrgbd
{
public:
    /* data */
    vector<string> vstrImageIndex;
    std::vector<std::string> obj_name_list;// label: class(1), count(1), dim(3), 
    Eigen::MatrixXd obj_dim_list; 

public:
    dataset_sunrgbd(/* args */);
    ~dataset_sunrgbd();
    void LoadImageIndex(const string &strFile);
    void LoadObjectGT(const string &obj_dim_file);
};

dataset_sunrgbd::dataset_sunrgbd(/* args */)
{
}

dataset_sunrgbd::~dataset_sunrgbd()
{
}

