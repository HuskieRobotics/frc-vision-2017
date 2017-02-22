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
  double centroid_x;
  double centroid_y;
  double width;
  double height;
  cv::Rect box;
  std::vector<cv::Point> contour;
  cv::RotatedRect rotatedRect;
};

std::vector<TargetInfo> processImpl(int w, int h, int texOut, DisplayMode mode,
                                    int h_min, int h_max, int s_min, int s_max,
                                    int v_min, int v_max) {
  LOGD("Image is %d x %d", w, h);
  LOGD("H %d-%d S %d-%d V %d-%d", h_min, h_max, s_min, s_max, v_min, v_max);
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

  t = getTimeMs();
  static cv::Mat contour_input;
  contour_input = thresh.clone();
  std::vector<std::vector<cv::Point>> contours;
  std::vector<cv::Point> convex_contour;
  std::vector<cv::Point> poly;
  std::vector<TargetInfo> targets;
  std::vector<TargetInfo> rejected_targets;
  cv::findContours(contour_input, contours, cv::RETR_EXTERNAL, //Find all extreme (outer) contours, save in contours.
                   cv::CHAIN_APPROX_TC89_KCOS);
  for (auto &contour : contours) {
      TargetInfo target;
      target.box = cv::boundingRect(contour);
      //target.rotatedRect = cv::minAreaRect(contour);
      target.centroid_x = 0;
      target.centroid_y = 0;

      //TODO: Measuring center of rectangle, need to measure center of mass
      //target.centroid_x += (target.box.tl().x + target.box.br().x)/2.0;
      //target.centroid_y += (target.box.tl().y + target.box.br().y)/2.0;

      target.centroid_x  = (target.box.tl().x + target.box.br().x)/2.0;//target.rotatedRect.center.x;
      target.centroid_y  = (target.box.tl().y + target.box.br().y)/2.0;//target.rotatedRect.center.y;

      target.width = target.box.width;
      target.height = target.box.height;

      // Filter based on size
      // Keep in mind width/height are in imager terms...
      const double kMinTargetWidth = 10;
      const double kMaxTargetWidth = 250;
      const double kMinTargetHeight = 10;
      const double kMaxTargetHeight = 250;
      if (target.width < kMinTargetWidth || target.width > kMaxTargetWidth ||
        target.height < kMinTargetHeight ||
        target.height > kMaxTargetHeight) {
        LOGD("Rejecting target due to size");
        rejected_targets.push_back(std::move(target));
        continue;
      }
      // Filter based on expected proportions
      const double kVertOverHorizontalMax = 1 / 1; //TODO: Find better values
      const double kVertOverHorizontalMin = 1 / 50;

      double actualVertOverHorizontal = target.height/target.width;

      if (actualVertOverHorizontal <= kVertOverHorizontalMin && actualVertOverHorizontal >= kVertOverHorizontalMax) {
        LOGD("Rejecting target due to shape");
        LOGD("proportions = %.2lf", actualVertOverHorizontal);
        rejected_targets.push_back(std::move(target));
        continue;
      }//*/

      /*
      // Filter based on fullness
      const double kMinFullness = .2;
      const double kMaxFullness = .5;
      double original_contour_area = cv::contourArea(contour);
      double poly_area = cv::contourArea(poly);
      double fullness = original_contour_area / poly_area;
      if (fullness < kMinFullness || fullness > kMaxFullness) {
        LOGD("Rejected target due to fullness");
        rejected_targets.push_back(std::move(target));
        continue;
      }//*/

      // We found a target
      LOGD("Found target at %.2lf, %.2lf...size %.2lf, %.2lf",
           target.centroid_x, target.centroid_y, target.width, target.height);
      targets.push_back(std::move(target));
  }
  //LOGD("Contour analysis costs %d ms", getTimeInterval(t));

  // write back
  t = getTimeMs();
  static cv::Mat vis;
  if (mode == DISP_MODE_RAW) {
    vis = input;
  } else if (mode == DISP_MODE_THRESH) {
    cv::cvtColor(thresh, vis, CV_GRAY2RGBA);

    //cv::drawContours(vis, contours, -1,cv::Scalar(255, 127, 0), 2);


    // Render the targets
    for (auto &target : targets) {
        //cv::polylines(vis, target.points, true, cv::Scalar(0, 112, 255), 3);//Blueish
        cv::circle(vis, cv::Point(target.centroid_x, target.centroid_y), 5,
                   cv::Scalar(0, 112, 255), 3);
        //cv::rotatedRectangle(vis, target.rotatedRect, cv::Scalar(112, 112, 30));
        cv::rectangle(vis, target.box, cv::Scalar(60, 112, 112));
    }
    /*for (auto &target : rejected_targets) {
        convex_contour.clear();
        cv::convexHull(target.contour, convex_contour, false); //Find the convex hull of the contour.
        cv::drawContours(vis, std::vector<std::vector<cv::Point> >(1,convex_contour), -1, cv::Scalar(255, 0, 0), 2);
        //cv::polylines(vis, target.points, true, cv::Scalar(255, 0, 0), 2);//Red
    }//*/

  } else {
    vis = input;
    // Render the targets
    for (auto &target : targets) {
      //cv::polylines(vis, target.points, true, cv::Scalar(0, 112, 255), 3);
      cv::circle(vis, cv::Point(target.centroid_x, target.centroid_y), 5,
                 cv::Scalar(0, 112, 255), 3);
    }
  }
  if (mode == DISP_MODE_TARGETS_PLUS) {
    for (auto &target : rejected_targets) {
      //cv::polylines(vis, target.points, true, cv::Scalar(255, 0, 0), 3);
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
}

extern "C" void processFrame(JNIEnv *env, int tex1, int tex2, int w, int h,
                             int mode, int h_min, int h_max, int s_min,
                             int s_max, int v_min, int v_max,
                             jobject destTargetInfo) {
  auto targets = processImpl(w, h, tex2, static_cast<DisplayMode>(mode), h_min,
                             h_max, s_min, s_max, v_min, v_max);
  int numTargets = targets.size();
  ensureJniRegistered(env);
  env->SetIntField(destTargetInfo, sNumTargetsField, numTargets);
  if (numTargets == 0) {
    return;
  }
  jobjectArray targetsArray = static_cast<jobjectArray>(
      env->GetObjectField(destTargetInfo, sTargetsField));
  for (int i = 0; i < std::min(numTargets, 3); ++i) {
    jobject targetObject = env->GetObjectArrayElement(targetsArray, i);
    const auto &target = targets[i];
    env->SetDoubleField(targetObject, sCentroidXField, target.centroid_x);
    env->SetDoubleField(targetObject, sCentroidYField, target.centroid_y);
    env->SetDoubleField(targetObject, sWidthField, target.width);
    env->SetDoubleField(targetObject, sHeightField, target.height);
  }
}
