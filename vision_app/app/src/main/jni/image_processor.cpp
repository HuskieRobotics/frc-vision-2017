#include "image_processor.h"

#include <algorithm>

#include <GLES2/gl2.h>
#include <EGL/egl.h>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/core/ocl.hpp>

#include "common.hpp"

enum DisplayMode {
  DISP_MODE_RAW = 0,
  DISP_MODE_THRESH = 1,
  DISP_MODE_TARGETS = 2,
  DISP_MODE_TARGETS_PLUS = 3
};

struct TargetInfo {
  TargetInfo(): isGeneratedPair(false) { }
  double centroid_x;
  double centroid_y;
  double width;
  double height;
  double leftToRightRatio;
  bool isGeneratedPair;
  cv::Rect box;
  std::vector<cv::Point> contour;
};

std::vector<TargetInfo> processImpl(int w, int h, int texOut, DisplayMode mode,
                                    int h_min, int h_max, int s_min, int s_max,
                                    int v_min, int v_max) {
  //LOGD("Image is %d x %d", w, h);
  //LOGD("H %d-%d S %d-%d V %d-%d", h_min, h_max, s_min, s_max, v_min, v_max);
  int64_t t;

  static cv::Mat input;
  input.create(h, w, CV_8UC4);

  // read
  t = getTimeMs();
  glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, input.data);
  //LOGD("glReadPixels() costs %d ms", getTimeInterval(t));

  // modify color scales
  t = getTimeMs();
  static cv::Mat hsv;
  cv::cvtColor(input, hsv, CV_RGBA2RGB);
  cv::cvtColor(hsv, hsv, CV_RGB2HSV);
  //LOGD("cvtColor() costs %d ms", getTimeInterval(t));

  //Threshold image
  t = getTimeMs();
  static cv::Mat thresh;
  cv::inRange(hsv, cv::Scalar(h_min, s_min, v_min),
              cv::Scalar(h_max, s_max, v_max), thresh);
  //LOGD("inRange() costs %d ms", getTimeInterval(t));
  int threshChannels = thresh.channels();

  t = getTimeMs();
  static cv::Mat contour_input;
  contour_input = thresh.clone();
  std::vector<std::vector<cv::Point>> contours;
  std::vector<cv::Point> convex_contour;
  std::vector<TargetInfo> targets;
  std::vector<TargetInfo> target_parts;
  std::vector<TargetInfo> rejected_targets;
  cv::findContours(contour_input, contours, cv::RETR_EXTERNAL, //Find all extreme (outer) contours, save in contours.
                   cv::CHAIN_APPROX_TC89_KCOS);
  for (auto &contour : contours) {
      TargetInfo target;
      target.box = cv::boundingRect(contour);

      target.centroid_x  = (target.box.tl().x + target.box.br().x)/2.0;
      target.centroid_y  = (target.box.tl().y + target.box.br().y)/2.0;

      target.width = target.box.width;
      target.height = target.box.height;
      target.leftToRightRatio = 0;

      // Filter based on size
      // Keep in mind width/height are in imager terms...
      const double kMinTargetWidth = 4;
      const double kMaxTargetWidth = 250;
      const double kMinTargetHeight = 5;
      const double kMaxTargetHeight = 250;
      if (target.width < kMinTargetWidth || target.width > kMaxTargetWidth ||
        target.height < kMinTargetHeight ||
        target.height > kMaxTargetHeight) {
        //LOGD("Rejecting target due to size");
        rejected_targets.push_back(std::move(target));
        continue;
      }
      // Filter based on expected proportions
      const double kVertOverHorizontalMax = 6.0;
      const double kVertOverHorizontalMin = 0.3;//2.0 for a full target, .3 for a possibly split target

      double actualVertOverHorizontal = target.height/target.width;

      if (actualVertOverHorizontal <= kVertOverHorizontalMin || actualVertOverHorizontal >= kVertOverHorizontalMax) {
        LOGD("Rejecting target due to shape: proportions = %.2lf", actualVertOverHorizontal);
        rejected_targets.push_back(std::move(target));
        continue;
      }


      const double kMinFullness = .70;
      const double kMaxFullness = 1;
      double original_contour_area = cv::contourArea(contour);
      // accept only char type matrices
      CV_Assert(thresh.depth() == CV_8U);
      int i,j;
      int xStart, xStop, yStop;
      int whiteCnt = 0;
      xStart = target.box.tl().x;
      xStop = target.box.br().x;
      yStop = target.box.br().y;
      uchar* p;
      for( i = target.box.tl().y; i < yStop; ++i)
      {
          p = thresh.ptr<uchar>(i);
          for ( j = xStart; j < xStop; j+=threshChannels)
          {
              whiteCnt += (p[j]==255) ? 1 : 0;
          }
      }
      double fullness = whiteCnt*1.0/target.box.area();//original_contour_area / target.box.area();
      if (fullness < kMinFullness || fullness > kMaxFullness) {
        LOGD("Rejected target due to fullness: %.2lf", fullness);
        rejected_targets.push_back(std::move(target));
        continue;
      }

      target_parts.push_back(std::move(target));
  }
  //LOGD("Contour analysis costs %d ms", getTimeInterval(t));


  // Look for pairs that are aligned vertically, and may represent two halves of a target, separated by the lift.
  const double kWidthMaxError = 0.075;
  const double kHorizontalLocationMaxError = 0.04;

  static int target_parts_len;
  static double min_altitude, max_altitude;
  static double max_width, max_height;
  static double proportions;

  target_parts_len = target_parts.size();
  for(int i=0; i<target_parts_len; ++i)
    for(int j=i+1; j<target_parts_len; ++j)
    {

        const auto &target1 = target_parts[i];
        const auto &target2 = target_parts[j];

        max_width = std::max(target1.width, target2.width);
        if ((std::abs(target1.width-target2.width)/max_width) < kWidthMaxError)//If widths within 15%
        {
          if ((std::abs(target1.centroid_x-target2.centroid_x)/std::max(target1.centroid_x, target2.centroid_x)) < kHorizontalLocationMaxError)//If horizontally aligned within 15%
          {
            min_altitude = std::min(target1.box.tl().y, target2.box.tl().y);
            max_altitude = std::max(target1.box.br().y, target2.box.br().y);
            max_height = max_altitude-min_altitude;
            proportions = max_height/max_width;
            if ((proportions<6.0)&&(proportions>2.0))//If [max_height - min_height]/width in range [2.0,6.0]
            {
              TargetInfo newTargetPair;
              newTargetPair.isGeneratedPair = true;
              newTargetPair.box = cv::Rect(std::min(target1.box.tl().x,target2.box.tl().x),min_altitude,max_width,max_height);
              newTargetPair.box.width = max_width;
              newTargetPair.box.height= max_height;
              newTargetPair.centroid_x = (newTargetPair.box.tl().x + newTargetPair.box.br().x)/2.0;
              newTargetPair.centroid_y = (newTargetPair.box.tl().y + newTargetPair.box.br().y)/2.0;
              target_parts.push_back(std::move(newTargetPair));//Add new target part to target_parts (appending to end is ok, since target_parts.size is saved, not calculated)
            }
          }
        }
    }

  target_parts_len = target_parts.size(); //Recalculate, since we have possibly added some partial pairs
  static double altitude_err_top, altitude_err_bottom;
  static double width;
  const double kAltMaxError = 0.25;
  for(int i=0; i<target_parts_len; ++i)
    for(int j=i+1; j<target_parts_len; ++j)
    {
      const auto &target1 = target_parts[i];
      const auto &target2 = target_parts[j];
      if ((target1.box & target2.box).area() == 0)//If boxes do not overlap (Look for area of the intersection of the rects)
      {
        altitude_err_top = double(std::abs(target1.box.tl().y - target2.box.tl().y)) / double(std::max(target1.height, target2.height));
        altitude_err_bottom = double(std::abs(target1.box.br().y - target2.box.br().y)) / double(std::max(target1.height, target2.height));
        LOGD("Altitude Err: %.2lf, %.2lf", altitude_err_top, altitude_err_bottom);
        if (altitude_err_top < kAltMaxError && altitude_err_bottom < kAltMaxError)// If bottom and top of boxes align within 12.5%
        {
          max_height = std::max(target1.box.br().y, target2.box.br().y) - std::min(target1.box.tl().y, target2.box.tl().y);//max_height = max(target1.top, target2.top) - min(target1.bottom, target2.bottom);
          width = std::abs(target1.centroid_x - target2.centroid_x);
          LOGD("max_height: %.2lf, width: %.2lf", max_height, width);
          if ((0.5*max_height)<width && width < (2.25*max_height) ) //if (width in range(.5*max_height, 1.75*max_height)
          {
            TargetInfo full_target;//Generate combined target
            full_target.box = target1.box | target2.box; //Rectangle that encloses both smaller rects
            full_target.height = full_target.box.height;
            full_target.width = full_target.box.width;
            full_target.centroid_x = full_target.box.x + (full_target.box.width/2);
            full_target.centroid_y = full_target.box.y + (full_target.box.height/2);
            if (target1.box.x < target2.box.x) //Is target1 on the left?
            {
                full_target.leftToRightRatio = target1.box.area() * 1.0 / target2.box.area();
            }
            else
            {
                full_target.leftToRightRatio = target2.box.area() * 1.0 / target1.box.area();
            }
            targets.push_back(std::move(full_target));// We found a target
            LOGE("Found target at %.2lf, %.2lf...size %.2lf, %.2lf... ratio %.2lf",
            full_target.centroid_x, full_target.centroid_y, full_target.width, full_target.height, full_target.leftToRightRatio);//*/
          }
        }
      }
    }

    //TODO: Remove this section
    /*TargetInfo full_target2;//Generate combined target
    full_target2.box = cv::Rect(10,10,40,40); //Rectangle that encloses both smaller rects
    full_target2.height = full_target2.box.height;
    full_target2.width = full_target2.box.width;
    full_target2.centroid_x = full_target2.box.x + (full_target2.box.width/2);
    full_target2.centroid_y = full_target2.box.y + (full_target2.box.height/2);

    targets.push_back(std::move(full_target2));// We found a target
    */

  // write back
  t = getTimeMs();
  static cv::Mat vis;
  if (mode == DISP_MODE_RAW) {
    vis = input;
  } else if (mode == DISP_MODE_THRESH) {
    cv::cvtColor(thresh, vis, CV_GRAY2RGBA);

    // Render the targets
    for (auto &target : target_parts) {
        cv::rectangle(vis, target.box, cv::Scalar(200, 20, 200), 1);
    }
    for (auto &target : rejected_targets) {
        cv::rectangle(vis, target.box, cv::Scalar(255, 10, 0), 1);
    }
    for (auto &target : targets) {
        cv::circle(vis, cv::Point(target.centroid_x, target.centroid_y), 5,
                   cv::Scalar(0, 190, 255), 3);
        cv::rectangle(vis, target.box, cv::Scalar(10, 255, 10), 2);
    }

  } else {
    vis = input;
    // Render the targets
    for (auto &target : targets) {
        cv::circle(vis, cv::Point(target.centroid_x, target.centroid_y), 5,
                   cv::Scalar(0, 190, 255), 3);
        cv::rectangle(vis, target.box, cv::Scalar(10, 255, 10), 2);

    }
  }
  if (mode == DISP_MODE_TARGETS_PLUS) {
    for (auto &target : target_parts) {
        cv::rectangle(vis, target.box, cv::Scalar(200, 20, 200), 1);
    }
    for (auto &target : rejected_targets) {
        cv::rectangle(vis, target.box, cv::Scalar(255, 10, 0), 1);
    }
  }
  //LOGD("Creating vis costs %d ms", getTimeInterval(t));

  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, texOut);
  t = getTimeMs();
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE,
                  vis.data);
  //LOGD("glTexSubImage2D() costs %d ms", getTimeInterval(t));

  return targets;
}

