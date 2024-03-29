// std c
#include <stdio.h>
#include <math.h>
#include <iostream>
#include <fstream>
#include <iostream>
#include <string>
#include <sstream>
#include <ctime>
#include <numeric>

// opencv 
#include <opencv/cv.h>
#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>
#include "opencv2/imgproc/imgproc.hpp"

// Eigen
#include <Eigen/Dense>
#include <Eigen/Core>

// ours
#include <line_lbd/line_lbd_allclass.h>
#include <line_lbd/line_descriptor.hpp>

#include "detect_cuboid_bbox/matrix_utils.h"
#include "detect_cuboid_bbox/object_3d_util.h"
#include "tictoc_profiler/profiler.hpp"

using namespace std;
// using namespace cv;
using namespace Eigen;

bool detect_cuboid_bbox::Read_Image_SUNRGBD(std::string & img_file) // read images width and height 
{
	rgb_img = cv::imread(img_file, CV_LOAD_IMAGE_COLOR);
	if (rgb_img.empty() || rgb_img.depth() != CV_8U)
		cout << "ERROR: cannot read color image. No such a file, or the image format is not 8UC3" << endl;
}

bool detect_cuboid_bbox::Read_Kalib_SUNRGBD(std::string &calib_file)
{
	Eigen::MatrixXd calib_data(2,9); 
	if (!read_all_number_txt(calib_file, calib_data))
		return -1;
	// std::cout << "calib_data: \n" << calib_data << std::endl;
	Eigen::Matrix3d Calib, Rtilt;  
	Rtilt << calib_data(0,0), calib_data(0,3), calib_data(0,6), 
			calib_data(0,1), calib_data(0,4), calib_data(0,7),
			calib_data(0,2), calib_data(0,5), calib_data(0,8);
	Calib << calib_data(1,0), calib_data(1,3), calib_data(1,6), 
			calib_data(1,1), calib_data(1,4), calib_data(1,7),
			calib_data(1,2), calib_data(1,5), calib_data(1,8);
	std::cout << "Calib: \n" << Calib << std::endl;
	std::cout << "Rtilt: \n" << Rtilt << std::endl;
	Eigen::Matrix3d rotate_y_z; // change from world to camera coordinate ...
	rotate_y_z << 1,0,0,  0,0,-1,  0,1,0; // roll, pitch, yaw = 1.57, 0, 0;
	// rotate_y_z << 1,0,0,  0,0,1,  0,-1,0; // roll, pitch, yaw = 1.57, 3.14, 3.14;
	Eigen::MatrixXd Tcw = rotate_y_z * Rtilt.transpose();
	// std::cout << "Tcw: \n" << Tcw << std::endl;

	Matrix4d transToWolrd; // Twc
	transToWolrd.setIdentity();
	transToWolrd.block(0,0,3,3) = Tcw.transpose();
	transToWolrd.col(3).head(3) = Eigen::Vector3d(0,0,0);
	// // std::cout << "euler_angles: " << rotate_y_z.eulerAngles ( 0,1,2 ) << std::endl;
	// Eigen::MatrixXd proj_matrix(3,4); // from world to image
	// proj_matrix.block(0,0,3,3) = Calib * rotate_y_z * Rtilt.transpose();
	// proj_matrix.col(3).head(3) = Eigen::Vector3d(0,0,0); // camera lays in 0,0,0 in original dataset
	// Eigen::Vector3d camera_rpy;
	// Eigen::Matrix3d Rot_Mat = transToWolrd.block(0,0,3,3); 
	// quat_to_euler_zyx(Quaterniond(Rot_Mat), camera_rpy(0), camera_rpy(1), camera_rpy(2));
	// std::cout << "camera orientation: " << 180/M_PI*camera_rpy.transpose() << std::endl;

	Kalib = Calib; // copy to detector.Kalib
	Twc = transToWolrd; // copy to detector.Twc
	return true;
}

bool detect_cuboid_bbox::Read_Dimension_SUNRGBD(std::string &dim_file)
{
    // std::vector<std::string> dataset_obj_name_list;// label: class(1), count(1), dim(3), 
    // Eigen::MatrixXd dataset_obj_dim_list(1,4); 
	dataset_obj_name_list.clear();
    dataset_obj_dim_list.resize(1,4); 
    dataset_obj_dim_list.setZero();
    if (!read_obj_detection_txt(dim_file, dataset_obj_dim_list, dataset_obj_name_list))
        return -1;
    std::cout << "dataset_obj_dim_list: \n" << dataset_obj_dim_list << std::endl;
	return true;
}

