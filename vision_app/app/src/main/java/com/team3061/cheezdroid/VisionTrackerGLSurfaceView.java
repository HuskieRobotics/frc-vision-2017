package com.team3061.cheezdroid;

import com.team3061.cheezdroid.comm.CameraTargetInfo;
import com.team3061.cheezdroid.comm.RobotConnection;
import com.team3061.cheezdroid.comm.VisionUpdate;
import com.team3061.cheezdroid.comm.messages.TargetUpdateMessage;
import com.team3061.cheezdroid.comm.messages.VisionMessage;

import org.opencv.android.BetterCamera2Renderer;
import org.opencv.android.BetterCameraGLSurfaceView;

import android.app.Activity;
import android.content.Context;
import android.hardware.camera2.CaptureRequest;
import android.os.Handler;
import android.os.Looper;
import android.util.AttributeSet;
import android.util.Log;
import android.util.Pair;
import android.view.SurfaceHolder;
import android.widget.TextView;
import android.widget.Toast;

import java.util.HashMap;

public class VisionTrackerGLSurfaceView extends BetterCameraGLSurfaceView implements BetterCameraGLSurfaceView.CameraTextureListener {

    static final String LOGTAG = "VTGLSurfaceView";
    protected int procMode = NativePart.DISP_MODE_THRESH;//NativePart.DISP_MODE_TARGETS_PLUS;
    public static final String[] PROC_MODE_NAMES = new String[]{"Raw image", "Threshholded image", "Targets", "Targets plus"};
    protected int frameCounter;
    protected long lastNanoTime;
    TextView mFpsText = null;
    private RobotConnection mRobotConnection;
    private Preferences m_prefs;

    static final int kHeight = 480;
    static final int kWidth = 640;
    static final double kCenterCol = ((double) kWidth) / 2.0 - .5;
    static final double kCenterRow = ((double) kHeight) / 2.0 - .5;

    static BetterCamera2Renderer.Settings getCameraSettings() {
        BetterCamera2Renderer.Settings settings = new BetterCamera2Renderer.Settings();
        settings.height = kHeight;
        settings.width = kWidth;
        settings.camera_settings = new HashMap<>();
        settings.camera_settings.put(CaptureRequest.CONTROL_MODE, CaptureRequest.CONTROL_MODE_OFF);
        settings.camera_settings.put(CaptureRequest.CONTROL_AE_MODE, CaptureRequest.CONTROL_AE_MODE_OFF);//TODO: only use FLASH_AE_MODE_ON while using FLASH_MODE_TORCH on the Galaxy ON5
        settings.camera_settings.put(CaptureRequest.FLASH_MODE, CaptureRequest.FLASH_MODE_OFF);
        settings.camera_settings.put(CaptureRequest.CONTROL_AWB_MODE, CaptureRequest.CONTROL_AWB_MODE_INCANDESCENT);
        settings.camera_settings.put(CaptureRequest.CONTROL_VIDEO_STABILIZATION_MODE, CaptureRequest.CONTROL_VIDEO_STABILIZATION_MODE_OFF);
        settings.camera_settings.put(CaptureRequest.LENS_OPTICAL_STABILIZATION_MODE, CaptureRequest.LENS_OPTICAL_STABILIZATION_MODE_OFF);
        settings.camera_settings.put(CaptureRequest.SENSOR_EXPOSURE_TIME, 10L);
        settings.camera_settings.put(CaptureRequest.CONTROL_AE_EXPOSURE_COMPENSATION, -4);
        settings.camera_settings.put(CaptureRequest.LENS_FOCUS_DISTANCE, 1000f);
        return settings;
    }

    public VisionTrackerGLSurfaceView(Context context, AttributeSet attrs) {
        super(context, attrs, getCameraSettings());
    }

    public void openOptionsMenu() {
        ((Activity) getContext()).openOptionsMenu();
    }

    @Override
    public void surfaceCreated(SurfaceHolder holder) {
        super.surfaceCreated(holder);
    }

    @Override
    public void surfaceDestroyed(SurfaceHolder holder) {
        super.surfaceDestroyed(holder);
    }