static bool sFieldsRegistered = false;

static jfieldID sNumTargetsField;
static jfieldID sTargetsField;

static jfieldID sCentroidXField;
static jfieldID sCentroidYField;
static jfieldID sWidthField;
static jfieldID sHeightField;
static jfieldID sLeftToRightRatioField;

static void ensureJniRegistered(JNIEnv *env) {
  if (sFieldsRegistered) {
    return;
  }
  sFieldsRegistered = true;
  jclass targetsInfoClass =
      env->FindClass("com/team3061/cheezdroid/NativePart$TargetsInfo");
  sNumTargetsField = env->GetFieldID(targetsInfoClass, "numTargets", "I");
  sTargetsField = env->GetFieldID(
      targetsInfoClass, "targets",
      "[Lcom/team3061/cheezdroid/NativePart$TargetsInfo$Target;");
  jclass targetClass =
      env->FindClass("com/team3061/cheezdroid/NativePart$TargetsInfo$Target");

  sCentroidXField = env->GetFieldID(targetClass, "centroidX", "D");
  sCentroidYField = env->GetFieldID(targetClass, "centroidY", "D");
  sWidthField = env->GetFieldID(targetClass, "width", "D");
  sHeightField = env->GetFieldID(targetClass, "height", "D");
  sLeftToRightRatioField = env->GetFieldID(targetClass, "leftToRightRatio", "D");
}

