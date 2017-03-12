#include "StitchingUtil.h"
#include "ImageUtil.h"
#include <algorithm>


using namespace cv::detail;
using namespace cv;
#ifdef OPENCV_3
void StitchingUtil::matchWithBRISK(
	const Mat &left, const Mat &right, std::vector<std::pair<Point2f, Point2f>> &matchedPair) {

	std::vector<KeyPoint> kptsL, kptsR;
	Mat descL, descR;
	std::vector<DMatch> goodMatches;
	const int Thresh = 150, Octave = 3;
	const float PatternScales = 4.0f;
#ifdef OPENCV_3
	Ptr<BRISK> brisk = BRISK::create(Thresh, Octave, PatternScales);
	brisk->detectAndCompute(left, getMask(left,true), kptsL, descL);
	brisk->detectAndCompute(right, getMask(right,false), kptsR, descR);
#else
	//TOSOLVE: 2.4.9-style incovation, not sure it works
	cv::BRISK briskDetector(Thresh, Octave, PatternScales);
	briskDetector.create("Feature2D.BRISK");
	briskDetector.detect(left, kptsL);
	briskDetector.compute(left, kptsL, descL);
	briskDetector.detect(right, kptsR);
	briskDetector.compute(right, kptsR, descR);
#endif

	descL.convertTo(descL, CV_32F);
	descR.convertTo(descR, CV_32F);

	FlannBasedMatcher matcher(new flann::KDTreeIndexParams(kFlannNumTrees));
	std::vector<DMatch> flannMatches;
	matcher.match(descL, descR, flannMatches);

	double maxDist = 0;
	double minDist = std::numeric_limits<float>::max();
	for(int i = 0; i < flannMatches.size(); ++i) {
		double dist = flannMatches[i].distance;
		maxDist = max(maxDist, dist);
		minDist = min(minDist, dist);
	}

	for(int i = 0; i < flannMatches.size(); ++i) {
		double distThresh = kFlannMaxDistScale * minDist;
		if (flannMatches[i].distance <= max(distThresh, kFlannMaxDistThreshold)) {
			goodMatches.push_back(flannMatches[i]);
		}
	}
	showMatchingPair(left, kptsL, right, kptsR, goodMatches);


	for (const DMatch& match : goodMatches) {
		const Point2f& kptL = kptsL[match.queryIdx].pt;
		const Point2f& kptR = kptsR[match.trainIdx].pt;
		matchedPair.push_back(std::make_pair(kptL, kptR));
	}

}
void StitchingUtil::matchWithORB(
	const Mat &left, const Mat &right, std::vector<std::pair<Point2f, Point2f>> &matchedPair) {
	static const bool kUseGPU = false;
	static const float kMatchConfidence = 0.4;

	detail::OrbFeaturesFinder finder;

	ImageFeatures imgFeaturesL;
	finder(left, imgFeaturesL, getMaskROI(left, true));
	imgFeaturesL.img_idx = 0;

	ImageFeatures imgFeaturesR;
	finder(right, imgFeaturesR, getMaskROI(right,false));
	imgFeaturesR.img_idx = 1;

	std::vector<ImageFeatures> features;
	features.push_back(imgFeaturesL);
	features.push_back(imgFeaturesR);
	std::vector<MatchesInfo> pairwiseMatches;
	BestOf2NearestMatcher matcher(kUseGPU, kMatchConfidence);
	matcher(features, pairwiseMatches);

	for (const MatchesInfo& matchInfo : pairwiseMatches) {
		if (matchInfo.src_img_idx != 0 || matchInfo.dst_img_idx != 1) {
			continue;
		}
		for (const DMatch& match : matchInfo.matches) {
			const Point2f& kptL = imgFeaturesL.keypoints[match.queryIdx].pt;
			const Point2f& kptR = imgFeaturesR.keypoints[match.trainIdx].pt;
			matchedPair.push_back(std::make_pair(kptL, kptR));
		}
		showMatchingPair(left, imgFeaturesL.keypoints, right, imgFeaturesR.keypoints, matchInfo.matches);
	}
}