    public void setProcessingMode(int newMode) {
        if (newMode >= 0 && newMode < PROC_MODE_NAMES.length)
            procMode = newMode;
        else
            Log.e(LOGTAG, "Ignoring invalid processing mode: " + newMode);
    }

    public int getProcessingMode() {
        return procMode;
    }

    @Override
    public void onCameraViewStarted(int width, int height) {
        ((Activity) getContext()).runOnUiThread(new Runnable() {
            public void run() {
                Toast.makeText(getContext(), "onCameraViewStarted", Toast.LENGTH_SHORT).show();
            }
        });
        // NativePart.initCL();
        frameCounter = 0;
        lastNanoTime = System.nanoTime();
    }

    @Override
    public void onCameraViewStopped() {
        ((Activity) getContext()).runOnUiThread(new Runnable() {
            public void run() {
                Toast.makeText(getContext(), "onCameraViewStopped", Toast.LENGTH_SHORT).show();
            }
        });
    }

    @Override
    public boolean onCameraTexture(int texIn, int texOut, int width, int height, long image_timestamp) {
        Log.d(LOGTAG, "onCameraTexture - Timestamp " + image_timestamp + ", current time " + System.nanoTime() / 1E9);
        // FPS
        frameCounter++;
        if (frameCounter >= 30) {
            final int fps = (int) (frameCounter * 1e9 / (System.nanoTime() - lastNanoTime));
            Log.i(LOGTAG, "drawFrame() FPS: " + fps);
            if (mFpsText != null) {
                Runnable fpsUpdater = new Runnable() {
                    public void run() {
                        mFpsText.setText("FPS: " + fps);
                    }
                };
                new Handler(Looper.getMainLooper()).post(fpsUpdater);
            } else {
                Log.d(LOGTAG, "mFpsText == null");
                mFpsText = (TextView) ((Activity) getContext()).findViewById(R.id.fps_text_view);
            }
            frameCounter = 0;
            lastNanoTime = System.nanoTime();
        }
        NativePart.TargetsInfo targetsInfo = new NativePart.TargetsInfo();
        Pair<Integer, Integer> hRange = m_prefs != null ? m_prefs.getThresholdHRange() : blankPair();
        Pair<Integer, Integer> sRange = m_prefs != null ? m_prefs.getThresholdSRange() : blankPair();
        Pair<Integer, Integer> vRange = m_prefs != null ? m_prefs.getThresholdVRange() : blankPair();
        NativePart.processFrame(texIn, texOut, width, height, procMode, hRange.first, hRange.second,
                sRange.first, sRange.second, vRange.first, vRange.second, targetsInfo);

        VisionUpdate visionUpdate = new VisionUpdate(image_timestamp);
        Log.i(LOGTAG, "Num targets = " + targetsInfo.numTargets);
        for (int i = 0; i < targetsInfo.numTargets; ++i) {
            NativePart.TargetsInfo.Target target = targetsInfo.targets[i];

            double x = 6329.113924 / target.width ;
            double y = -(target.centroidX - kCenterCol) / getFocalLengthPixels();
            double z = (target.centroidY - kCenterRow) / getFocalLengthPixels();
            //x = x-((target.centroidX - kCenterCol)*(target.centroidX - kCenterCol)*3.733e-04)-((target.centroidY - kCenterRow)*(target.centroidY - kCenterRow)*3.733e-04);
            //TODO: This algorithm doesn't work. It appears that the distance depends on the width and the location, but the width+location are not able to be calculated independently.
            double theta = target.leftToRightRatio; //TODO: This isn't true!!!!! Need to make an algorithm to calculate the actual angle!
            Log.i(LOGTAG, "Target at: " + y + ", " + z + " d:" + x + " in");
            visionUpdate.addCameraTargetInfo(
                    new CameraTargetInfo(x, y, z, theta));
        }

        if (mRobotConnection != null) {
            TargetUpdateMessage update = new TargetUpdateMessage(visionUpdate, System.nanoTime());
            mRobotConnection.send(update);
        }
        return true;
    }

    public void setRobotConnection(RobotConnection robotConnection) {
        mRobotConnection = robotConnection;
    }

    public void setPreferences(Preferences prefs) {
        m_prefs = prefs;
    }

    private static Pair<Integer, Integer> blankPair() {
        return new Pair<Integer, Integer>(0, 255);
    }
}