bool detect_cuboid_bbox::Read_Label_SUNRGBD(std::string &label_file)
{
	std::vector<std::string> truth_class;// label: class(1), 2d bbox(4), centroid(3), dim(3), orient(2)
	Eigen::MatrixXd truth_cuboid_list(1,12); 
	truth_class.clear();
	truth_cuboid_list.setZero();
	if (!read_obj_detection_txt(label_file, truth_cuboid_list, truth_class))
		return -1;
	std::cout << "truth_cuboid_list: \n" << truth_cuboid_list << std::endl;
	detected_obj_name = truth_class; // regards truth as input
	detected_obj_input = truth_cuboid_list;
	return true;
}

bool detect_cuboid_bbox::Get_Object_Input_SUNRGBD(const int& obj_id, std::string &obj_name, 
            Eigen::Vector4d &obj_bbox, Eigen::Vector3d& obj_dim, double& obj_yaw)
{
	// prepare ground truth, we only need average dimension
	obj_name = detected_obj_name[obj_id];
	obj_bbox = detected_obj_input.row(obj_id).head(4);
	// check bbox inside image
	if(obj_bbox(0)>rgb_img.cols || obj_bbox(0)+obj_bbox(2)>rgb_img.cols 
		|| obj_bbox(1)>rgb_img.rows ||obj_bbox(1)+obj_bbox(3)>rgb_img.rows)
		return false;
	// check object name
	auto it = std::find(dataset_obj_name_list.begin(), dataset_obj_name_list.end(), obj_name); // find the name, if not, ignore
	if (it == dataset_obj_name_list.end()) //other object, ignore
	{
		std::cout << "----- class name ignore -----" << std::endl;
		return false;
	}
	else
	{
		int obj_name_id = std::distance(dataset_obj_name_list.begin(), it);
		obj_dim << dataset_obj_dim_list(obj_name_id,2), dataset_obj_dim_list(obj_name_id,1), dataset_obj_dim_list(obj_name_id,3);
	}

	// Eigen::Vector3d object_loc_global = detected_obj_input.row(obj_id).segment(4,3);
	// Eigen::Vector3d object_dim_truth = detected_obj_input.row(obj_id).segment(7,3);
	// obj_dim = Eigen::Vector3d(object_dim_truth(1), object_dim_truth(0), object_dim_truth(2)) ; // switch x y
	
	Eigen::Vector2d heading = detected_obj_input.row(obj_id).tail(2);
	obj_yaw = 1 * atan2(heading(1), heading(0));
	return true;
}

