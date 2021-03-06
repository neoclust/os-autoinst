
#include <stdio.h>
#include <iostream>
#include <exception>

#include "opencv2/core/core.hpp"
#include "opencv2/features2d/features2d.hpp"
#include "opencv2/highgui/highgui.hpp"
#include "opencv2/calib3d/calib3d.hpp"
#include "opencv2/nonfree/features2d.hpp"
#include <opencv2/imgproc/imgproc.hpp>

#include "tinycv.h"

#define DEBUG 0

#define VERY_DIFF 0.0
#define VERY_SIM 1000000.0


using namespace cv;


struct Image {
  cv::Mat img;
};

// make box lines eq 0° or 90°
inline Point2f normalize_aspect(Point2f in, Point2f x, Point2f y) {
	Point2f out(in);
	out.y += y.y;
	out.y /= 2;
	out.x += x.x;
	out.x /= 2;
	return out;
}

int MyErrorHandler(int status, const char* func_name, const char* err_msg, const char* file_name, int line, void*) {
	// suppress error msg's
	return 0;
}

std::vector<char> str2vec(std::string str_in) {
	std::vector<char> out(str_in.data(), str_in.data() + str_in.length());
	return out;
}

std::vector<int> search_SURF(std::string str_scene, std::string str_object) {
	cvSetErrMode(CV_ErrModeParent);
	cvRedirectError(MyErrorHandler);

	std::vector<char> data_scene = str2vec(str_scene);
	std::vector<char> data_object = str2vec(str_object);

	Mat img_object = imdecode(Mat(data_object), CV_LOAD_IMAGE_COLOR );
	Mat img_scene =  imdecode(Mat(data_scene),  CV_LOAD_IMAGE_COLOR );

	if( !img_object.data || !img_scene.data ) {
		std::cerr<< "Error reading images" << std::endl;
		throw(std::exception());
	}

	//-- Step 1: Detect the keypoints using SURF Detector
	int minHessian = 400;

	SurfFeatureDetector detector( minHessian );

	std::vector<KeyPoint> keypoints_object, keypoints_scene;

	detector.detect( img_object, keypoints_object );
	detector.detect( img_scene, keypoints_scene );

	//-- Step 2: Calculate descriptors (feature vectors)
	SurfDescriptorExtractor extractor;

	Mat descriptors_object, descriptors_scene;

	extractor.compute( img_object, keypoints_object, descriptors_object );
	extractor.compute( img_scene, keypoints_scene, descriptors_scene );

	//-- Step 3: Matching descriptor vectors using FLANN matcher
	FlannBasedMatcher matcher;
	std::vector< DMatch > matches;
	matcher.match( descriptors_object, descriptors_scene, matches );

	double max_dist = 0; double min_dist = 100;

	//-- Quick calculation of max and min distances between keypoints
	for( int i = 0; i < descriptors_object.rows; i++ ) {
		double dist = matches[i].distance;
		if( dist < min_dist ) min_dist = dist;
		if( dist > max_dist ) max_dist = dist;
	}

	//printf("-- Max dist : %f \n", max_dist );
	//printf("-- Min dist : %f \n", min_dist );

	//-- Draw only "good" matches (i.e. whose distance is less than 3*min_dist )
	std::vector< DMatch > good_matches;

	for( int i = 0; i < descriptors_object.rows; i++ ) {
		if( matches[i].distance < 3*min_dist ) {
			good_matches.push_back( matches[i]);
		}
	}

	Mat img_matches;
	drawMatches( img_object, keypoints_object, img_scene, keypoints_scene,
			   good_matches, img_matches, Scalar::all(-1), Scalar::all(-1),
			   vector<char>(), DrawMatchesFlags::NOT_DRAW_SINGLE_POINTS );


	//-- Localize the object from img_1 in img_2
	std::vector<Point2f> obj;
	std::vector<Point2f> scene;

	for( size_t i = 0; i < good_matches.size(); i++ ) {
		//-- Get the keypoints from the good matches
		obj.push_back( keypoints_object[ good_matches[i].queryIdx ].pt );
		scene.push_back( keypoints_scene[ good_matches[i].trainIdx ].pt );
	}

	try {
		Mat H = findHomography( obj, scene, CV_RANSAC );

		//-- Get the corners from the image_1 ( the object to be "detected" )
		std::vector<Point2f> obj_corners(4);
		obj_corners[0] = cvPoint(0,0); obj_corners[1] = cvPoint( img_object.cols, 0 );
		obj_corners[2] = cvPoint( img_object.cols, img_object.rows ); obj_corners[3] = cvPoint( 0, img_object.rows );
		std::vector<Point2f> scene_corners(4);

		perspectiveTransform( obj_corners, scene_corners, H);

		#if DEBUG
		//-- Draw lines between the corners (the mapped object in the scene - image_2 )
		Point2f offset( (float)img_object.cols, 0);
		line( img_matches, scene_corners[0] + offset, scene_corners[1] + offset, Scalar( 0, 255, 0), 4 );
		line( img_matches, scene_corners[1] + offset, scene_corners[2] + offset, Scalar( 0, 255, 0), 4 );
		line( img_matches, scene_corners[2] + offset, scene_corners[3] + offset, Scalar( 0, 255, 0), 4 );
		line( img_matches, scene_corners[3] + offset, scene_corners[0] + offset, Scalar( 0, 255, 0), 4 );
		#endif

		// normalize aspect
		std::vector<Point2f> match_box(4);
		match_box[0] = normalize_aspect(scene_corners[0], scene_corners[3], scene_corners[1]);
		match_box[2] = normalize_aspect(scene_corners[2], scene_corners[1], scene_corners[3]);
		match_box[1] = Point2f(match_box[0].x, match_box[2].y);
		match_box[3] = Point2f(match_box[2].x, match_box[0].y);

		#if DEBUG
		line( img_matches, match_box[0] + offset, match_box[1] + offset, Scalar( 255, 0, 0), 4 );
		line( img_matches, match_box[1] + offset, match_box[2] + offset, Scalar( 255, 0, 0), 4 );
		line( img_matches, match_box[2] + offset, match_box[3] + offset, Scalar( 255, 0, 0), 4 );
		line( img_matches, match_box[3] + offset, match_box[0] + offset, Scalar( 255, 0, 0), 4 );
		#endif

		//-- Show detected matches
		//printf("match at %ix%i\n", int(match_box[0].x), int(match_box[0].y));
		std::vector<int> outvec(4);
		outvec[0] = int(match_box[0].x);
		outvec[1] = int(match_box[0].y);
		outvec[2] = int(match_box[2].x);
		outvec[3] = int(match_box[2].y);

		#if DEBUG
		imwrite("debug.ppm", img_matches);
		#endif
		return outvec;
	}
	catch(const cv::Exception& e) {
		if(e.code == -215) {
			std::vector<int> vec_out(0);
			return vec_out;
			//printf("did not match\n");
		}
		else {
			throw(e);
		}
	}

}

