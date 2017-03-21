#include "StitchingUtil.h"
#include "Supplements\Matchers.h"
#include "Supplements\RewarpableWarper.h"

using namespace cv::detail;
StitchingInfo StitchingUtil::opencvSelfStitching(
	const std::vector<Mat> &srcs, Mat &dstImage, StitchingInfo &sInfo, std::pair<double, double> &maskRatio) {
		Size sz = srcs[0].size();
		if (sInfo.isNull()) {
			for (auto src:srcs) {
				if (src.size().area() < sz.area()) sz = src.size();
			}
		}
		return opencvSelfStitching(srcs,dstImage, sz, sInfo, maskRatio);
}

StitchingInfo StitchingUtil::opencvSelfStitching(
	const std::vector<Mat> &srcs, Mat &dstImage, const Size resizeSz,StitchingInfo &sInfoNotNull, std::pair<double, double> &maskRatio) {
	StitchingInfo sInfo;

	double work_scale = 1, seam_scale = 1, compose_scale = 1;
	double seam_work_aspect = 1;
	Mat full_img1,full_img, img;
	int imgCnt = srcs.size(); 
	float warped_image_scale;
	std::vector<CameraParams> cameras;
	std::vector<Mat> images(imgCnt);
	std::vector<Size> full_img_sizes(imgCnt);

	Ptr<FeaturesFinder> finder;
	finder = new supp::SIFTFeaturesFinder();
	std::vector<ImageFeatures> features(imgCnt);
	std::vector<MatchesInfo> pairwise_matches;
	HomographyBasedEstimator estimator;


	if (!sInfoNotNull.isNull()) {
		assert(imgCnt == sInfoNotNull.imgCnt);
		sInfo.imgCnt = sInfoNotNull.imgCnt;
		sInfo.maskRatio = sInfoNotNull.maskRatio;
		sInfo.resizeSz = sInfoNotNull.resizeSz;
		for (int i = 0; i < imgCnt; ++i) {
			full_img1 = srcs[i].clone();
			//LOG_WARN("Orig Size:" << full_img1.size());
			//assert(full_img1.size().width >= resizeSz[i].width && full_img1.size().height >= resizeSz[i].height);
			_resize_(full_img1,full_img, sInfo.resizeSz,0,0);
			full_img_sizes[i] = full_img.size();
			work_scale = min(1.0, sqrt(osParam.workMegapix * 1e6 / full_img.size().area()));
			_resize_(full_img, img, Size(), work_scale, work_scale);
			seam_scale = min(1.0, sqrt(osParam.seamMegapix * 1e6 / full_img.size().area()));
			seam_work_aspect = seam_scale / work_scale;
			_resize_(full_img, img, Size(), seam_scale, seam_scale);
			images[i] = img.clone();
		}
		sInfoNotNull.setToCamerasInternalParam(cameras);
		warped_image_scale = sInfoNotNull.getWarpScale();

	} else {

		
		sInfo.imgCnt = imgCnt;
		sInfo.maskRatio = maskRatio;
		sInfo.resizeSz = resizeSz;
		LOG_MESS("Finding features... with MaskRatio (" << sInfo.maskRatio.first << "," << sInfo.maskRatio.second <<")");
		for (int i = 0; i < imgCnt; ++i) {
			full_img1 = srcs[i].clone();
			//LOG_WARN("Orig Size:" << full_img1.size());
			//assert(full_img1.size().width >= resizeSz[i].width && full_img1.size().height >= resizeSz[i].height);
			_resize_(full_img1,full_img, sInfo.resizeSz, 0,0);
			full_img_sizes[i] = full_img.size();
			work_scale = min(1.0, sqrt(osParam.workMegapix * 1e6 / full_img.size().area()));

			_resize_(full_img, img, Size(), work_scale, work_scale);
			seam_scale = min(1.0, sqrt(osParam.seamMegapix * 1e6 / full_img.size().area()));
			seam_work_aspect = seam_scale / work_scale;
			(*finder)(img, features[i],StitchingUtil::getMaskROI(img, i,imgCnt, sInfo.maskRatio));
			//LOG_MESS(features[i].keypoints[0].pt.x << "," <<features[i].keypoints[0].pt.y);
			//LOG_MESS(features[i].keypoints[1].pt.x << "," <<features[i].keypoints[1].pt.y);system("pause");
			features[i].img_idx = i;
			LOG_MESS("Features in image #" << i+1 << ": " << features[i].keypoints.size());
			_resize_(full_img, img, Size(), seam_scale, seam_scale);
			images[i] = img.clone();
		}
		
		
		finder->collectGarbage();
		full_img.release();
		img.release();

		LOG_MESS("Pairwise matching ...");
		BestOf2NearestMatcher matcher(false, osParam.match_conf);
		matcher(features, pairwise_matches); 
		matcher.collectGarbage();
		estimator = HomographyBasedEstimator(false);
		estimator(features, pairwise_matches, cameras);
	
	
		for (size_t i = 0; i < cameras.size(); ++i) {
			Mat R;
			cameras[i].R.convertTo(R, CV_32F);
			cameras[i].R = R;
			LOG_MESS("Initial intrinsics #" << i+1 << ":\n" << cameras[i].K());
			LOG_MESS("Initial intrinsics R #" << i+1 << ":\n" << cameras[i].R);
			LOG_MESS("Initial intrinsics t #" << i+1 << ":\n" << cameras[i].t);
			//system("pause");
		}

		Ptr<detail::BundleAdjusterBase> adjuster;
		adjuster = new detail::BundleAdjusterRay();

		adjuster->setConfThresh(osParam.conf_thresh);
		Mat_<uchar> refine_mask = Mat::zeros(3, 3, CV_8U);
		refine_mask(0,0) = 1;
		refine_mask(0,1) = 1;
		refine_mask(0,2) = 1;
		refine_mask(1,1) = 1;
		refine_mask(1,2) = 1;
		adjuster->setRefinementMask(refine_mask);
		(*adjuster)(features, pairwise_matches, cameras);

	
		std::vector<double> focals;
		for (size_t i = 0; i < cameras.size(); ++i) {
			LOG_MESS("Camera #" << i+1 << ":\n" << cameras[i].K());
			focals.push_back(cameras[i].focal);
		}

		sort(focals.begin(), focals.end());
		warped_image_scale =(focals[(focals.size()-1) / 2] + focals[focals.size() / 2]) * 0.5f; 

		std::vector<Mat> rmats;
		for (size_t i = 0; i < cameras.size(); ++i)
			rmats.push_back(cameras[i].R);
		waveCorrect(rmats, osParam.wave_correct);
		for (size_t i = 0; i < cameras.size(); ++i)
			cameras[i].R = rmats[i];
		
	}
	
	sInfo.setFromCamerasInternalParam(cameras);
	if (cameras.size() == 2) {
		double relative_focal_ratio = abs((cameras[1].focal-cameras[0].focal)*1.0/cameras[0].focal);
		if (relative_focal_ratio < ERR) {
			LOG_ERR("Terrible cam estimation.")
			return sInfo;
		}
			
	}

	LOG_MESS("Warping images ... ");


	std::vector<Point> corners(imgCnt);
	std::vector<UMat> masks_warped(imgCnt);
	std::vector<UMat> images_warped(imgCnt);
	std::vector<Size> sizes(imgCnt);
	std::vector<Mat> masks(imgCnt);

	for (int i = 0; i < imgCnt; ++i) {
		masks[i].create(images[i].size(), CV_8U);
		masks[i].setTo(Scalar::all(255));
		//masks[i] = getMask(images[i],i==0);
	}

	/**
	* Spherical is nearly deprecated
	* Merator is slower but better than Cylindrical
	*/

	//supp::RewarpableMercatorWarper* warper = new supp::RewarpableMercatorWarper(warped_image_scale * seam_work_aspect);
	supp::RewarpableCylindricalWarper* warper = new supp::RewarpableCylindricalWarper(warped_image_scale * seam_work_aspect);

	if (!sInfoNotNull.isNull())
		warper->setProjectorData(sInfoNotNull.warpData);

	for (int i = 0; i < imgCnt; ++i) {
		Mat_<float> K;
		cameras[i].K().convertTo(K, CV_32F);
		float swa = (float)seam_work_aspect;
		K(0,0) *= swa; K(0,2) *= swa;
		K(1,1) *= swa; K(1,2) *= swa;

		corners[i] = warper->warp(images[i], K, cameras[i].R, INTER_LINEAR, BORDER_REFLECT, images_warped[i]);//Calculate the unite corner
		sizes[i] = images_warped[i].size();

		warper->warp(masks[i], K, cameras[i].R, INTER_NEAREST, BORDER_CONSTANT, masks_warped[i]);
	}

	std::vector<UMat> images_warped_f(imgCnt);
	for (int i = 0; i < imgCnt; ++i)
		images_warped[i].convertTo(images_warped_f[i], CV_32F);


	Ptr<ExposureCompensator> compensator = ExposureCompensator::createDefault(osParam.expos_comp_type);
	compensator->feed(corners, images_warped, masks_warped);

	Ptr<SeamFinder> seam_finder;
	seam_finder = new detail::GraphCutSeamFinder(GraphCutSeamFinderBase::COST_COLOR);
	seam_finder->find(images_warped_f, corners, masks_warped);

	images.clear();
	images_warped.clear();
	images_warped_f.clear();
	masks.clear();

	LOG_MESS("Compositing...");

	Mat img_warped, img_warped_s;
	Mat dilated_mask, seam_mask, mask, mask_warped;
	Ptr<Blender> blender;
	
	double compose_work_aspect = 1;

	for (int img_idx = 0; img_idx < imgCnt; ++img_idx) {
		LOG_MESS("Compositing image #" << img_idx+1);
		// reCalculate corner and mask since the former estimation is based on work_scale
		
		full_img1 = srcs[img_idx].clone();
		_resize_(full_img1,full_img, sInfo.resizeSz, 0,0);
		compose_scale = min(1.0, sqrt(osParam.composeMegapix * 1e6 / full_img.size().area()));
		compose_work_aspect = compose_scale / work_scale;
		warped_image_scale *= static_cast<float>(compose_work_aspect);
		//warper = warper_creator->create(warped_image_scale);
		warper->setScale(warped_image_scale);

		// Update
		for (int i = 0; i < imgCnt; ++i) {
			cameras[i].focal *= compose_work_aspect;
			cameras[i].ppx *= compose_work_aspect;
			cameras[i].ppy *= compose_work_aspect;


			Size sz = full_img_sizes[i];
			if (std::abs(compose_scale - 1) > 1e-1) {
				sz.width = round(full_img_sizes[i].width * compose_scale);
				sz.height = round(full_img_sizes[i].height * compose_scale);
			}

			Mat K;
			cameras[i].K().convertTo(K, CV_32F);
			Rect roi = warper->warpRoi(sz, K, cameras[i].R);
			corners[i] = roi.tl();
			sizes[i] = roi.size();
		}
	
		if (abs(compose_scale - 1) > 1e-1)
			_resize_(full_img, img, Size(), compose_scale, compose_scale);
		else
			img = full_img;
		full_img.release();
		Size img_size = img.size();
	
		Mat K;
		cameras[img_idx].K().convertTo(K, CV_32F);
		warper->warp(img, K, cameras[img_idx].R, INTER_LINEAR, BORDER_REFLECT, img_warped);
		mask.create(img_size, CV_8U);
		mask.setTo(Scalar::all(255));
		warper->warp(mask, K, cameras[img_idx].R, INTER_NEAREST, BORDER_CONSTANT, mask_warped);
		compensator->apply(img_idx, corners[img_idx], img_warped, mask_warped);

		img_warped.convertTo(img_warped_s, CV_16S);
		img_warped.release();
		img.release();
		mask.release();

		dilate(masks_warped[img_idx], dilated_mask, Mat());
		_resize_(dilated_mask, seam_mask, mask_warped.size(),0,0);
		mask_warped = seam_mask & mask_warped;
		if (blender.empty()) {
			blender = Blender::createDefault(osParam.blend_type, false);
			Size dst_sz = resultRoi(corners, sizes).size();
			float blend_width = sqrt(static_cast<float>(dst_sz.area())) * osParam.blend_strength / 100.f;
			if (blend_width < 1.f)
				blender = Blender::createDefault(Blender::NO, false);
			else if (osParam.blend_type == Blender::MULTI_BAND) {
				MultiBandBlender * mb = dynamic_cast<MultiBandBlender*>(static_cast<Blender*>(blender));
				mb->setNumBands(static_cast<int>(ceil(log(blend_width)/log(2.0)) - 1.0));
				LOG_MESS("Multi-band blender, number of bands: " << mb->numBands());
			} else if (osParam.blend_type == Blender::FEATHER) {
				FeatherBlender *fb = dynamic_cast<FeatherBlender*>(static_cast<Blender*>(blender));
				fb->setSharpness(1.0/blend_width);
				LOG_MESS("Feather blender, sharpness " << fb->sharpness());
			}

			blender->prepare(corners, sizes);

		}

		blender->feed(img_warped_s, mask_warped, corners[img_idx]);

	}

	sInfo.warpData = (warper)->getProjectorAllData();
	sInfo.setRanges(corners, sizes);

	Mat result, result_mask, tmp;
	blender->blend(result, result_mask);
	result.convertTo(tmp, CV_8UC3);
	removeBlackPixel(tmp, dstImage, sInfo);
	LOG_MESS("Size of Pano:" << dstImage.size());
	if (warper != NULL) delete warper;
	return sInfo;
}