void StitchingUtil::matchWithAKAZE(
	const Mat &left, const Mat &right, std::vector<std::pair<Point2f, Point2f>> &matchedPair) {

	Mat descL, descR;
	std::vector<KeyPoint> kptsL, kptsR;
	std::vector<DMatch> goodMatches;

	Ptr<AKAZE> akaze = AKAZE::create();
	akaze->detectAndCompute(left, getMask(left, true), kptsL, descL);
	akaze->detectAndCompute(right, getMask(right, false), kptsR, descR);

	// FlannBasedMatcher with KD-Trees needs CV_32
	descL.convertTo(descL, CV_32F);
	descR.convertTo(descR, CV_32F);

	// KD-Tree param: # of parallel kd-trees
	FlannBasedMatcher matcher(new flann::KDTreeIndexParams(kFlannNumTrees));
	std::vector<DMatch> flannMatches;
	matcher.match(descL, descR, flannMatches);

	double maxDist = 0;
	double minDist = std::numeric_limits<float>::max() ;
	for(int i = 0; i < flannMatches.size(); ++i) {
		double dist = flannMatches[i].distance;
		maxDist = max(maxDist, dist);
		minDist = min(minDist, dist);
	}

	for(int i = 0; i < flannMatches.size(); ++i) {
		double distThresh = kFlannMaxDistScale * minDist;
		if (flannMatches[i].distance <= max(distThresh, kFlannMaxDistThreshold)) {
			goodMatches.push_back(flannMatches[i]);
		}
	}
	showMatchingPair(left, kptsL, right, kptsR, goodMatches);;
	for (const DMatch& match : goodMatches) {
		const Point2f& kptL = kptsL[match.queryIdx].pt;
		const Point2f& kptR = kptsR[match.trainIdx].pt;
		matchedPair.push_back(std::make_pair(kptL, kptR));
	}

}
#endif

void StitchingUtil::facebookKeyPointMatching(Mat &left, Mat &right, std::vector<std::pair<Point2f, Point2f>> &matchedPair) {
	// input Mat must be grayscale
	assert(left.channels() == 1);
	assert(right.channels() == 1);

	std::vector<std::pair<Point2f, Point2f>> matchPointPairsLRAll;
	//try {
	//	matchWithBRISK(left, right, matchPointPairsLRAll);  /* BRISK seems not so good */
	//} catch(...){
	//	std::cout << "[Warning] matchingWithBrisk failed.";
	//};
	matchWithORB(left, right, matchPointPairsLRAll);
#ifdef OPENCV_3
	matchWithAKAZE(left, right, matchPointPairsLRAll);
#endif


	// Remove duplicate keypoints
	sort(matchPointPairsLRAll.begin(), matchPointPairsLRAll.end(), _cmp_pp2f);
	matchPointPairsLRAll.erase(
		std::unique(matchPointPairsLRAll.begin(), matchPointPairsLRAll.end()),
		matchPointPairsLRAll.end());

	// Apply RANSAC to filter weak matches (like, really weak)
	std::vector<Point2f> matchesL, matchesR;
	std::vector<uchar> inlinersMask;
	for(int i = 0; i < matchPointPairsLRAll.size(); ++i) {
		matchesL.push_back(matchPointPairsLRAll[i].first);
		matchesR.push_back(matchPointPairsLRAll[i].second);
	}

	static const int kRansacReprojThreshold = 80;
	findHomography(
		matchesR,
		matchesL,
		CV_RANSAC,
		kRansacReprojThreshold,
		inlinersMask);

	for (int i = 0; i < inlinersMask.size(); ++i) {
		if (inlinersMask[i]) {
			matchedPair.push_back(std::make_pair(matchesL[i], matchesR[i]));
		}
	}
}