std::vector<int> search_TEMPLATE(const Image *scene, const Image *object, double &similarity) {
  cvSetErrMode(CV_ErrModeParent);
  cvRedirectError(MyErrorHandler);
 
  std::vector<int> outvec(4);

  if (scene->img.empty() || object->img.empty() ) {
    std::cerr << "Error reading images. Scene or object is empty." << std::endl;
    throw(std::exception());
  }

  // Calculate size of result matrix and create it If scene is W x H
  // and object is w x h, res is (W - w + 1) x ( H - h + 1)
  int res_width  = scene->img.cols - object->img.cols + 1;
  int res_height = scene->img.rows - object->img.rows + 1;
  if (res_width <= 0 || res_height <= 0) {
     similarity = 0;
     outvec[0] = 0;   
     outvec[1] = 0;
     outvec[2] = 0;
     outvec[3] = 0;
     return outvec;
  }
  Mat res = Mat::zeros(res_height, res_width, CV_32FC1);

  // Perform the matching. Info about algorythm:
  // http://docs.opencv.org/trunk/doc/tutorials/imgproc/histograms/template_matching/template_matching.html
  // http://docs.opencv.org/modules/imgproc/doc/object_detection.html
  matchTemplate(scene->img, object->img, res, CV_TM_CCOEFF_NORMED);

  // Localizing the best math with minMaxLoc
  double minval, maxval;
  Point  minloc, maxloc;
  minMaxLoc(res, &minval, &maxval, &minloc, &maxloc, Mat());

#if DEBUG
#warning FIXME: modifies original image
  Mat s = scene->img;
  rectangle(s, Point(maxloc.x, maxloc.y),
	    Point(maxloc.x + object->img.cols, maxloc.y + object->img.rows),
	    CV_RGB(255,0,0), 3);
  imwrite("debug.ppm", scene->img);
#endif

  outvec[0] = int(maxloc.x);
  outvec[1] = int(maxloc.y);
  outvec[2] = int(maxloc.x + object->img.cols);
  outvec[3] = int(maxloc.y + object->img.rows);

  similarity = maxval;

  return outvec;
}


