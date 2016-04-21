// Copyright (C) 2014 The Regents of the University of California (Regents).
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//
//     * Redistributions in binary form must reproduce the above
//       copyright notice, this list of conditions and the following
//       disclaimer in the documentation and/or other materials provided
//       with the distribution.
//
//     * Neither the name of The Regents or University of California nor the
//       names of its contributors may be used to endorse or promote products
//       derived from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//
// Please contact the author of this library if you have any questions.
// Author: Chris Sweeney (cmsweeney@cs.ucsb.edu)

#include "theia/sfm/camera/camera.h"

#include <ceres/rotation.h>
#include <Eigen/Core>
#include <Eigen/Geometry>
#include <glog/logging.h>

#include "theia/sfm/camera/projection_matrix_utils.h"
#include "theia/sfm/camera/project_point_to_image.h"
#include "theia/sfm/camera/radial_distortion.h"

namespace theia {

using Eigen::AngleAxisd;
using Eigen::Map;
using Eigen::Matrix;
using Eigen::Matrix3d;
using Eigen::Vector2d;
using Eigen::Vector3d;
using Eigen::Vector4d;

// Create a camera with the specified shared extrinsics.
Camera::Camera(std::shared_ptr<SharedExtrinsics> shared_extrinsics) {
  DCHECK_NOTNULL(shared_extrinsics.get());

  SetFocalLength(1.0);
  SetAspectRatio(1.0);
  SetSkew(0.0);
  SetPrincipalPoint(0.0, 0.0);
  SetRadialDistortion(0.0, 0.0);
  SetSharedToCameraTransform(Eigen::Matrix3d::Identity());
  SetSharedExtrinsics(shared_extrinsics);

  image_size_[0] = 0;
  image_size_[1] = 0;
}

// Create a camera with its own extrinsics.
Camera::Camera() : Camera(std::make_shared<SharedExtrinsics>()) {
}

bool Camera::InitializeFromProjectionMatrix(
      const int image_width,
      const int image_height,
      const Matrix3x4d projection_matrix) {
  DCHECK_GT(image_width, 0);
  DCHECK_GT(image_height, 0);
  image_size_[0] = image_width;
  image_size_[1] = image_height;

  Matrix3d world_to_camera_rotation;
  Vector3d position;
  Matrix3d calibration_matrix;
  DecomposeProjectionMatrix(projection_matrix,
                            &calibration_matrix,
                            &world_to_camera_rotation,
                            &position);

  const Eigen::Matrix3d world_to_shared_rotation = shared_to_camera_rotation.inverse() * world_to_camera_rotation;
  const Eigen::AngleAxisd world_to_shared_aa(world_to_shared_rotation);

  Map<Vector3d>(mutable_extrinsics().mutable_extrinsics() + SharedExtrinsics::ORIENTATION) = world_to_shared_aa.angle() * world_to_shared_aa.axis();
  Map<Vector3d>(mutable_extrinsics().mutable_extrinsics() + SharedExtrinsics::POSITION) = position;

  if (calibration_matrix(0, 0) == 0 || calibration_matrix(1, 1) == 0) {
    LOG(INFO) << "Cannot set focal lengths to zero!";
    return false;
  }

  CalibrationMatrixToIntrinsics(calibration_matrix,
                                mutable_intrinsics() + FOCAL_LENGTH,
                                mutable_intrinsics() + SKEW,
                                mutable_intrinsics() + ASPECT_RATIO,
                                mutable_intrinsics() + PRINCIPAL_POINT_X,
                                mutable_intrinsics() + PRINCIPAL_POINT_Y);
  return true;
}

void Camera::GetProjectionMatrix(Matrix3x4d* pmatrix) const {
  Matrix3d calibration_matrix;
  GetCalibrationMatrix(&calibration_matrix);
  ComposeProjectionMatrix(calibration_matrix,
                          GetOrientationAsRotationMatrix(),
                          GetPosition(),
                          pmatrix);
}

void Camera::GetCalibrationMatrix(Matrix3d* kmatrix) const {
  IntrinsicsToCalibrationMatrix(FocalLength(),
                                Skew(),
                                AspectRatio(),
                                PrincipalPointX(),
                                PrincipalPointY(),
                                kmatrix);
}

double Camera::ProjectPoint(const Vector4d& point, Vector2d* pixel) const {
  return ProjectPointToImage(extrinsics().extrinsics(),
                             intrinsics(),
                             point.data(),
                             GetSharedToCameraTransform(),
                             pixel->data());
}

Vector3d Camera::PixelToUnitDepthRay(const Vector2d& pixel) const {
  Vector3d direction;

  // First, undo the calibration.
  const double focal_length_y = FocalLength() * AspectRatio();
  const double y_normalized = (pixel[1] - PrincipalPointY()) / focal_length_y;
  const double x_normalized =
      (pixel[0] - PrincipalPointX() - y_normalized * Skew()) / FocalLength();

  // Undo radial distortion.
  const Vector2d normalized_point(x_normalized, y_normalized);
  Vector2d undistorted_point;
  RadialUndistortPoint(normalized_point,
                       RadialDistortion1(),
                       RadialDistortion2(),
                       &undistorted_point);

  // Apply rotation.
  const Matrix3d& rotation = GetOrientationAsRotationMatrix();
  direction = rotation.transpose() * undistorted_point.homogeneous();
  return direction;
}

