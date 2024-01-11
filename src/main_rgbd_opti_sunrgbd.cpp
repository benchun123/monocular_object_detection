// std c
#include <stdio.h>
#include <iostream>
#include <fstream>
#include <string>

// opencv
#include <opencv/cv.h>
#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>
#include "opencv2/imgproc/imgproc.hpp"

// Eigen
#include <Eigen/Dense>
#include <Eigen/Core>

// third party
#include "tictoc_profiler/profiler.hpp"

// ours
#include "sun_rgbd.h"
#include "detect_cuboid_bbox/detect_cuboid_bbox.h"
#include "plane_detection.h"

using namespace std;
using namespace cv;
using namespace Eigen;

int main(int argc, char **argv)
{
	if (argc != 2)
	{
		cout << "Usage: det_bbox_sun_rgbd_node path/to/data" << endl;
		return -1;
	}

	ca::Profiler::enable();
	std::string base_folder = std::string(argv[1]);
	std::string image_folder = base_folder + "/rgb/";
	std::string depth_folder = base_folder + "/depth/";
	std::string label_folder = base_folder + "/label/";
	std::string calib_folder = base_folder + "/calib/";
	std::string output_folder = base_folder + "/offline_cuboid/";
	std::string output_img_folder = base_folder + "/offline_img/";
	std::string index_file = base_folder + "/index.txt";
	std::string obj_dim_file = base_folder + "/obj_dim_average.txt";

	dataset_sunrgbd data_loader;
	data_loader.LoadImageIndex(index_file);
	std::vector<std::string> vImageId = data_loader.vstrImageIndex;

	detect_cuboid_bbox object_detector;
	object_detector.whether_plot_detail_images = true;
	object_detector.whether_plot_ground_truth = false;
	object_detector.whether_plot_sample_images = false;
	object_detector.whether_plot_final_scores = true;
	object_detector.whether_sample_obj_dimension = true;
	object_detector.whether_sample_obj_yaw = true;
	object_detector.whether_add_plane_constraints = true;
	object_detector.whether_save_cam_obj_data = true;
	object_detector.whether_save_final_image = true;

	object_detector.Read_Dimension_SUNRGBD(obj_dim_file);

	int total_frame_number = vImageId.size();
	total_frame_number = 50;
	for (int frame_index = 0; frame_index < total_frame_number; frame_index++)
	{
		// frame_index=243;
		std::cout << "-----------" << "frame index " << frame_index << " " << vImageId[frame_index] << "-----------" << std::endl;

		//read image, calib and input
		ca::Profiler::tictoc("read image");
		std::string image_file = image_folder + "/" + vImageId[frame_index] + ".jpg";
		object_detector.Read_Image_SUNRGBD(image_file);
		std::string calib_file = calib_folder + "/" + vImageId[frame_index] + ".txt";
		object_detector.Read_Kalib_SUNRGBD(calib_file);
		std::string truth_cuboid_file = label_folder + "/" + vImageId[frame_index] + ".txt";
		object_detector.Read_Label_SUNRGBD(truth_cuboid_file);
		ca::Profiler::tictoc("read image");

		// read depth image
		ca::Profiler::tictoc("plane estimation");
		PlaneDetection plane_detector;
		string depth_img_file = depth_folder + "/" + vImageId[frame_index] + ".png";
		plane_detector.setDepthValue(8000);
		plane_detector.setKalibValue(object_detector.Kalib);
		plane_detector.readDepthImage(depth_img_file);
		plane_detector.ConvertDepthToPointCloud();
		plane_detector.ComputePlanesFromOrganizedPointCloud();
		ca::Profiler::tictoc("plane estimation");

		std::vector<ObjectSet> frames_cuboids; // each 2d bbox generates an ObjectSet, which is vector of sorted proposals
		cv::Mat rgb_img = object_detector.rgb_img.clone();
		std::vector<cv::Mat> det_plane = plane_detector.mvPlaneCoefficients;
		// create save file every frame

		string output_cuboid_file = output_folder + "/" + vImageId[frame_index] + "_3d_cuboids.txt";
		string output_cuboid_img = output_img_folder + "/" + vImageId[frame_index] + "_3d_img.png";
		object_detector.detect_cuboid_every_frame(rgb_img, det_plane, frames_cuboids, output_cuboid_file, output_cuboid_img);

	} // loop frame_id

	ca::Profiler::print_aggregated(std::cout);

	return 0;
}