// Use Peak signal-to-noise ratio to check the similarity between two
// images.
//
// This method calculate the mean square error, but returns a measure
// in dB units. If the images are the same, it return 0.0, and if the
// images are the same but with different compression ration (or noise
// when the input is from analog video), the range is between 30 and
// 50. Maybe higher is the quality is bad. 
//
// Source (C&P):
// http://docs.opencv.org/doc/tutorials/highgui/video-input-psnr-ssim/video-input-psnr-ssim.html

double getPSNR(const Mat& I1, const Mat& I2)
{
  Mat s1;
  absdiff(I1, I2, s1);       // |I1 - I2|
  s1.convertTo(s1, CV_32F);  // cannot make a square on 8 bits
  s1 = s1.mul(s1);           // |I1 - I2|^2

  Scalar s = sum(s1);        // sum elements per channel

  double sse = s.val[0] + s.val[1] + s.val[2]; // sum channels

  if( sse <= 1e-10) // for small values return zero
    return VERY_SIM;
  else {
    double mse  = sse / (double)(I1.channels() * I1.total());
    double psnr = 10.0 * log10((255 * 255) / mse);
    return psnr;
  }
}


void image_destroy(Image *s)
{
  delete(s);
}

Image *image_read(const char *filename)
{
  Image *image = new Image;
  image->img = imread(filename, CV_LOAD_IMAGE_COLOR);
  if (!image->img.data) {
    std::cerr << "Could not open image " << filename << std::endl;
    return 0L;
  }
  return image;
}

bool image_write(Image *s, const char *filename)
{
  if (!imwrite(filename, s->img)) {
    std::cerr << "Could not write image " << filename << std::endl;
    return false;
  }
  return true;
}

static std::vector<uchar> convert_to_ppm(const Mat &s, int &header_length)
{
  vector<uchar> buf;
  if (!imencode(".ppm", s, buf)) {
    fprintf(stderr, "convert_to_ppm failed\n");
    header_length = 0;
    return buf;
  }
  
  const char *cbuf = reinterpret_cast<const char*> (&buf[0]);
  const char *cbuf_start = cbuf;
  // the perl code removed the header before md5, 
  // so we need to do the same
  cbuf = strchr(cbuf, '\n') + 1; // "P6\n";
  cbuf = strchr(cbuf, '\n') + 1; // "800 600\n";
  cbuf = strchr(cbuf, '\n') + 1; // "255\n";

  header_length = cbuf - cbuf_start;
  return buf;
}

Image *image_copy(Image *s)
{
  Image *ni = new Image();
  s->img.copyTo(ni->img);
  return ni;
}

long image_xres(Image *s)
{
  return s->img.cols;
}

long image_yres(Image *s)
{
  return s->img.rows;
}

/* 
 * in the image s replace all pixels in the given range with 0 - in place
 */
void image_replacerect(Image *s, long x, long y, long width, long height)
{
  rectangle(s->img, Rect(x, y, width, height), Scalar(0), CV_FILLED);
}

/* copies the given range into a new image */
Image *image_copyrect(Image *s, long x, long y, long width, long height)
{
  // avoid an exception
  if ( y+height > s->img.rows || x+width > s->img.cols ) {
    printf("copyrect: out of range\n");
    return 0;
  }

  Image *n = new Image;
  Mat tmp = Mat(s->img, Range(y, y+height), Range(x,x+width));
  n->img = tmp.clone();
  return n;
}

// in-place op: change all values to 0 (if below threshold) or 255 otherwise
void image_threshold(Image *s, int level)
{
  int header_length;
  vector<uchar> buf = convert_to_ppm(s->img, header_length);

  vector<uchar>::iterator it = buf.begin() + header_length;
  for (; it != buf.end(); ++it) {
    *it = (*it < level) ? 0 : 0xff;
  }
  s->img = imdecode(buf, 1);
}

std::vector<float> image_avgcolor(Image *s)
{
  Scalar t = mean(s->img);

  vector<float> f;
  f.push_back(t[2] / 255.0); // Red
  f.push_back(t[1] / 255.0); // Green
  f.push_back(t[0] / 255.0); // Blue

  return f;
}

std::vector<int> image_search(Image *s, Image *needle, double &similarity)
{
  return search_TEMPLATE(s, needle, similarity);
}


Image *image_scale(Image *a, long width, long height)
{
  Image *n = new Image;
  n->img = Mat(height, width, a->img.type());
  resize(a->img, n->img, n->img.size());

  return n;
}

double image_similarity(Image *a, Image*b)
{
  if (a->img.rows != b->img.rows)
    return VERY_DIFF;

  if (a->img.cols != b->img.cols)
    return VERY_DIFF;

  return getPSNR(a->img, b->img);
}