  // ----------------------- Getter and Setter methods ---------------------- //
void Camera::SetPosition(const Vector3d& position) {
  Map<Vector3d>(mutable_extrinsics().mutable_extrinsics() + SharedExtrinsics::POSITION) = position;
}

Vector3d Camera::GetPosition() const {
  return Map<const Vector3d>(extrinsics().extrinsics() + SharedExtrinsics::POSITION);
}

void Camera::SetOrientationFromRotationMatrix(const Matrix3d& world_to_camera_rotation) {
  const Eigen::Matrix3d world_to_shared_rotation = shared_to_camera_rotation.inverse() * world_to_camera_rotation;

  ceres::RotationMatrixToAngleAxis(
      ceres::ColumnMajorAdapter3x3(world_to_shared_rotation.data()),
      mutable_extrinsics().mutable_extrinsics() + SharedExtrinsics::ORIENTATION);
}

void Camera::SetOrientationFromAngleAxis(const Vector3d& world_to_camera_angle_axis) {
  const Eigen::AngleAxisd world_to_camera_aa(world_to_camera_angle_axis.norm(), world_to_camera_angle_axis.normalized());
  const Eigen::AngleAxisd world_to_shared_aa(Eigen::Quaterniond(shared_to_camera_rotation).inverse() * world_to_camera_aa);

  Map<Vector3d>(mutable_extrinsics().mutable_extrinsics() + SharedExtrinsics::ORIENTATION) = world_to_shared_aa.angle() * world_to_shared_aa.axis();
}

Matrix3d Camera::GetOrientationAsRotationMatrix() const {
  Matrix3d world_to_shared_rotation;
  ceres::AngleAxisToRotationMatrix(
      extrinsics().extrinsics() + SharedExtrinsics::ORIENTATION,
      ceres::ColumnMajorAdapter3x3(world_to_shared_rotation.data()));
  return shared_to_camera_rotation * world_to_shared_rotation;
}

Vector3d Camera::GetOrientationAsAngleAxis() const {
  Eigen::AngleAxisd rotation_aa(GetOrientationAsRotationMatrix());
  return rotation_aa.angle() * rotation_aa.axis();
}

void Camera::SetFocalLength(const double focal_length) {
  mutable_intrinsics()[FOCAL_LENGTH] = focal_length;
}

double Camera::FocalLength() const {
  return intrinsics()[FOCAL_LENGTH];
}

void Camera::SetAspectRatio(const double aspect_ratio) {
  mutable_intrinsics()[ASPECT_RATIO] = aspect_ratio;
}
double Camera::AspectRatio() const {
  return intrinsics()[ASPECT_RATIO];
}

void Camera::SetSkew(const double skew) {
  mutable_intrinsics()[SKEW] = skew;
}

double Camera::Skew() const {
  return intrinsics()[SKEW];
}

void Camera::SetPrincipalPoint(const double principal_point_x,
                               const double principal_point_y) {
  mutable_intrinsics()[PRINCIPAL_POINT_X] = principal_point_x;
  mutable_intrinsics()[PRINCIPAL_POINT_Y] = principal_point_y;
}

double Camera::PrincipalPointX() const {
  return intrinsics()[PRINCIPAL_POINT_X];
}

double Camera::PrincipalPointY() const {
  return intrinsics()[PRINCIPAL_POINT_Y];
}

void Camera::SetRadialDistortion(const double radial_distortion_1,
                                 const double radial_distortion_2) {
  mutable_intrinsics()[RADIAL_DISTORTION_1] = radial_distortion_1;
  mutable_intrinsics()[RADIAL_DISTORTION_2] = radial_distortion_2;
}

double Camera::RadialDistortion1() const {
  return intrinsics()[RADIAL_DISTORTION_1];
}

double Camera::RadialDistortion2() const {
  return intrinsics()[RADIAL_DISTORTION_2];
}

void Camera::SetImageSize(const int image_width, const int image_height) {
  image_size_[0] = image_width;
  image_size_[1] = image_height;
}

}  // namespace theia