void StitchingUtil::selfKeyPointMatching(Mat &left, Mat &right, std::vector<std::pair<Point2f, Point2f>> &matchedPair, StitchingType sType) {
	// input Mat must be grayscale
	assert(left.channels() == 1);
	assert(right.channels() == 1);

	assert(sType == SELF_SIFT || sType == SELF_SURF);

	Mat desL, desR;
	std::vector<KeyPoint> kptL, kptR;
#if (defined OPENCV_3) && (defined  OPENCV3_CONTRIB)
	if (sType == SELF_SIFT) {
		Ptr<FeatureDetector> DE = cv::xfeatures2d::SIFT::create();
		DE->detectAndCompute(left, getMask(left, true), kptL, desL);
		DE->detectAndCompute(right, getMask(right, false), kptR, desR);
	} else {
		Ptr<FeatureDetector> DE = cv::xfeatures2d::SURF::create();
		DE->detectAndCompute(left, getMask(left, true), kptL, desL);
		DE->detectAndCompute(right, getMask(right, false), kptR, desR);
	}
#elif (defined OPENCV_2)
	if (sType == SELF_SIFT) {
		SiftFeatureDetector siftFD;
		SiftDescriptorExtractor siftDE;
		siftFD.detect(left, kptL);
		siftFD.detect(right, kptR);
		siftDE.compute(left, kptL, desL);
		siftDE.compute(right, kptR, desR);
	} else {
		SurfFeatureDetector surfFD(minHessian);
		SurfDescriptorExtractor surfDE;
		surfFD.detect(left, kptL);
		surfFD.detect(right, kptR);
		surfDE.compute(left, kptL, desL);
		surfDE.compute(right, kptR, desR);
	}
#endif
	FlannBasedMatcher matcher(new flann::KDTreeIndexParams(kFlannNumTrees));
	std::vector<DMatch> matches;
	matcher.match(desL, desR, matches);

	double maxDist = 0;
	double minDist = std::numeric_limits<float>::max();
	double dis;
	for (int i=0; i<desL.rows; ++i) {
		dis = matches[i].distance;
		minDist = min(dis, minDist);
		maxDist = max(dis, maxDist);
	}

	// USe only Good macthes (distance less than 3*minDist)
	std::vector<DMatch> goodMatches;
	for (int i=0; i<desL.rows; ++i) {
		if (matches[i].distance < max(kFlannMaxDistScale*minDist, kFlannMaxDistThreshold))
			goodMatches.push_back(matches[i]);
	}

	showMatchingPair(left, kptL, right, kptR, goodMatches);
	for (int i=0; i<goodMatches.size(); ++i) {
		matchedPair.push_back(std::make_pair(kptL[goodMatches[i].queryIdx].pt, kptR[goodMatches[i].trainIdx].pt));
	}
}

void StitchingUtil::selfStitchingSAfterMatching(
	const Mat &left, const Mat &right, const Mat &leftOri, const Mat &rightOri,
	std::vector<std::pair<Point2f, Point2f>> &matchedPair, Mat &dstImage) {
	// input mat should be colored ones
	std::vector<Point2f> matchedL, matchedR;
	unzipMatchedPair(matchedPair, matchedL, matchedR);

	const double ransacReprojThreshold = 5;
	OutputArray mask=noArray();
	const int maxIters = 2000;
	const double confidence = 0.95;

	Mat H = cv::findHomography(matchedR, matchedL, CV_RANSAC, ransacReprojThreshold, mask, maxIters, confidence);
	LOG_MESS("Homography = \n" << H );
	warpPerspective(rightOri, dstImage, H, Size(leftOri.cols+rightOri.cols, leftOri.rows), INTER_LINEAR);
	Mat half(dstImage, Rect(0,0,leftOri.cols,leftOri.rows));
	leftOri.copyTo(half);

	// TOSOLVE: how to add blend manually
}

Stitcher StitchingUtil::opencvStitcherBuild(StitchingType sType) {
	Stitcher s = Stitcher::createDefault(0);
	if (sType == OPENCV_DEFAULT) return s;
	s.setRegistrationResol(0.3);					//0.6 by default, smaller the faster
	s.setPanoConfidenceThresh(1);					//1 by default, 0.6 or 0.4 worth a try
	s.setWaveCorrection(false);						//true by default, set false for acceleration
	const bool useORB = false;						// ORB is faster but less stable
	s.setFeaturesFinder(useORB ? (FeaturesFinder*)(new OrbFeaturesFinder()) : (FeaturesFinder*)(new SurfFeaturesFinder()));
	s.setFeaturesMatcher(new BestOf2NearestMatcher(false, 0.5f /* 0.65 by default */));
	s.setBundleAdjuster(new BundleAdjusterRay());	// faster
	s.setSeamFinder(new NoSeamFinder);
	s.setExposureCompensator(new NoExposureCompensator);
	s.setBlender(new FeatherBlender);				// multiBandBlender byb default, this is faster
	//s.setWarper(new cv::CylindricalWarper());
	return s;
}