void detect_cuboid_bbox::compute_obj_visible_edge(cuboid* new_cuboid, 
        Eigen::MatrixXi& visible_edge_pt_ids, Eigen::MatrixXd& box_edges_visible)
{

	// prepare for visible 2d corner in image
	// based on trial, may be different from different dataset ...
	Eigen::Matrix3d obj_rot_cam = new_cuboid->obj_rot_camera;
	Eigen::MatrixXd box_corners_2d_float = new_cuboid->box_corners_2d;
	Eigen::Vector3d object_rpy;
	quat_to_euler_zyx(Quaterniond(obj_rot_cam), object_rpy(0), object_rpy(1), object_rpy(2));
	std::cout << "object_rpy: " << object_rpy.transpose() << std::endl;

	// Eigen::MatrixXi visible_edge_pt_ids;
	if (object_rpy(2) >= -180.0/180.0*M_PI && object_rpy(2) < -170.0/180.0*M_PI) 
	{
		visible_edge_pt_ids.resize(7, 2);
		visible_edge_pt_ids << 0,1, 1,2, 2,3, 3,0, 0,4, 3,7, 6,7; // 0123 on th top are shown all the time
	}
	else if (object_rpy(2) >= -170.0/180.0*M_PI && object_rpy(2) < -90.0/180.0*M_PI)
	{
		visible_edge_pt_ids.resize(9, 2);
		visible_edge_pt_ids << 0,1, 1,2, 2,3, 3,0, 0,4, 1,5, 3,7, 4,5, 4,7; // 0123 on th top are shown all the time
	}
	else if (object_rpy(2) >= -90.0/180.0*M_PI && object_rpy(2) < -80.0/180.0*M_PI) 
	{
		visible_edge_pt_ids.resize(7, 2);
		visible_edge_pt_ids << 0,1, 1,2, 2,3, 3,0, 0,4, 3,7, 4,7; // 0123 on th top are shown all the time
	}
	else if (object_rpy(2) >= -80.0/180.0*M_PI && object_rpy(2) < 0.0/180.0*M_PI) 
	{
		visible_edge_pt_ids.resize(9, 2);
		visible_edge_pt_ids << 0,1, 1,2, 2,3, 3,0, 0,4, 2,6, 3,7, 5,7, 6,7; // 0123 on th top are shown all the time
	}
	else if (object_rpy(2) >= 0.0/180.0*M_PI && object_rpy(2) < 10.0/180.0*M_PI)
	{
		visible_edge_pt_ids.resize(7, 2);
		visible_edge_pt_ids << 0,1, 1,2, 2,3, 3,0, 2,6, 3,7, 6,7; // 0123 on th top are shown all the time
	}
	else if (object_rpy(2) >= 10.0/180.0*M_PI && object_rpy(2) < 90.0/180.0*M_PI) 
	{
		visible_edge_pt_ids.resize(9, 2);
		visible_edge_pt_ids << 0,1, 1,2, 2,3, 3,0, 1,5, 2,6, 3,7, 5,6, 6,7; // 0123 on th top are shown all the time
	}
	else if (object_rpy(2) >= 90.0/180.0*M_PI && object_rpy(2) < 100.0/180.0*M_PI) // -3.14=+3.14
	{
		visible_edge_pt_ids.resize(7, 2);
		visible_edge_pt_ids << 0,1, 1,2, 2,3, 3,0, 2,6, 3,7, 6,7; // 0123 on th top are shown all the time
	}
	else if (object_rpy(2) >= 100.0/180.0*M_PI && object_rpy(2) < 180.0/180.0*M_PI) 
	{
		visible_edge_pt_ids.resize(9, 2);
		visible_edge_pt_ids << 0,1, 1,2, 2,3, 3,0, 0,4, 2,6, 3,7, 4,7, 6,7; // 0123 on th top are shown all the time
	}

	// std::cout <<"visible_edge_pt_ids: \n" << visible_edge_pt_ids.transpose() << std::endl;

	// prepare for visible 2d edges in image
	// MatrixXd box_edges_visible;
	box_edges_visible.resize(visible_edge_pt_ids.rows(),4);
	for (size_t i = 0; i < box_edges_visible.rows(); i++)
	{
		box_edges_visible(i,0) = box_corners_2d_float(0, visible_edge_pt_ids(i,0));
		box_edges_visible(i,1) = box_corners_2d_float(1, visible_edge_pt_ids(i,0));
		box_edges_visible(i,2) = box_corners_2d_float(0, visible_edge_pt_ids(i,1));
		box_edges_visible(i,3) = box_corners_2d_float(1, visible_edge_pt_ids(i,1));
	}

}

void detect_cuboid_bbox::formulate_cuboid_param(cuboid* new_cuboid, Eigen::Vector3d& obj_loc_cam, 
		Eigen::Matrix3d& obj_rot_cam, Eigen::Vector3d& obj_dim_cam, Eigen::Matrix3d& Kalib)
{

	// prepare for 2d corner in image
	Eigen::MatrixXd corner_3d = compute3D_BoxCorner_in_camera(obj_dim_cam, obj_loc_cam, obj_rot_cam);
	Eigen::MatrixXd box_corners_2d_float = project_camera_points_to_2d(corner_3d, Kalib);
	// box_corners_2d_float = project_camera_points_to_2d(corner_3d, Kalib);
	Eigen::Vector4d bbox_new;
	bbox_new(0) = std::max(0.0, box_corners_2d_float.row(0).minCoeff());
	bbox_new(1) = std::max(0.0, box_corners_2d_float.row(1).minCoeff());
	bbox_new(2) = std::min(double(rgb_img.cols), box_corners_2d_float.row(0).maxCoeff());
	bbox_new(3) = std::min(double(rgb_img.rows), box_corners_2d_float.row(1).maxCoeff());
	// bbox_new << box_corners_2d_float.row(0).minCoeff(), box_corners_2d_float.row(1).minCoeff(),
	//             box_corners_2d_float.row(0).maxCoeff(), box_corners_2d_float.row(1).maxCoeff();
	std::cout << "bbox_new: " << bbox_new.transpose() << std::endl;          


	new_cuboid->obj_loc_camera = obj_loc_cam;
	new_cuboid->obj_rot_camera = obj_rot_cam;
	new_cuboid->obj_dim_camera = obj_dim_cam;
	new_cuboid->box_corners_2d = box_corners_2d_float;
	new_cuboid->box_corners_3d_cam = corner_3d;
	new_cuboid->bbox_2d = bbox_new;
}

