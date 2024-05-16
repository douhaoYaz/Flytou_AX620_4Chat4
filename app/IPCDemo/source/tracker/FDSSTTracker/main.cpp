#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <time.h>

#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <vector>
#include "fdssttracker.hpp"

//#include <windows.h>
using namespace cv;
#include "fdssttracker.hpp"
void draw_rectangle(int event, int x, int y, int flags, void*);

Mat firstFrame;
Point previousPoint, currentPoint;
Rect bbox;
bool reDrawBox = false;
int main(int argc, char* argv[])
{
	bool HOG = true;
	bool FIXEDWINDOW = false;
	bool MULTISCALE = true;
	bool SILENT = true;
	bool LAB = true;
	// Create KCFTracker object
	FDSSTTracker tracker(HOG, FIXEDWINDOW, MULTISCALE, LAB);


	VideoCapture capture;
	Mat frame;
	//frame = capture.open("/home/rpdzkj/projects/testvideo1.mp4");
	frame = capture.open(argv[1]);
	if (!capture.isOpened())
	{
		printf("can not open ...\n");
		return -1;
	}
	
	int totalFrame=capture.get(CAP_PROP_FRAME_COUNT);  //获取总帧数
	int frameRate=capture.get(CAP_PROP_FPS);   //获取帧率
	double pauseTime=1000/frameRate; // 由帧率计算两幅图像间隔时间
	std::cout << "  " << capture.get(CAP_PROP_FRAME_WIDTH) << "x" <<
		capture.get(CAP_PROP_FRAME_HEIGHT) << "@" <<
		capture.get(CAP_PROP_FPS) << "FPS" << std::endl;

	//获取视频的第一帧,并框选目标
	capture.read(firstFrame);
	//resize(firstFrame, firstFrame,cv::Size(1920,1080));
	if (!firstFrame.empty())
	{
		moveWindow("output", 0, 0);
		namedWindow("output", WINDOW_AUTOSIZE);
		imshow("output", firstFrame);
		setMouseCallback("output", draw_rectangle, 0);
		waitKey();
	}
	//使用TrackerMIL跟踪
	//Ptr<TrackerMIL> tracker = TrackerMIL::create();
	//cv::Ptr<cv::legacy::Tracker> tracker = cv::legacy::TrackerMOSSE::create();  // 创建MOSSE跟踪器

	//Ptr<TrackerKCF> tracker = TrackerKCF::create();
	//Ptr<cv::legacy::TrackerMedianFlow> tracker = cv::legacy::TrackerMedianFlow::create();
	//Ptr<cv::legacy::TrackerBoosting> tracker= cv::legacy::TrackerBoosting::create();
	//Ptr<cv::legacy::TrackerTLD> tracker = cv::legacy::TrackerTLD::create();
	capture.read(frame);
	//resize(frame, frame, cv::Size(1920, 1080));
	//bbox.x = 470; bbox.y = 370; bbox.width = 350; bbox.height = 240;
	cvtColor(frame, frame, cv::COLOR_BGR2GRAY);
	tracker.init(bbox, frame);
	//tracker->init(frame, bbox);
	namedWindow("output", WINDOW_AUTOSIZE);
	while (capture.read(frame))
	{
		//resize(frame, frame, cv::Size(1920, 1080));
		//tracker->update(frame, bbox);
		if (reDrawBox) {
            frame.copyTo(firstFrame);
            waitKey();
            reDrawBox = false;
            tracker.setROI(bbox);
        }
		Mat gray;
		cvtColor(frame, gray, cv::COLOR_BGR2GRAY);
		auto t_start = std::chrono::high_resolution_clock::now();
		bbox = tracker.update(gray);
		auto t_end = std::chrono::high_resolution_clock::now();
		float total_inf = std::chrono::duration<float, std::milli>(t_end - t_start).count();
		std::cout << "Inference take: " << total_inf << " ms." << std::endl;
		rectangle(frame, bbox, Scalar(255, 0, 0), 2, 1);
		imshow("output", frame);

		if (capture.get(CAP_PROP_POS_FRAMES) == totalFrame) {capture.set(CAP_PROP_POS_FRAMES, 0);}

		if (waitKey(pauseTime) == 'q')
			break;
	}
	capture.release();
	destroyWindow("output");
	return 0;
}

//框选目标
void draw_rectangle(int event, int x, int y, int flags, void*)
{
	if (event == EVENT_LBUTTONDOWN)
	{
		previousPoint = Point(x, y);
	}
	else if (event == EVENT_MOUSEMOVE && (flags & EVENT_FLAG_LBUTTON))
	{
		Mat tmp;
		firstFrame.copyTo(tmp);
		currentPoint = Point(x, y);
		rectangle(tmp, previousPoint, currentPoint, Scalar(0, 255, 0, 0), 1, 8, 0);
		imshow("output", tmp);
	}
	else if (event == EVENT_LBUTTONUP)
	{
		bbox.x = previousPoint.x;
		bbox.y = previousPoint.y;
		bbox.width = abs(previousPoint.x - currentPoint.x);
		bbox.height = abs(previousPoint.y - currentPoint.y);
	}
	else if (event == EVENT_RBUTTONUP)
	{
		destroyWindow("output");
	}
	else if (event == EVENT_LBUTTONDBLCLK) {
        reDrawBox = true;
    }
}