void StitchingUtil::opencvStitching(const std::vector<Mat> &srcs, Mat &dstImage, StitchingType sType) {
	assert(sType <= OPENCV_TUNED);
	static Stitcher s = opencvStitcherBuild(sType);
	Stitcher::Status status;
	switch (sType) {
	case OPENCV_DEFAULT:
		status = s.stitch(srcs, dstImage);
		
		if (Stitcher::OK != status) {
			LOG_ERR("Cannot stitch the image, errCode = " << status);
			assert(false);
		}
		break;
	case OPENCV_TUNED:
		status = s.estimateTransform(srcs);
		if (Stitcher::OK != status) {
			LOG_ERR("Cannot stitch the image, error in estimateTranform, errCode = " << status);
			assert(false);
		}
		status = s.composePanorama(dstImage);
				if (Stitcher::OK != status) {
			LOG_ERR("[Error] Cannot stitch the image, error in composePanorama, errCode = " << status);
			assert(false);
		}
		break;
	default:
		assert(false);
	}
}

void StitchingUtil::_stitch(const std::vector<Mat> &srcs, Mat &dstImage, StitchingType sType, std::pair<double, double> &maskRatio) {
	std::vector<Mat> srcsGrayScale;
	std::vector<std::pair<Point2f, Point2f>> matchedPair;
	Mat tmp, tmpGrayScale, tmp2;
	switch (sType) {
	case OPENCV_DEFAULT:
	case OPENCV_TUNED:
		opencvStitching(srcs, dstImage, sType);
		break;
	case OPENCV_SELF_DEV:
		opencvSelfStitching(srcs, dstImage, maskRatio);
		break;
	case FACEBOOK:
	case SELF_SURF:
	case SELF_SIFT:
		getGrayScaleAndFiltered(srcs, srcsGrayScale);	// Add filter
		tmp = srcs[0].clone(), tmpGrayScale = srcsGrayScale[0].clone();
		for (int i=1; i<srcs.size(); ++i) {
			matchedPair.clear();
			sType == FACEBOOK
				? facebookKeyPointMatching(tmpGrayScale, srcsGrayScale[i], matchedPair)
				: selfKeyPointMatching(tmpGrayScale, srcsGrayScale[i], matchedPair, sType);
			selfStitchingSAfterMatching(tmpGrayScale, srcsGrayScale[i].clone(), tmp, srcs[i], matchedPair, tmp2);
			tmp = tmp2.clone();
			cvtColor(tmp, tmpGrayScale, CV_RGB2GRAY);
		}
		dstImage = tmp;
		break;
	default:
		assert(false);
	}
}

void StitchingUtil::doStitch(std::vector<Mat> &srcs, Mat &dstImage, StitchingPolicy sp, StitchingType sType) {
	// assumes srcs[0] is the front angle of view, so srcs[1] needs cut
	// TOSOLVE: Currently supports two srcs to stitch
	assert(srcs.size() == 2);		
	std::vector<Mat> matCut;
	Mat tmp, forshow;
	switch(sp) {
	case DIRECT:
		matCut.clear();
		matCut.push_back( // matCut[0]: left after cut
			srcs[1](Range(0, srcs[1].rows), Range(round(srcs[1].cols/2)+1, srcs[1].cols)).clone());
		matCut.push_back( // matCut[1]: right after cut
			srcs[1](Range(0, srcs[1].rows), Range(0, round(srcs[1].cols/2)+1)).clone());

		dstImage.create(srcs[0].rows, srcs[0].cols + matCut[1].cols + matCut[0].cols, srcs[0].type());
		srcs[0].copyTo(dstImage(Rect(matCut[0].cols, 0, srcs[0].cols, srcs[0].rows)));
		matCut[0].copyTo(dstImage(Rect(0, 0, matCut[0].cols, srcs[0].rows)));
		matCut[1].copyTo(dstImage(Rect(matCut[0].cols + srcs[0].cols, 0, matCut[1].cols, srcs[0].rows)));
		break;
	case STITCH_ONE_SIDE:
		//std::swap(srcs[0], srcs[1]);
		_stitch(srcs, dstImage, sType);
		//// stitch another side
		//matCut.clear();
		//matCut.push_back(
		//	tmp(Range(0, tmp.rows), Range(round(tmp.cols*2.0/3), tmp.cols)).clone());
		//resize(dstImage, forshow, Size(matCut[0].cols/2, matCut[0].rows/2));
		//imshow("windows",forshow);
		//matCut.push_back(
		//	tmp(Range(0, tmp.rows), Range(0, round(tmp.cols*2.0/3))).clone());
		//_stitch(matCut, dstImage, sType);
		break;
	case STITCH_DOUBLE_SIDE:
		_stitchDoubleSide(srcs, dstImage, sType);
		break;
	default:
		assert(false);
	}
}

