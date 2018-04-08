#include <Windows.h>
#include <iostream>
#include <cstdio>
#include <Kinect.h>
#include <time.h>
#include <opencv2\core.hpp>
#include <opencv2\highgui.hpp>
#include <opencv2\imgproc.hpp>
#include <math.h>

using	namespace	std;
using	namespace	cv;

Vec3b	COLOR_TABLE[] = { Vec3b(255,0,0),Vec3b(0,255,0),Vec3b(0,0,255),Vec3b(255,255,255),Vec3b(0,0,0) ,Vec3b{ 255,0,255 } };
enum { BLUE, GREEN, RED, WHITE, BLACK, YELLOW };

const	int	MIN_DISTANCE = 25;				//The minimum distance of the screen rendering area in the depth map, in millimeters   深度图中屏幕渲染区最小距离,单位为毫米
const	int	MAX_DISTANCE = 250;				//The maximum distance of the screen rendering area in the depth map, in millimeters   深度图中屏幕渲染区最大距离,单位为毫米
const	int	DEPTH_AVE_E = 10;				//After finding the average screen depth, use this average depth to filter the threshold of illegal points in millimeters   求屏幕平均深度后，用此平均深度去筛非法点的阈值,单位为毫米
const	int	FINGER_DEPTH_E = 100;				//Determines whether the finger touches the screen in millimeters.   判断手指是否触碰屏幕的值，单位为毫米
const	int	FINGER_DISTANCE_E = 200;			//The value of the sudden increase in finger position, in pixels   判断手指位置突增的值,单位为像素
const	int	FINGER_MOVE_E = 2;				//Judging the tiny jitter of the finger in pixels   判断手指微小抖动的值,单位为像素
const	int	BGR_E = 45;					//Color error at screen recognition, maximum 255   屏幕识别时的彩色误差,最大255
const	int	AUTO_SEC = 5;					//The number of read seconds for automatically identifying the base point must not exceed the capacity of the lower VIS array   自动选取识别基点的读秒数,不得超过下方VIS数组的容量
int	COLORHEIGHT = 0, COLORWIDTH = 0;
int	DEPTHWIDTH = 0, DEPTHHEIGHT = 0;
bool	VIS[100] = { false };					//The array used to read the seconds   用来读秒的数组

