#include <stdio.h>
#include <sys/time.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h> // for isatty

#include "ASICamera2.h"

#define  MAX_CONTROL 7

// Defined in libASICamera2
extern unsigned long GetTickCount();

bool exit_mainloop;

static void sigint_handler(int sig, siginfo_t *si, void *unused)
{
	struct sigaction sa;

	fprintf(stderr, "Caught %s.\n", sig == SIGINT ? "SIGINT" : "SIGTERM");
	// Remove the handler
	sigemptyset(&sa.sa_mask);
	sa.sa_sigaction = 0;
	sa.sa_flags = SA_RESETHAND;
	if (sigaction(sig, &sa, NULL) == -1) {
		fprintf(stderr, "sigaction: %s\n", strerror(errno));
		exit(EXIT_FAILURE);
	}
	// Break the main loop
	exit_mainloop = true;
}

static void install_sigint_handler(void)
{
	struct sigaction sa;

	sa.sa_flags = SA_SIGINFO;
	sigemptyset(&sa.sa_mask);
	sa.sa_sigaction = sigint_handler;
	if (sigaction(SIGINT, &sa, NULL) == -1) {
		fprintf(stderr, "sigaction: %s\n", strerror(errno));
		exit(EXIT_FAILURE);
	}
	if (sigaction(SIGTERM, &sa, NULL) == -1) {
		fprintf(stderr, "sigaction: %s\n", strerror(errno));
		exit(EXIT_FAILURE);
	}
}

int  main()
{
	const char* bayer[] = {"RG","BG","GR","GB"};
	int i;
	bool bresult;
	int time1,time2;
	int CamIndex=0;

	install_sigint_handler();

	int numDevices = ASIGetNumOfConnectedCameras();
	if (numDevices <= 0)
	{
		fprintf(stderr, "no camera connected\n");
		exit(EXIT_FAILURE);
	}

	ASI_CAMERA_INFO CamInfo;
	fprintf(stderr, "Attached cameras:\n");
	for (i = 0; i < numDevices; i++)
	{
		ASIGetCameraProperty(&CamInfo, i);
		fprintf(stderr, "\t%d: %s\n", i, CamInfo.Name);
	}
	CamIndex = 0;

	ASIGetCameraProperty(&CamInfo, CamIndex);
	bresult = ASIOpenCamera(CamInfo.CameraID);
	bresult += ASIInitCamera(CamInfo.CameraID);
	if (bresult)
	{
		fprintf(stderr, "OpenCamera error, are you root?\n");
		exit(EXIT_FAILURE);
	}

	fprintf(stderr, "%s information\n",CamInfo.Name);
	int iMaxWidth, iMaxHeight;
	iMaxWidth = CamInfo.MaxWidth;
	iMaxHeight = CamInfo.MaxHeight;
	fprintf(stderr, "\tResolution: %dx%d\n", iMaxWidth, iMaxHeight);
	if (CamInfo.IsColorCam)
		fprintf(stderr, "\tColor Camera: bayer pattern:%s\n",bayer[CamInfo.BayerPattern]);
	else
		fprintf(stderr, "\tMono camera\n");

	int ctrlnum;
	ASIGetNumOfControls(CamInfo.CameraID, &ctrlnum);
	ASI_CONTROL_CAPS ctrlcap;
	for (i = 0; i < ctrlnum; i++)
	{
		ASIGetControlCaps(CamInfo.CameraID, i, &ctrlcap);
		fprintf(stderr, "\t%s '%s' [%ld,%ld] %s\n", ctrlcap.Name, ctrlcap.Description,
			ctrlcap.MinValue, ctrlcap.MaxValue,
			ctrlcap.IsAutoSupported?"Auto":"Manual");
	}

	// RAW16 does not seem to work with USB2 on RPi3 (causes too long exposure times?)
	// ASI_IMG_RAW8: use ffmpeg -pixel_format gray8
	// ASI_IMG_RAW16: use ffmpeg -pixel_format gray12le
	// Common (pipe from stdin): ffmpeg -f rawvideo -vcodec rawvideo -video_size 1280x960 -i pipe:0
	ASI_IMG_TYPE Image_type = ASI_IMG_RAW8;
	ASISetROIFormat(CamInfo.CameraID, iMaxWidth, iMaxHeight, 1, Image_type);

	unsigned char *imageData;
	long imageSize;
	if (Image_type == ASI_IMG_RAW16)
		imageSize = iMaxWidth * iMaxHeight * 2;
	else if (Image_type == ASI_IMG_RGB24)
		imageSize = iMaxWidth * iMaxHeight * 3;
	else
		imageSize = iMaxWidth * iMaxHeight * 1;

	imageData = (unsigned char*)malloc(imageSize);

	int exp_ms = 125;
	int gain = 4;
	ASISetControlValue(CamInfo.CameraID, ASI_EXPOSURE, exp_ms*1000, ASI_FALSE);
	ASISetControlValue(CamInfo.CameraID, ASI_GAIN, gain, ASI_TRUE);
	ASISetControlValue(CamInfo.CameraID, ASI_BANDWIDTHOVERLOAD, 60, ASI_FALSE); // transfer speed percentage
	ASISetControlValue(CamInfo.CameraID, ASI_HIGH_SPEED_MODE, 0, ASI_FALSE);
	ASISetControlValue(CamInfo.CameraID, ASI_WB_B, 90, ASI_FALSE);
	ASISetControlValue(CamInfo.CameraID, ASI_WB_R, 48, ASI_TRUE);

	if (CamInfo.IsTriggerCam)
	{
		ASI_CAMERA_MODE mode;
		// Multi mode camera, need to select the camera mode
		ASISetCameraMode(CamInfo.CameraID, ASI_MODE_NORMAL);
		ASIGetCameraMode(CamInfo.CameraID, &mode);
		if (mode != ASI_MODE_NORMAL)
			fprintf(stderr, "Set mode failed!\n");
	}

	ASIStartVideoCapture(CamInfo.CameraID);

	long lVal;
	ASI_BOOL bAuto;
	ASIGetControlValue(CamInfo.CameraID, ASI_TEMPERATURE, &lVal, &bAuto);
	fprintf(stderr, "Sensor temperature:%.1f\n", lVal/10.0);

	if (isatty(fileno(stdout))) {
		fprintf(stderr, "stdout is a tty, will not dump video data\n");
		exit_mainloop = true;
	}

	time1 = GetTickCount();
	int iDroppedFrames;
	unsigned long fpsCount = 0, fCount = 0;
	while (!exit_mainloop)
	{
		ASI_ERROR_CODE code;

		code = ASIGetVideoData(CamInfo.CameraID, imageData, imageSize, 500);
		if (code == ASI_SUCCESS) {
			fCount++;
			fpsCount++;
			fwrite(imageData, 1, imageSize, stdout);
			fflush(stdout);
		}
		else {
			fprintf(stderr, "ASIGetVideoData() error: %d\n", code);
			exit(EXIT_FAILURE);
		}

		time2 = GetTickCount();
		if (time2-time1 >= 1000)
		{
			ASIGetDroppedFrames(CamInfo.CameraID, &iDroppedFrames);
			fprintf(stderr, "\nfps:%lu dropped:%d\n", fpsCount, iDroppedFrames);
			fpsCount = 0;
			time1 = GetTickCount();
		}
	}
	fprintf(stderr, "Frames written: %lu\n", fCount);
	ASIStopVideoCapture(CamInfo.CameraID);
	ASICloseCamera(CamInfo.CameraID);
	return 0;
}