extern "C" void processFrame(JNIEnv *env, int tex1, int tex2, int w, int h,
                             int mode, int h_min, int h_max, int s_min,
                             int s_max, int v_min, int v_max,
                             jobject destTargetInfo) {
  auto targets = processImpl(w, h, tex2, static_cast<DisplayMode>(mode), h_min,
                             h_max, s_min, s_max, v_min, v_max);
  int numTargets = targets.size();
  numTargets = std::min(numTargets, 3); //Limit to 3 targets
  ensureJniRegistered(env);
  env->SetIntField(destTargetInfo, sNumTargetsField, numTargets);
  if (numTargets == 0) {
    return;
  }
  jobjectArray targetsArray = static_cast<jobjectArray>(
      env->GetObjectField(destTargetInfo, sTargetsField));
  for (int i = 0; i < numTargets; ++i) {
    jobject targetObject = env->GetObjectArrayElement(targetsArray, i);
    const auto &target = targets[i];
    env->SetDoubleField(targetObject, sCentroidXField, target.centroid_x);
    env->SetDoubleField(targetObject, sCentroidYField, target.centroid_y);
    env->SetDoubleField(targetObject, sWidthField, target.width);
    env->SetDoubleField(targetObject, sHeightField, target.height);
    env->SetDoubleField(targetObject, sLeftToRightRatioField, target.leftToRightRatio);
  }
}