bool	find_edge(const Mat &, const Point &, int &, int &, int &, int &);
bool	check_depth_coordinate(int, int);
void	draw_screen(Mat &, int &, int &, int &, int &);
int	main(void)
{
	IKinectSensor	* mySensor = nullptr;
	GetDefaultKinectSensor(&mySensor);
	mySensor->Open();

	IFrameDescription	* myDescription = nullptr;

	IColorFrameSource	* myColorSource = nullptr;
	IColorFrameReader	* myColorReader = nullptr;
	IColorFrame		* myColorFrame = nullptr;
	mySensor->get_ColorFrameSource(&myColorSource);
	myColorSource->get_FrameDescription(&myDescription);
	myDescription->get_Height(&COLORHEIGHT);
	myDescription->get_Width(&COLORWIDTH);
	myColorSource->OpenReader(&myColorReader);			//The above is the preparation of the Color frame, open Reader directly   以上为Color帧的准备，直接开好Reader


	IDepthFrameSource	* myDepthSource = nullptr;
	IDepthFrameReader	* myDepthReader = nullptr;
	IDepthFrame		* myDepthFrame = nullptr;
	mySensor->get_DepthFrameSource(&myDepthSource);
	myDepthSource->get_FrameDescription(&myDescription);
	myDescription->get_Height(&DEPTHHEIGHT);
	myDescription->get_Width(&DEPTHWIDTH);
	myDepthSource->OpenReader(&myDepthReader);			//The above is the preparation of Depth frame, directly opened Reader   以上为Depth帧的准备，直接开好Reader


	IBodyFrameSource	* myBodySource = nullptr;
	IBodyFrameReader	* myBodyReader = nullptr;
	IBodyFrame		* myBodyFrame = nullptr;
	mySensor->get_BodyFrameSource(&myBodySource);
	myBodySource->OpenReader(&myBodyReader);			//The above preparation for the Body frame, directly opened Reader   以上为Body帧的准备，直接开好Reader


	ICoordinateMapper	* myMapper = nullptr;
	mySensor->get_CoordinateMapper(&myMapper);			//Preparation of Maper   Maper的准备



	int	colorLeft = 0, colorRight = 0, colorUp = 0, colorButtom = 0;
	int	depthLeft = 0, depthRight = 0, depthUp = 0, depthButtom = 0;
	DepthSpacePoint	depthLeftUp = { 0,0 }, depthRightButtom = { 0,0 };

	bool	gotColorCenter = false;
	bool	gotColorScreen = false;
	bool	gotDepthScreen = false;
	bool	gotFinger = false;
	bool	gotFrontFinger = false;
	int	basePoint = 5;
	Point	center = { COLORWIDTH / 2,COLORHEIGHT / 2 };

	Point	fingerPoint = { 0,0 };
	Point	frontFingerPoint = { -1,-1 };

	int	screenDepth = 0;
	bool	click = false;
	bool	failMessage = false;

	time_t	startTime = clock();
	time_t	nextUndoTime = 0;
	time_t	curTime = 0;
	bool	undoFlag = true;
	bool	work = false;
	bool	hint = true;
	bool	firstRun = true;

	while (1)
	{
		Mat	colorImg(COLORHEIGHT, COLORWIDTH, CV_8UC4);									//Reading color data   读彩色数据
		while (myColorReader->AcquireLatestFrame(&myColorFrame) != S_OK);
		myColorFrame->CopyConvertedFrameDataToArray(COLORHEIGHT * COLORWIDTH * 4, colorImg.data, ColorImageFormat_Bgra);


		Mat	depthImg(DEPTHHEIGHT, DEPTHWIDTH, CV_8UC3);									//Reading depth data   读深度数据
		UINT16	* depthData = new UINT16[DEPTHHEIGHT * DEPTHWIDTH];
		while (myDepthReader->AcquireLatestFrame(&myDepthFrame) != S_OK);
		myDepthFrame->CopyFrameDataToArray(DEPTHHEIGHT * DEPTHWIDTH, depthData);


		int	bodyCount;													//Reading body data   读身体数据
		myBodySource->get_BodyCount(&bodyCount);
		IBody	** bodyArr = new IBody *[bodyCount];
		for (int i = 0; i < bodyCount; i++)
			bodyArr[i] = nullptr;
		while (myBodyReader->AcquireLatestFrame(&myBodyFrame) != S_OK);
		myBodyFrame->GetAndRefreshBodyData(bodyCount, bodyArr);

		bool	mapSuccessed = true;
		//Get the status of the right hand (actually the left hand, screen mirror) from the body data and determine whether to withdraw   从身体数据中获取右手(实际上是左手)的状态,判断是否撤销
		bool	undo = false;
		double	spineHeight = 0, handHeight = 0;
		for (int i = 0; i < bodyCount; i++)
		{
			BOOLEAN		isTracked = false;
			if (bodyArr[i]->get_IsTracked(&isTracked) == S_OK && isTracked)
			{
				HandState	rightState;
				bodyArr[i]->get_HandRightState(&rightState);
				if (rightState == HandState_Closed)									//Definitely to call for revocation   确定要调用撤销
					undo = true;

				Joint	* jointArr = new Joint[JointType_Count];
				bodyArr[i]->GetJoints(JointType_Count, jointArr);

				if (jointArr[JointType_SpineShoulder].TrackingState == TrackingState_Tracked)
					spineHeight = jointArr[JointType_SpineShoulder].Position.Y;
				if (jointArr[JointType_HandRight].TrackingState == TrackingState_Tracked)
					handHeight = jointArr[JointType_HandRight].Position.Y;

				delete[] jointArr;
			}
		}

		if (firstRun)
		{
			printf("Please place the Kinect directly in front of the projection surface, as far as possible parallel to the projection surface, and then set the projection surface as a full-color full-screen, %d seconds will be based on the color of the projection surface recognition, please see the recognition results in %d seconds\n", AUTO_SEC,AUTO_SEC);
			//printf("请将Kinect摆放在投影面的正前方，尽量平行于投影面，然后将投影面设为纯色的全屏，%d秒后将基于此颜色进行投影面识别,请于%d秒后查看识别结果\n", AUTO_SEC, AUTO_SEC);
			startTime = clock();
			firstRun = false;
			fill(VIS,VIS + 100,false);
		}

		//Automatic countdown selection screen recognition point   自动读秒选取屏幕识别点
		if (!gotColorCenter)
		{
			circle(colorImg, center, 15, COLOR_TABLE[RED], -1);					//Draw the center of the screen   画出屏幕中心点

			firstRun = false;
			curTime = clock();
			int	index = AUTO_SEC - (curTime - startTime) / CLOCKS_PER_SEC;
			if (index >= 0 && index <= AUTO_SEC)
			{
				if (!VIS[index])
				{
					printf("%d\n", index);
					VIS[index] = true;
				}
				if (!index)
				{
					gotColorCenter = true;
					puts("");
				}
			}
			goto	release;									//To Release once, because the point has been drawn to the screen at this time, the recognition will be disturbed by the painted points   要Release一次，因为此时已经将点画到了画面上，识别会被画上的点干扰
		}

		//If the screen has not yet been identified, the screen is found based on the selected center point   若目前还没有识别出屏幕，则基于选中的中心点来找屏幕
		if (!gotColorScreen && gotColorCenter)
			if (!find_edge(colorImg, center, colorLeft, colorRight, colorUp, colorButtom))
				goto	release;
			else
				gotColorScreen = true;


		if (gotColorScreen)
			draw_screen(colorImg, colorLeft, colorRight, colorUp, colorButtom);							//So far the color screen has been found, draw the screen border   至此彩色屏幕已经找到了，画出屏幕边框
		mapSuccessed = true;
																		//Convert the coordinates of the screen identified in the color data to the depth space, and find the average distance to the screen, only once   把彩色数据里识别出的屏幕的坐标转换到深度空间,并求出到屏幕的平均距离,只求一次											
		
		if (!gotDepthScreen && !failMessage)
		{

			DepthSpacePoint		* colorMapArray = new DepthSpacePoint[COLORHEIGHT * COLORWIDTH];
			myMapper->MapColorFrameToDepthSpace(DEPTHHEIGHT * DEPTHWIDTH, depthData, COLORHEIGHT * COLORWIDTH, colorMapArray);

			//Check whether the converted coordinates are valid   检查转换后的坐标是否合法
			if (check_depth_coordinate(colorMapArray[colorUp * COLORWIDTH + colorLeft].X, colorMapArray[colorUp * COLORWIDTH + colorLeft].Y))
				depthLeftUp = colorMapArray[colorUp * COLORWIDTH + colorLeft];
			else
				mapSuccessed = false;
			if (check_depth_coordinate(colorMapArray[colorButtom * COLORWIDTH + colorRight].X, colorMapArray[colorButtom * COLORWIDTH + colorRight].Y))
				depthRightButtom = colorMapArray[colorButtom * COLORWIDTH + colorRight];
			else
				mapSuccessed = false;
			depthLeft = (int)depthLeftUp.X, depthRight = (int)depthRightButtom.X, depthUp = (int)depthLeftUp.Y, depthButtom = (int)depthRightButtom.Y;


			if (!depthLeft || !depthRight || !depthUp || !depthButtom)							//Check if the converted coordinates are 0   检查转换后的坐标是否为0
				mapSuccessed = false;

			delete[] colorMapArray;
			//At this point, the coordinates are legal, and it is judged whether the depth is legal    至此坐标合法，开始判断深度是否合法
			if (mapSuccessed)												//The first pass scan calculates the average of all point distances   第一遍扫描求出所有点距离的平均值
			{
				int	sum = 0;
				int	count = 0;
				for (int i = depthUp + 10; i < depthButtom - 10; i++)
					for (int j = depthLeft + 10; j < depthRight - 10; j++)
					{
						int	index = i * DEPTHWIDTH + j;
						if (check_depth_coordinate(j, i))
						{
							sum += depthData[index];
							count++;
						}

					}
				if (!count)
					mapSuccessed = false;
				else
					screenDepth = sum / count;

				if (mapSuccessed)											//The second pass screens away from the illegal point based on the above average   第二遍根据上面求出的平均值，筛去距离非法的点
				{
					sum = count = 0;
					for (int i = depthUp + 10; i < depthButtom - 10; i++)
						for (int j = depthLeft + 10; j < depthRight - 10; j++)
						{
							int	index = i * DEPTHWIDTH + j;
							if (check_depth_coordinate(j, i) && (depthData[index] - screenDepth) <= DEPTH_AVE_E)
							{
								sum += depthData[index];
								count++;
							}
						}
					if (!count)
						mapSuccessed = false;
					else
					{
						screenDepth = sum / count;								//Finally find the screen average distance   最后求出屏幕平均距离
						if (!screenDepth)
							mapSuccessed = false;
						else											//At this point, the coordinates and depth are legal   至此坐标、深度都合法
						{
							gotDepthScreen = true;
							printf("The distance from the camera to the projection plane is =%.2lf meters\n\n", (double)screenDepth / 1000);
							//printf("摄像头到投影面的距离=%.2lf米\n\n", (double)screenDepth / 1000);
						}
					}
				}
			}
		}

		//If the screen distance calculation fails, output the prompt message   若屏幕距离计算失败，则输出提示信息
		if (gotColorScreen && !gotDepthScreen && !failMessage)
		{
			printf("The distance to the projection surface was unsuccessfully calculated. Please make sure that the Kinect position is 2-3 meters in front of the projection surface, parallel to the projection surface, and then press F5 to recognize again.\n\n");
			//printf("未成功计算出到投影面的距离，请确保Kinect位置在投影面正前方2-3米处，与投影面平行，然后按F5重新识别\n\n");
			failMessage = true;
		}

		//If the above conversion is successful, render the depth map, draw the screen, and find the fingertip inside.   若以上转换成功，则对深度图进行渲染,然后画出屏幕,最后在里面找指尖
		gotFinger = false;
		fingerPoint = { 0,0 };
		if (mapSuccessed)
		{
			for (int i = depthUp; i < depthButtom; i++)									//First pass rendering   第一遍渲染
				for (int j = depthLeft; j < depthRight; j++)
				{
					int	index = i * DEPTHWIDTH + j;
					if (screenDepth - depthData[index] >= MIN_DISTANCE && screenDepth - depthData[index] <= MAX_DISTANCE)
						depthImg.at<Vec3b>(i, j) = COLOR_TABLE[GREEN];
					else
						depthImg.at<Vec3b>(i, j) = COLOR_TABLE[BLACK];
				}

			Mat	temp;													//Render the second pass to remove the edges   第二遍渲染,去除边缘
			depthImg.copyTo(temp);
			for (int i = depthUp; i < depthButtom; i++)
				for (int j = depthLeft; j < depthRight; j++)
				{
					bool	edge = false;
					for (int q = -1; q <= 1; q++)
						for (int e = -1; e <= 1; e++)
							if (check_depth_coordinate(i + q,j + 3) && depthImg.at<Vec3b>(i + q, j + e) == COLOR_TABLE[BLACK])
							{
								edge = true;
								temp.at<Vec3b>(i, j) = COLOR_TABLE[BLACK];
								goto	label;
							}
				label:
					if (!edge)
						temp.at<Vec3b>(i, j) = COLOR_TABLE[GREEN];
				}

			depthImg = temp;

			draw_screen(depthImg, depthLeft, depthRight, depthUp, depthButtom);						//After rendering, start drawing the edges of the screen   渲染完后，开始画屏幕边缘


			for (int i = depthUp; i <= depthButtom; i++)									//Fingertip recognition 2, take the highest point as the fingertip   指尖识别2，取最高点为指尖
			{
				gotFinger = false;
				for (int j = depthLeft; j < depthRight; j++)
					if (depthImg.at<Vec3b>(i, j) == COLOR_TABLE[GREEN])
					{
						fingerPoint.x = j;
						fingerPoint.y = i;
						gotFinger = true;
						break;
					}
				if (gotFinger)
					break;
			}
			//Determine if the position of the fingertip is within the screen frame   判断指尖的位置是否在屏幕框内
			if (!(fingerPoint.x >= depthLeft && fingerPoint.y <= depthRight && fingerPoint.y >= depthUp && fingerPoint.y <= depthButtom))
				gotFinger = false;
		}

		//If fingertips are found, handle the fingertips   如果找到了指尖，则对指尖进行处理
		if (gotFinger)
		{
			if (frontFingerPoint.x == -1)
				frontFingerPoint = fingerPoint;
			//Judging whether there is a distance increase point   判断是否有距离突增点
			if (sqrt(pow(fingerPoint.x - frontFingerPoint.x, 2) + pow(fingerPoint.y - frontFingerPoint.y, 2)) >= FINGER_DISTANCE_E
				&& depthImg.at<Vec3b>(frontFingerPoint.y, frontFingerPoint.x) == COLOR_TABLE[GREEN])
				fingerPoint = frontFingerPoint;
			//To determine whether the movement is very tiny   判断是否移动很微小
			if ((abs(fingerPoint.x - frontFingerPoint.x) <= FINGER_MOVE_E && abs(fingerPoint.y - frontFingerPoint.y) <= FINGER_MOVE_E))
				fingerPoint = frontFingerPoint;
			else
				frontFingerPoint = fingerPoint;
		}


		//Call the mouse   调用鼠标
		if (gotFinger && work)
		{
			fingerPoint.y += 1;
			int	fingerDepth = depthData[fingerPoint.y * DEPTHWIDTH + fingerPoint.x];
			fingerPoint.y -= 8;
			if (fingerPoint.y < 0)
				fingerPoint.y = 0;
			if (abs(fingerDepth - screenDepth) <= FINGER_DEPTH_E)
			{
				circle(depthImg, fingerPoint, 3, COLOR_TABLE[RED], -1);
				mouse_event(MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_MOVE | MOUSEEVENTF_LEFTDOWN, 65535 * (depthRight - fingerPoint.x) / (depthRight - depthLeft),65535 * (fingerPoint.y - depthUp) / (depthButtom - depthUp), 0, 0);
				click = true;
			}
			else
			{
				mouse_event(MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_LEFTUP, 65535 * (depthRight - fingerPoint.x) / (depthRight - depthLeft), 65535 * (fingerPoint.y - depthUp) / (depthButtom - depthUp), 0, 0);
				click = false;
			}
			gotFrontFinger = true;
		}
		//Fix once for mouse loss   对鼠标丢失修正一次
		else	if (gotFrontFinger && work)
		{
			fingerPoint = frontFingerPoint;
			int	fingerDepth = depthData[fingerPoint.y * DEPTHWIDTH + fingerPoint.x];
			if (click)
			{
				circle(depthImg, fingerPoint, 3, COLOR_TABLE[RED], -1);
				mouse_event(MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_MOVE | MOUSEEVENTF_LEFTDOWN, 65535 * (depthRight - fingerPoint.x) / (depthRight - depthLeft),65535 * (fingerPoint.y - depthUp) / (depthButtom - depthUp), 0, 0);
				click = true;
			}
			else
			{
				mouse_event(MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_LEFTUP, 65535 * (depthRight - fingerPoint.x) / (depthRight - depthLeft), 65535 * (fingerPoint.y - depthUp) / (depthButtom - depthUp), 0, 0);
				click = false;
			}
			gotFrontFinger = false;
		}
		if (!gotFinger)
			mouse_event(MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_LEFTUP, 65535 / 2, 65535 / 2, 0, 0);




		//Judging whether it is at the window where Flag equal true   判断是否到了将Flag置真的窗口
		curTime = clock();
		if (curTime >= nextUndoTime && !undoFlag)
			undoFlag = true;
		//Call keyboard undo   调用键盘撤销
		if (undo && undoFlag && handHeight > spineHeight)
		{
			keybd_event(VK_CONTROL, 0, 0, 0);
			keybd_event(0x5A, 0, 0, 0);
			keybd_event(VK_CONTROL, 0, KEYEVENTF_KEYUP, 0);
			keybd_event(0x5A, 0, KEYEVENTF_KEYUP, 0);
			nextUndoTime = curTime + 500;											//Set Flag to FALSE and set the next window with Flag true after 500ms   将Flag置为假，并将下一个将Flag置真的窗口设定在500ms后
			undoFlag = false;
		}


		for (int i = 0; i < DEPTHHEIGHT; i++)											//Screen mirror conversion   画面做镜像转换
			for (int j = 0, k = DEPTHWIDTH - 1; j < DEPTHWIDTH / 2 + 1; j++, k--)
				swap(depthImg.at<Vec3b>(i, j), depthImg.at<Vec3b>(i, k));


		if (gotDepthScreen && !failMessage)
		{
			failMessage = true;
			printf("Recognition is complete. Please check the COLOR window to make sure that the edge of the projection surface matches the red box. Then check the DEPTH window to see if there is a green image.\n");
			printf("The appearance of the green image indicates that the corresponding position of the Kinect camera is too close to the screen. For example, the green point appears at the lower left of the screen, which means that the left side of the camera is closer to the screen than the right side, and the lower side is closer to the screen than the top.\n");
			printf("Please fine-tune the position of the Kinect according to the image until the green point disappears completely. At this time, the Kinect should be substantially parallel to the projection surface. Then press F5 to re-correct.\n");
			printf("Please make sure the screen is clean and the recognition area is correct and then activate the program. After the activation, the mouse will be invoked. The presence of the green dot will interfere with the mouse position\n");
			printf("Activate the program by pressing F1.\n\n");

			//printf("识别完成.请查看查看COLOR窗口，确保投影面的边缘和红色框基本吻合，然后查看DEPTH窗口，检查是否有绿色的图像出现.\n");
			//printf("绿色图像的出现代表着Kinect摄像头的相应位置距离屏幕太近，比如绿色点出现在画面左下方，就代表摄像头左边比右边更靠近屏幕、下面比上面更靠近屏幕，\n");
			//printf("请根据图像微调Kinect的位置，直到绿色点完全消失，此时Kinect应该基本平行于投影面.然后按F5重新矫正.\n");
			//printf("请确保画面干净且识别区正确再激活程序，激活后会开启对鼠标的调用，绿色点的存在会干扰鼠标位置\n");
			//printf("激活程序请按F1.\n\n");
		}
		if (GetKeyState(VK_F1) < 0)												//If you are not satisfied with the results, you can re-identify in the next cycle   若对效果不满意，则可以在下一次循环中重新识别
		{
			printf("Program activation!\n\n");
			work = true;
		}
		if (GetKeyState(VK_F5) < 0)												//If you are not satisfied with the results, you can re-identify in the next cycle   若对效果不满意，则可以在下一次循环中重新识别
		{
			gotColorCenter = gotColorScreen = gotDepthScreen = failMessage = work = false;
			firstRun = true;
		}

		release:														//Display images and release Frame   显示图像并释放Frame

		namedWindow("COLOR", WINDOW_AUTOSIZE);
		namedWindow("DEPTH", WINDOW_AUTOSIZE);
		imshow("COLOR", colorImg);
		imshow("DEPTH", depthImg);

		

		if (waitKey(30) == VK_ESCAPE)
			break;
		myColorFrame->Release();
		myDepthFrame->Release();
		myBodyFrame->Release();
		delete[] depthData;
		delete[] bodyArr;
	}



	myDepthReader->Release();		//Released Depth   释放Depth
	myDepthSource->Release();

	myColorReader->Release();		//Release Color   释放Color
	myColorSource->Release();

	myBodyReader->Release();		//Release Body   释放Body
	myBodySource->Release();

	myMapper->Release();			//Release public resources   释放公共资源
	myDescription->Release();

	mySensor->Close();			//Release the Sensor   释放Sensor
	mySensor->Release();


	return	0;
}