void StitchingUtil::_stitchDoubleSide(std::vector<Mat> &srcs, Mat &dstImage, StitchingType sType) {
	Mat dstBF, dstFB;
	StitchingUtil::osParam.blend_strength = 4;
	_stitch(srcs, dstBF, sType);
	std::reverse(srcs.begin(), srcs.end());
	_stitch(srcs, dstFB, sType);
	std::reverse(srcs.begin(), srcs.end());
	//imshow("BF",dstBF);
	//imshow("FB",dstFB);
	//cvWaitKey();

	ImageUtil iu;
	iu.USM(dstBF, dstBF);
	iu.USM(dstFB, dstFB);

	const double ratio = 1.0/(4*(1-OVERLAP_RATIO_DOUBLESIDE));
	const double overlapRatio_tolerance = OVERLAP_RATIO_DOUBLESIDE*1.05;
	Mat dstTmp;
	std::vector<Mat> tmpSrc;
	tmpSrc.push_back(
		dstFB(Range(0,dstFB.rows), Range(max(0.0,0.5-ratio)*dstFB.cols,min(1.0,0.5+ratio)*dstFB.cols)).clone());
	tmpSrc.push_back(
		dstBF(Range(0,dstBF.rows), Range(max(0.0,0.5-ratio)*dstBF.cols,min(1.0,0.5+ratio)*dstBF.cols)).clone());
	// dstTmp: F-B-F
	StitchingUtil::osParam.blend_strength = 0;
	_stitch(tmpSrc,dstTmp,sType,std::make_pair(overlapRatio_tolerance,0.7));	
	iu.USM(dstTmp, dstTmp);

	//imshow("FBF",dstTmp);
	//cvWaitKey();
	tmpSrc.clear();
	tmpSrc.push_back(
		dstTmp(Range(0,dstTmp.rows), Range(dstTmp.cols*0.5,dstTmp.cols)).clone());
	tmpSrc.push_back(
		dstTmp(Range(0,dstTmp.rows), Range(0,dstTmp.cols*0.5)).clone());
	StitchingUtil::osParam.blend_strength = 1;
	_stitch(tmpSrc,dstImage,sType,std::make_pair(overlapRatio_tolerance,0.7));

}

void StitchingUtil::getGrayScaleAndFiltered(const std::vector<Mat> &src, std::vector<Mat> &dst) {
	for (int i=0; i<src.size(); ++i) {
		Mat tmp1,tmp2;
		cvtColor(src[i], tmp1, CV_RGB2GRAY);
		bilateralFilter(tmp1, tmp2, 11,11*2,11/2);
		dst.push_back(tmp2);
	}
	assert(src.size() == dst.size());
}

void StitchingUtil::unzipMatchedPair(
	std::vector< std::pair<Point2f, Point2f> > &matchedPair, std::vector<Point2f> &matchedL, std::vector<Point2f> &matchedR) {
		matchedL.clear(); matchedR.clear();
		for (int i=0; i<matchedPair.size(); ++i) {
			matchedL.push_back(matchedPair[i].first);
			matchedR.push_back(matchedPair[i].second);
		}
}

bool StitchingUtil::_cmp_pp2f(const std::pair<Point2f, Point2f> &a, const std::pair<Point2f, Point2f> &b) {
	return a.first == b.first ? _cmp_p2f(a.second, b.second) : _cmp_p2f(a.first, b.first);
}

bool StitchingUtil::_cmp_p2f(const Point2f &a, const Point2f &b) {
	return a.x == b.x ? a.y < b.y : a.x < b.x;
}

