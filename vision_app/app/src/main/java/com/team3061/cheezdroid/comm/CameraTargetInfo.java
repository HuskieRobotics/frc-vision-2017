package com.team3061.cheezdroid.comm;

import android.util.Log;

import org.json.JSONException;
import org.json.JSONObject;

public class CameraTargetInfo {
    protected double m_x; //distance, in ft
    protected double m_y;
    protected double m_z;
    protected double m_theta;

    // Coordinate frame:
    // +x is out the camera's optical axis
    // +y is to the left of the image
    // +z is to the top of the image
    // We assume the x component of all targets is +1.0 (since this is homogeneous)
    public CameraTargetInfo(double x, double y, double z, double theta) {
        m_x = x;
        m_y = y;
        m_z = z;
        m_theta = theta;
    }

    private double doubleize(double value) {
        double leftover = value % 1;
        if (leftover < 1e-7) {
            value += 1e-7;
        }
        return value;
    }

    public double getX() {
        return m_x;
    }

    public double getY() {
        return m_y;
    }

    public double getZ() {
        return m_z;
    }

    public double getTheta() {
        return m_theta;
    }

    public JSONObject toJson() {
        JSONObject j = new JSONObject();
        try {
            j.put("x", getX());
            j.put("y", doubleize(getY()));
            j.put("z", doubleize(getZ()));
            j.put("theta", getTheta());
        } catch (JSONException e) {
            Log.e("CameraTargetInfo", "Could not encode Json");
        }
        return j;
    }
}