void detect_cuboid_bbox::detect_cuboid_with_bbox_constraints(cv::Mat& rgb_img, Eigen::Vector4d& obj_bbox, double& obj_yaw,
         const Eigen::Vector3d& obj_dim_ave, std::vector<cv::Mat>& mvPlaneCoefficients, std::vector<cuboid *>& single_object_candidate)
{

	// step 1: sample yaw and dimension
	std::vector<double> obj_length_samples;
	std::vector<double> obj_width_samples;
	std::vector<double> obj_height_samples;
	std::vector<double> obj_yaw_samples;
	double s_range = 0.2;
	double s_step = 0.2;
	if(whether_sample_obj_dimension)
	{
		linespace<double>(obj_dim_ave(0)*(1-s_range), obj_dim_ave(0)*(1+s_range), obj_dim_ave(0)*s_step, obj_length_samples);
		linespace<double>(obj_dim_ave(1)*(1-s_range), obj_dim_ave(1)*(1+s_range), obj_dim_ave(1)*s_step, obj_width_samples);
		linespace<double>(obj_dim_ave(2)*(1-s_range), obj_dim_ave(2)*(1+s_range), obj_dim_ave(2)*s_step, obj_height_samples);
	}
	else
	{
		obj_length_samples.push_back(obj_dim_ave(0));
		obj_width_samples.push_back(obj_dim_ave(1));
		obj_height_samples.push_back(obj_dim_ave(2));
	}

	// sample object yaw, note the yaw is in world coordinates, could we sample local yaw?
	if(whether_sample_obj_yaw)
	{
		// double yaw_init = camera_rpy(2) - 90.0 / 180.0 * M_PI; // yaw init is directly facing the camera, align with camera optical axis
		double yaw_init = 0.0 / 180.0 * M_PI; // yaw init is directly facing the camera, align with camera optical axis
		linespace<double>(yaw_init - 90.0 / 180.0 * M_PI, yaw_init + 90.0 / 180.0 * M_PI, 5.0 / 180.0 * M_PI, obj_yaw_samples);
		// linespace<double>(yaw_init - 180.0 / 180.0 * M_PI, yaw_init + 180.0 / 180.0 * M_PI, 5.0 / 180.0 * M_PI, obj_yaw_samples);
	}
	else
	{
		obj_yaw_samples.push_back(obj_yaw);
	}


	// step 2: prepare edge and canny for score, do not need loop every proposal, outside
	// //  compute canny and distance map for distance error
	double bbox_thres = 30.0;
	Eigen::Vector4d bbox_canny;
	bbox_canny(0) = std::max(0.0, obj_bbox(0)-bbox_thres);
	bbox_canny(1) = std::max(0.0, obj_bbox(1)-bbox_thres);
	bbox_canny(2) = std::min(double(rgb_img.cols), obj_bbox(2)+obj_bbox(0)+bbox_thres);
	bbox_canny(3) = std::min(double(rgb_img.rows), obj_bbox(3)+obj_bbox(1)+bbox_thres);
	// std::cout << "bbox_canny: " << bbox_canny.transpose() << std::endl;
	cv::Rect canny_bbox = cv::Rect(bbox_canny(0), bbox_canny(1), bbox_canny(2)-bbox_canny(0), bbox_canny(3)-bbox_canny(1)); //left, top, width, height
	cv::Mat gray_img, im_canny;
	if (rgb_img.channels() == 3)
		cv::cvtColor(rgb_img, gray_img, CV_BGR2GRAY);
	else
		gray_img = rgb_img;
	cv::Canny(gray_img(canny_bbox), im_canny, 80, 200); // low thre, high thre    im_canny 0 or 255   [80 200  40 100]
	cv::Mat dist_map;
	cv::distanceTransform(255 - im_canny, dist_map, CV_DIST_L2, 3); // dist_map is float datatype
	if (whether_plot_detail_images)
	{
		cv::imshow("im_canny", im_canny);
		cv::Mat dist_map_img;
		cv::normalize(dist_map, dist_map_img, 0.0, 1.0, cv::NORM_MINMAX);
		cv::imshow("normalized distance map", dist_map_img);
		cv::waitKey();
	}

	//edge detection // maybe just outside and compute once
	line_lbd_detect line_lbd_obj;
	line_lbd_obj.use_LSD = true;
	line_lbd_obj.line_length_thres = 15;  // remove short edges
	cv::Mat all_lines_mat;
	line_lbd_obj.detect_filter_lines(rgb_img, all_lines_mat);
	Eigen::MatrixXd all_lines_raw(all_lines_mat.rows,4);
	for (int rr=0;rr<all_lines_mat.rows;rr++)
		for (int cc=0;cc<4;cc++)
			all_lines_raw(rr,cc) = all_lines_mat.at<float>(rr,cc);
	// std::cout << "all_lines_raw: " << all_lines_raw << std::endl;
	Eigen::Vector2d bbox_top_left = Eigen::Vector2d(bbox_canny(0), bbox_canny(1));
	Eigen::Vector2d bbox_bot_right = Eigen::Vector2d(bbox_canny(0)+bbox_canny(2), bbox_canny(1)+bbox_canny(3));
	// find edges inside the object bounding box
	Eigen::MatrixXd all_lines_inside_object(all_lines_raw.rows(), all_lines_raw.cols()); // first allocate a large matrix, then only use the toprows to avoid copy, alloc
	int inside_obj_edge_num = 0;
	for (int edge_id = 0; edge_id < all_lines_raw.rows(); edge_id++)
	if (check_inside_box(all_lines_raw.row(edge_id).head<2>(), bbox_top_left, bbox_bot_right))
		if (check_inside_box(all_lines_raw.row(edge_id).tail<2>(), bbox_top_left, bbox_bot_right))
		{
		all_lines_inside_object.row(inside_obj_edge_num) = all_lines_raw.row(edge_id);
		inside_obj_edge_num++;
		}
	// merge edges and remove short lines, after finding object edges.  edge merge in small regions should be faster than all.
	double pre_merge_dist_thre = 10;
	double pre_merge_angle_thre = 5;
	double edge_length_threshold = 40;
	MatrixXd all_lines_merge_inobj;
	merge_break_lines(all_lines_inside_object.topRows(inside_obj_edge_num), all_lines_merge_inobj, pre_merge_dist_thre,
			pre_merge_angle_thre, edge_length_threshold);
	// std::cout << "all_lines_merge_inobj: " << all_lines_merge_inobj << std::endl;
	filter_line_by_coincide(all_lines_merge_inobj, pre_merge_angle_thre, pre_merge_dist_thre);
	// std::cout << "all_lines_merge_inobj: " << all_lines_merge_inobj << std::endl;
	if (whether_plot_detail_images)
	{
		cv::Mat output_img;
		plot_image_with_edges(rgb_img, output_img, all_lines_merge_inobj, cv::Scalar(255, 0, 0));
		cv::imshow("Raw detected Edges", output_img);
		cv::waitKey(0);
	}


	// step 4. for every sample and find best cuboid
	double combined_scores = 1e9;
	double min_com_error = 1e9;
	// Eigen::Matrix4d obj_mat_final_camera;
	// obj_mat_final_camera.setZero();
	// Eigen::Vector3d obj_dim_final_camera;
	// Eigen::Vector4d obj_bbox_final_camera;
	cuboid* final_obj_cuboid_cam = new cuboid();

	for (int obj_len_id = 0; obj_len_id < obj_length_samples.size(); obj_len_id++)
		for (int obj_wid_id = 0; obj_wid_id < obj_width_samples.size(); obj_wid_id++)
			for (int obj_hei_id = 0; obj_hei_id < obj_height_samples.size(); obj_hei_id++)
				for (int obj_yaw_id = 0; obj_yaw_id < obj_yaw_samples.size(); obj_yaw_id++)
	{
		std::cout <<"yaw_id: " << obj_yaw_id << "-----------" << std::endl;

		// step 1: get object dimension and orintation in camera coordinate
		Eigen::Vector3d object_dim_cam; // should change coordinates?
		object_dim_cam(0) = obj_length_samples[obj_len_id];
		object_dim_cam(1) = obj_width_samples[obj_wid_id];
		object_dim_cam(2) = obj_height_samples[obj_hei_id];
		std::cout << "object_dim_cam: " << object_dim_cam.transpose() << std::endl;

		double yaw_sample = obj_yaw_samples[obj_yaw_id]; // global
		Eigen::Matrix4d transToWolrd = Twc;
		Eigen::Matrix3d obj_local_rot;
		trasform_rotation_from_world_to_camera(transToWolrd, yaw_sample, obj_local_rot);
		// std::cout <<"obj_local_rot: \n" << obj_local_rot << std::endl;
		Eigen::Vector3d object_rpy;
		quat_to_euler_zyx(Quaterniond(obj_local_rot), object_rpy(0), object_rpy(1), object_rpy(2));
		std::cout << "object_rpy: " << object_rpy.transpose() << std::endl;

		// step 2: calcuate object location in camera coordinate
		double theta_ray = 0;
		Eigen::Vector4d box_2d; // change xywh to xyxy
		box_2d << obj_bbox(0), obj_bbox(1),
				obj_bbox(0)+obj_bbox(2), obj_bbox(1)+obj_bbox(3);
		Eigen::MatrixXd cam_to_img(3,4); // cam_to_img=[K|0]
		cam_to_img.block(0,0,3,3) = Kalib;
		cam_to_img.col(3).head(3) = Eigen::Vector3d(0,0,0);
		calculate_theta_ray(rgb_img, box_2d, cam_to_img, theta_ray);
		// std::cout << "theta_ray in degree: " << theta_ray/M_PI*180 << std::endl;

		Eigen::Vector3d esti_location;
		esti_location.setZero();
		calculate_location_new(object_dim_cam, cam_to_img, box_2d, obj_local_rot, theta_ray, esti_location);
		std::cout << "object_dim_cam: " << object_dim_cam.transpose() << std::endl;
		std::cout << "cam_to_img: " << cam_to_img << std::endl;
		std::cout << "box_2d: " << box_2d.transpose() << std::endl;
		std::cout << "obj_local_rot: " << obj_local_rot << std::endl;
		std::cout << "theta_ray: " << theta_ray << std::endl;
		std::cout << "esti location: " << esti_location.transpose() << std::endl;
		
		if(esti_location.isZero())
		{
			std::cout << " bbox estimation failed " << std::endl;
			continue;
		}

		cuboid* det_obj_candidate = new cuboid();
		formulate_cuboid_param(det_obj_candidate, esti_location, obj_local_rot, object_dim_cam, Kalib);

		// step 3: compute visible corners and edge, prepare for score
		Eigen::Vector4d bbox_new = det_obj_candidate->bbox_2d;
		Eigen::MatrixXd box_corners_2d_float = det_obj_candidate->box_corners_2d;
		Eigen::MatrixXi visible_edge_pt_ids;
		Eigen::MatrixXd box_edges_visible;
		compute_obj_visible_edge(det_obj_candidate, visible_edge_pt_ids, box_edges_visible);

		// step 4: add score function : distance error and angle error
		// make sure new bbox are in the canny image, else, distance error is not accurate
		if( bbox_new(0) >= bbox_canny(0) && bbox_new(1) >= bbox_canny(1) &&   // xmin, ymin
			bbox_new(2) <= bbox_canny(2) && bbox_new(3) <= bbox_canny(3) ) // xmax, ymax
		{
			// compute distance error
			bool reweight_edge_distance = false; // if want to compare with all configurations. we need to reweight
			double sum_dist=0.0;
			MatrixXd box_corners_2d_float_shift(2, 8); // shift from whole image to canny image
			box_corners_2d_float_shift.row(0) = box_corners_2d_float.row(0).array() - bbox_canny(0);
			box_corners_2d_float_shift.row(1) = box_corners_2d_float.row(1).array() - bbox_canny(1);
			sum_dist = box_edge_sum_dists(dist_map, box_corners_2d_float_shift, visible_edge_pt_ids, reweight_edge_distance);
			double obj_diag_length = sqrt(obj_bbox(2) * obj_bbox(2) + obj_bbox(3) * obj_bbox(3)); // sqrt(width^2+height^2)
			double distance_error = std::min(fabs(sum_dist/obj_diag_length), 1e9);
			// std::cout <<"sum_dist: " << sum_dist << " distance_error: " << distance_error << std::endl;

			// compute angle error
			double angle_error = box_edge_angle_error(all_lines_merge_inobj, box_edges_visible);
			// double angle_error = box_edge_angle_error_new(all_lines_merge_inobj, box_edges_visible);
			// std::cout << "angle_error: " << angle_error << std::endl;

			// compute feature error = (dis_err + k*angle_err)/(1+k)
			double weight_angle = 0.5; 
			double feature_error = (1-weight_angle)*(distance_error/10.0) + weight_angle * (fmod(angle_error,M_PI)/M_PI);

			// compute obj-plane error
			double obj_plane_error = compute_obj_plane_error(esti_location, object_dim_cam, obj_local_rot, mvPlaneCoefficients);

			double weight_3d = 0.8; 
			if(!whether_add_plane_constraints)
				weight_3d = 0.0;
			combined_scores = (1-weight_3d)* feature_error+ weight_3d * obj_plane_error;

			det_obj_candidate->edge_distance_error = distance_error;
			det_obj_candidate->edge_angle_error = angle_error;
			det_obj_candidate->plane_obj_error = obj_plane_error;
			det_obj_candidate->overall_error = combined_scores;

			std::cout << "feature_error: " << feature_error << " obj_plane_error: " << obj_plane_error << std::endl;
			std::cout << "combined_scores: " << combined_scores << std::endl;
			
		}// loop if(in 2d box)
		else
		{
			det_obj_candidate->edge_distance_error = 1000;
			det_obj_candidate->edge_angle_error = 1000;
			det_obj_candidate->plane_obj_error = 1000;
			det_obj_candidate->overall_error = 1000;
			std::cout << "combined_scores: 10000, outside bbox"  << std::endl;
		} // loop if(in 2d box)

		// step 5: update selected cuboid with the min_error (distance error + angle error)
		if (combined_scores < min_com_error)
		{
			std::cout << "yaw update, combined_scores: " << combined_scores << " min_com_error: "<< min_com_error << std::endl;  
			final_obj_cuboid_cam = det_obj_candidate;
			min_com_error = combined_scores;
			// obj_mat_final_camera.block(0,0,3,3) = obj_local_rot;
			// obj_mat_final_camera.col(3).head(3) = esti_location;
			// obj_dim_final_camera = object_dim_cam;
			// obj_bbox_final_camera = Eigen::Vector4d(bbox_new(0), bbox_new(1), bbox_new(2)-bbox_new(0), bbox_new(3)-bbox_new(1));
		}
		// plot in camera coordinate
		if(whether_plot_sample_images)
		{
			cv::Mat plot_img = rgb_img.clone();
			plot_3d_box_with_loc_dim_camera(plot_img, Kalib, esti_location, object_dim_cam, obj_local_rot);
			cv::imshow("proposal image", plot_img);
			cv::waitKey(0);
		}
	} // loop yaw_id

	if(whether_plot_final_scores)
	{
		// Eigen::Matrix3d plot_obj_rot = obj_mat_final_camera.block(0,0,3,3);
		// Eigen::Vector3d plot_obj_loc = obj_mat_final_camera.col(3).head(3);
		// Eigen::Vector3d plot_obj_dim = obj_dim_final_camera;
		Eigen::Matrix3d plot_obj_rot = final_obj_cuboid_cam->obj_rot_camera;
		Eigen::Vector3d plot_obj_loc = final_obj_cuboid_cam->obj_loc_camera;
		Eigen::Vector3d plot_obj_dim = final_obj_cuboid_cam->obj_dim_camera;
		std::cout << "!!!!!!!final_location_camera:"  << plot_obj_loc.transpose()  << std::endl;
		cv::Mat plot_img = rgb_img.clone();
		plot_3d_box_with_loc_dim_camera(plot_img, Kalib, plot_obj_loc, plot_obj_dim, plot_obj_rot);
		cv::imshow("selection image", plot_img);
		cv::waitKey(0);              
	}

	if(final_obj_cuboid_cam->overall_error!=1000)
		single_object_candidate.push_back(final_obj_cuboid_cam);
}