Mat StitchingUtil::getMask(const Mat &srcImage, bool isLeft, std::pair<double, double> &ratio) {
	Mat mask = Mat::zeros(srcImage.size(), CV_8U);
	Mat roi(mask, getMaskROI(srcImage,isLeft,ratio)[0]);
	roi = Scalar(255,255,255);
	return mask;
}
std::vector<Rect> StitchingUtil::getMaskROI(const Mat &srcImage, bool isLeft, std::pair<double, double> &ratio) {
	double widthParam = ratio.first, heightParam = ratio.second;
	std::vector<Rect> ret;
	ret.push_back(isLeft
		? Rect(round(srcImage.cols*(1-widthParam)),0,round(srcImage.cols*widthParam),srcImage.rows*heightParam)
		: Rect(0,0,round(srcImage.cols*widthParam),srcImage.rows*heightParam));
	return ret;
}

void StitchingUtil::showMatchingPair(
	const Mat &left,
	const std::vector<KeyPoint> &kptL,
	const Mat &right,
	const std::vector<KeyPoint> &kptR,
	const std::vector<DMatch> &goodMatches) {
		Mat img_matches;
		drawMatches(left,kptL, right, kptR, goodMatches, img_matches);
		Mat forshow;
		resize(img_matches, forshow, Size(img_matches.cols/3, img_matches.rows/3));
		imshow("MatchSift", forshow);
		waitKey();
}

std::vector<UMat> StitchingUtil::convertMatToUMat(std::vector<Mat> &input) {
	std::vector<UMat> ret(input.size());
	for (int i=0; i<input.size(); ++i) {
		ret[i] = input[i].clone().getUMat(ACCESS_RW);
	}
	return ret;
}

bool StitchingUtil::almostBlack(const Vec3b &v) {
	const int tolerance = square(3*BLACK_TOLERANCE);
	return v[0]*v[0] + v[1]*v[1] + v[2]*v[2] <= tolerance;
}