bool	find_edge(const Mat & img, const Point & center, int & left, int & right, int & up, int & buttom)
{
	Vec4b	screen = img.at<Vec4b>(center.y, center.x);

	int	front_i = -1, front_j = -1;
	for (int i = center.y; i < COLORHEIGHT; i++)
	{
		bool	flag = false;
		for (int j = center.x; j >= 0; j--)
			if (abs(img.at<Vec4b>(i, j)[0] - screen[0]) <= BGR_E && abs(img.at<Vec4b>(i, j)[1] - screen[1]) <= BGR_E && abs(img.at<Vec4b>(i, j)[2] - screen[2]) <= BGR_E && abs(img.at<Vec4b>(i, j)[3] - screen[3]) <= BGR_E)
			{
				flag = true;
				if (front_i == -1)
					front_i = i;
				if (front_j == -1)
					front_j = j;
				if (front_i < i && front_i != -1)
					front_i = i;
				if (front_j > j && front_j != -1)
					front_j = j;
			}
			else
				break;
		if (!flag)
			break;

	}
	buttom = front_i;
	left = front_j;

	front_i = -1;
	front_j = -1;
	for (int i = center.y; i >= 0; i--)
	{
		bool	flag = false;
		for (int j = center.x; j < COLORWIDTH; j++)
			if (abs(img.at<Vec4b>(i, j)[0] - screen[0]) <= BGR_E && abs(img.at<Vec4b>(i, j)[1] - screen[1]) <= BGR_E && abs(img.at<Vec4b>(i, j)[2] - screen[2]) <= BGR_E && abs(img.at<Vec4b>(i, j)[3] - screen[3]) <= BGR_E)
			{
				flag = true;
				if (front_i == -1)
					front_i = i;
				if (front_j == -1)
					front_j = j;
				if (front_i > i && front_i != -1)
					front_i = i;
				if (front_j < j && front_j != -1)
					front_j = j;
			}
			else
				break;
		if (!flag)
			break;
	}
	up = front_i;
	right = front_j;

	if (left == -1 || right == -1 || buttom == -1 || up == -1)
		return	false;
	return	true;
}

void	draw_screen(Mat & img, int & left, int & right, int & up, int & buttom)
{
	Point	p_1 = { left,up };
	Point	p_2 = { right,up };
	Point	p_3 = { right,buttom };
	Point	p_4 = { left,buttom };

	line(img, p_1, p_2, COLOR_TABLE[RED], 3);
	line(img, p_2, p_3, COLOR_TABLE[RED], 3);
	line(img, p_3, p_4, COLOR_TABLE[RED], 3);
	line(img, p_4, p_1, COLOR_TABLE[RED], 3);
}

bool	check_depth_coordinate(int x, int y)
{
	if (x >= 0 && x < DEPTHWIDTH && y >= 0 && y < DEPTHHEIGHT)
		return	true;
	return	false;
}