void detect_cuboid_bbox::detect_cuboid_every_frame(cv::Mat& rgb_img, std::vector<cv::Mat>& mvPlaneCoefficients, 
		std::vector<ObjectSet>& frame_cuboid, std::string& output_file, std::string& output_img)
{
	int det_obj_num = detected_obj_name.size();
	std::vector<cuboid *> single_object_candidate;
	for (size_t object_id = 0; object_id < det_obj_num; object_id++)
	{
		double det_obj_yaw; // do not need
		std::string det_obj_name;
		Eigen::Vector4d det_obj_2d_bbox;
		Eigen::Vector3d det_obj_dim_ave;
		bool check_object_name = Get_Object_Input_SUNRGBD(int(object_id), det_obj_name, det_obj_2d_bbox, det_obj_dim_ave, det_obj_yaw);
		if (!check_object_name) // exclude object that we do not care
			continue;
		else
		{
			ca::Profiler::tictoc("object detection");
			detect_cuboid_with_bbox_constraints(rgb_img, det_obj_2d_bbox, det_obj_yaw, det_obj_dim_ave, mvPlaneCoefficients, single_object_candidate);
			ca::Profiler::tictoc("object detection");
		} // loop object name check

		if (whether_plot_final_scores || whether_save_final_image)
		{
			cv::Mat plot_img = rgb_img.clone();
			for (size_t i = 0; i < single_object_candidate.size(); i++)
			{
				cuboid* detected_cuboid = single_object_candidate[i];
				Eigen::Vector3d obj_dim_plot = detected_cuboid->obj_dim_camera;
				Eigen::Vector3d obj_loc_plot = detected_cuboid->obj_loc_camera;
				Eigen::Matrix3d obj_rot_plot = detected_cuboid->obj_rot_camera;
				plot_3d_box_with_loc_dim_camera(plot_img, Kalib, obj_loc_plot, obj_dim_plot, obj_rot_plot);
			}
			if (whether_plot_final_scores)
			{
				cv::imshow("final selection image", plot_img);
				cv::waitKey(0);
			}
			if (whether_save_final_image)
			{
				cv::imwrite(output_img, plot_img);
			}
		}
		if (whether_save_cam_obj_data)
		{
			ofstream online_stream_cube_multi;
			online_stream_cube_multi.open(output_file.c_str());

			for (size_t i = 0; i < single_object_candidate.size(); i++)
			{
				cuboid* detected_cuboid = single_object_candidate[i];
				Eigen::Vector4d raw_2d_objs = detected_cuboid->bbox_2d;
				// Eigen::Vector3d obj_dim_cam = obj_dim_multi_final_camera[i];
				// Eigen::Matrix4d obj_mat_final_camera = obj_mat_multi_final_camera[i];
				// Eigen::Vector3d obj_loc_cam = obj_mat_final_camera.col(3).head(3);
				// Eigen::Matrix3d obj_ori_cam = obj_mat_final_camera.block(0, 0, 3, 3);
				Eigen::Vector3d obj_dim_plot = detected_cuboid->obj_dim_camera;
				Eigen::Vector3d obj_loc_plot = detected_cuboid->obj_loc_camera;
				Eigen::Matrix3d obj_rot_plot = detected_cuboid->obj_rot_camera;
				// Eigen::Vector3d obj_rpy_cam;
				// quat_to_euler_zyx(Quaterniond(obj_ori_cam), obj_rpy_cam(0), obj_rpy_cam(1), obj_rpy_cam(2));
				// std::cout << "obj_mat_final_camera: " << obj_mat_final_camera << std::endl;
				// transfer to global coordinates
				Eigen::Vector3d obj_dim_world = detected_cuboid->obj_dim_camera;
				Eigen::Matrix4d obj_mat_final_camera;
				obj_mat_final_camera.setIdentity();
				obj_mat_final_camera.block(0, 0, 3, 3) = detected_cuboid->obj_rot_camera;
				obj_mat_final_camera.col(3).head(3) = detected_cuboid->obj_loc_camera;
				Eigen::Matrix4d obj_mat_final_world =  Twc * obj_mat_final_camera; // transToWolrd
				Eigen::Vector3d obj_loc_world = obj_mat_final_world.col(3).head(3);
				Eigen::Matrix3d mat_temp = obj_mat_final_world.block(0, 0, 3, 3);
				Eigen::Vector3d obj_rpy_world;
				quat_to_euler_zyx(Quaterniond(mat_temp), obj_rpy_world(0), obj_rpy_world(1), obj_rpy_world(2));
				std::cout << "object : " << detected_obj_name[i] << std::endl;
				std::cout << "esti obj_loc_world: " << obj_loc_world.transpose() << std::endl;
				std::cout << "esti obj_rpy_world: " << obj_rpy_world.transpose() << std::endl;
				std::cout << "esti obj_dim_world: " << obj_dim_world.transpose() << std::endl;
				// pay attention to the format
				online_stream_cube_multi << detected_obj_name[i] << " " << raw_2d_objs(0)
											<< " " << raw_2d_objs(1) << " " << raw_2d_objs(2) << " " << raw_2d_objs(3)
											<< " " << obj_loc_world(0) << " " << obj_loc_world(1) << " " << obj_loc_world(2)
											<< " " << obj_rpy_world(0) << " " << obj_rpy_world(1) << " " << obj_rpy_world(2)
											<< " " << obj_dim_world(1) << " " << obj_dim_world(0) << " " << obj_dim_world(2)
											<< " " << "\n";
			}	  // loop for object_id
			online_stream_cube_multi.close();
		}
	} // loop object_id


}