void StitchingUtil::removeBlackPixelByDoubleScan(Mat &src, Mat &dst) {
	// first find height boundary
	Mat_<Vec3b> tmpSrc = src;
	//imshow("src",tmpSrc);
	//	cvWaitKey();
	int widthSideTolerance = tmpSrc.cols*0.02;
	int maxRows = tmpSrc.rows-1, minRows = 0;
	int maxCols = tmpSrc.cols-1, minCols = 0;
	LOG_MESS(widthSideTolerance << " " << maxRows);
	for (int i=widthSideTolerance; i<tmpSrc.cols-widthSideTolerance; ++i) {
		for (;maxRows>=0 && almostBlack(tmpSrc(maxRows, i)); --maxRows);
		for (;minRows<tmpSrc.rows&&almostBlack(tmpSrc(minRows, i)); ++minRows);
	}
	// then find width boundary
	for (int j=minRows; j <= maxRows; ++j) {
		for (;maxCols>=0&&almostBlack(tmpSrc(j, maxCols)); --maxCols);
		for (;minCols<tmpSrc.cols&&almostBlack(tmpSrc(j, minCols)); ++minCols);
	}

	double restRatioPercent = (maxRows-minRows+1)*(maxCols-minCols+1)*100.0/(tmpSrc.cols*tmpSrc.rows);
	LOG_MESS("Remove black pixel, remain:" << restRatioPercent << "%");
	dst = src(Range(minRows,maxRows), Range(minCols,maxCols)).clone();
	if (restRatioPercent < 70) {
		LOG_WARN("removeBlackPixelByDoubleScan() only remain " << restRatioPercent <<"% of src.")
#ifdef SHOW_IMAGE
		imshow("src",tmpSrc);
		imshow("dst",dst);
		cvWaitKey();
#endif
	}
	
}
// REF: http://stackoverflow.com/questions/21410449/how-do-i-crop-to-largest-interior-bounding-box-in-opencv
bool StitchingUtil::removeBlackPixelByContourBound(Mat &src, Mat &dst) {
	Mat grayscale;
	cvtColor(src, grayscale, CV_BGR2GRAY);
	Mat mask = grayscale > BLACK_TOLERANCE;

	std::vector<std::vector<Point>> contours;
	std::vector<Vec4i> hierarchy;

	findContours(mask, contours, hierarchy, CV_RETR_EXTERNAL, CV_CHAIN_APPROX_NONE, Point(0,0));
	Mat contourImg = Mat::zeros(src.size(), CV_8UC3);

	int maxSz = 0, id = 0;
	for (int i=0; i<contours.size(); ++i) {
		if (contours.at(i).size() > maxSz) {
			maxSz = contours.at(i).size();
			id = i;
		}
	}

	Mat contourMask = cv::Mat::zeros(src.size(), CV_8UC1);
	drawContours(contourMask, contours, id, Scalar(255), -1, 8, hierarchy,0,Point());

	std::vector<Point> cSortedX = contours.at(id);
	std::sort(cSortedX.begin(), cSortedX.end(), [](const Point &a, const Point &b){return a.x<b.x;});
	std::vector<Point> cSortedY = contours.at(id);
	std::sort(cSortedY.begin(), cSortedY.end(), [](const Point &a, const Point &b){return a.y<b.y;});
	int minX = 0, maxX = cSortedX.size()-1;
	int minY = 0, maxY = cSortedY.size()-1;

	Rect interiorBoundingBox;

	while(minX < maxX && minY < maxY) {
		Point minP(cSortedX[minX].x, cSortedY[minY].y);
		Point maxP(cSortedX[maxX].x, cSortedY[maxY].y);
		interiorBoundingBox = Rect(minP.x, minP.y, maxP.x-minP.x, maxP.y-minP.y);
		// out-codes
		int ocTop = 0, ocBottom = 0, ocLeft = 0, ocRight = 0;

		if (checkInterior(contourMask, interiorBoundingBox, ocTop, ocBottom, ocLeft, ocRight))
			break;
		if (ocLeft) ++minX;
		if (ocRight)--maxX;
		if (ocTop) ++minY;
		if (ocBottom) --maxY;
		if (!(ocLeft || ocRight || ocTop || ocBottom)) {
			LOG_WARN("removeBlackPixelByContourBound() failed. Using removeBlackPixelByDoubleScan() instead.");
			return false;
		}

	}
	double restRatioPercent = interiorBoundingBox.size().area()*100.0/src.size().area();
	LOG_MESS("Remove black pixel, remain:" << interiorBoundingBox << " " << restRatioPercent << "%");
	dst = src(interiorBoundingBox).clone();
	if (restRatioPercent < 70) {
		LOG_WARN("removeBlackPixelByContourBound() only remain " << restRatioPercent <<"% of src.")
#ifdef SHOW_IMAGE
		imshow("src",src);
		imshow("dst",dst);
		cvWaitKey();
#endif
	}
	
	return true;
}

bool StitchingUtil::checkInterior(const Mat& mask, const Rect& interiorBB, int& top, int& bottom, int& left, int& right) {
	bool ret = true;
	Mat sub = mask(interiorBB);
	int x=0,y=0;
	int _top = 0, _bottom = 0, _left = 0, _right = 0;

	for (; x<sub.cols; ++x) {
		if (sub.at<unsigned char>(y,x) <= BLACK_TOLERANCE) {
			ret = false;
			++_top;
		}
	}
	for (y=sub.rows-1, x=0; x<sub.cols; ++x) {
		if (sub.at<unsigned char>(y,x) <= BLACK_TOLERANCE) {
			ret = false;
			++_bottom;
		}
	}
	for (x=0,y=0; y<sub.rows; ++y) {
		if (sub.at<unsigned char>(y,x) <= BLACK_TOLERANCE) {
			ret = false;
			++_left;
		}
	}
	for (x=sub.cols-1,y=0; y<sub.rows; ++y) {
		if (sub.at<unsigned char>(y,x) <= BLACK_TOLERANCE) {
			ret = false;
			++_right;
		}
	}

	if (_top > _bottom) {
		if (_top > _left && _top > _right) top=1;
	} else {
		if (_bottom > _left && _bottom > _right) bottom = 1;
	}

	if (_left >= _right) {
		if (_left >= _bottom && _left >= _top) left = 1;
	} else {
		if (_right >= _top && _right >= _bottom) right = 1;
	}
	return ret;
}
