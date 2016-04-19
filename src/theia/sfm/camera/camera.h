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

#ifndef THEIA_SFM_CAMERA_CAMERA_H_
#define THEIA_SFM_CAMERA_CAMERA_H_

#include <cereal/cereal.hpp>
#include <cereal/types/memory.hpp>
#include <cereal/access.hpp>
#include <Eigen/Core>
#include <Eigen/Geometry>
#include <vector>

#include "theia/sfm/types.h"
#include "theia/sfm/camera/shared_extrinsics.h"

namespace theia {

// This class contains the full camera pose information including extrinsic
// parameters as well as intrinsic parameters. Extrinsic parameters include the
// camera orientation (as angle-axis) and position, and intrinsic parameters
// include focal length, aspect ratio, skew, principal points, and (up to
// 2-parameter) radial distortion. Methods are provided for common
// transformations and projections.
//
// Intrinsics of the camera are modeled such that:
//
//  K = [f     s     px]
//      [0   f * a   py]
//      [0     0      1]
//
// where f = focal length, px and py is the principal point, s = skew, and
// a = aspect ratio.
//
// Extrinsic parametser transform the homogeneous 3D point X to the image point
// p such that:
//   p = R * (X[0..2] / X[3] - C);
//   p = p[0,1] / p[2];
//   r = p[0] * p[0] + p[1] * p[1];
//   d = 1 + k1 * r + k2 * r * r;
//   p *= d;
//   p = K * p;
//
//  where R = orientation, C = camera position, and k1 k2 are the radial
//  distortion parameters.
class Camera {
 public:
  EIGEN_MAKE_ALIGNED_OPERATOR_NEW

  Camera();
  Camera(std::shared_ptr<SharedExtrinsics> shared_extrinsics);
  ~Camera() {}

  // Initializes the camera intrinsic and extrinsic parameters from the
  // projection matrix by decomposing the matrix.
  //
  // NOTE: The projection matrix does not contain information about radial
  // distortion, so those parameters will need to be set separately.
  bool InitializeFromProjectionMatrix(
      const int image_width,
      const int image_height,
      const Matrix3x4d projection_matrix);

  // ---------------------------- Helper methods ---------------------------- //
  // Returns the projection matrix. Does not include radial distortion.
  void GetProjectionMatrix(Matrix3x4d* pmatrix) const;

  // Returns the calibration matrix in the form specified above.
  void GetCalibrationMatrix(Eigen::Matrix3d* kmatrix) const;

  // Projects the homogeneous 3D point into the image plane and undistorts the
  // point according to the radial distortion parameters. The function returns
  // the depth of the point so that points that project behind the camera (i.e.,
  // negative depth) can be determined. Points at infinity return a depth of
  // infinity.
  double ProjectPoint(const Eigen::Vector4d& point,
                      Eigen::Vector2d* pixel) const;

  // Converts the pixel point to a ray in 3D space such that the origin of the
  // ray is at the camera center and the direction is the pixel direction
  // rotated according to the camera orientation in 3D space.
  //
  // NOTE: The depth of the ray is set to 1. This is so that we can remain
  // consistent with ProjectPoint. That is:
  //      if we have:
  //         d = ProjectPoint(X, &x);
  //         r = PixelToRay(x);
  //      then it will be the case that
  //         X = c + r * d;
  //    X is the 3D point
  //    x is the image projection of X
  //    c is the camera position
  //    r is the ray obtained from PixelToRay
  //    d is the depth of the 3D point with respect to the image
  Eigen::Vector3d PixelToUnitDepthRay(const Eigen::Vector2d& pixel) const;

  // ----------------------- Getter and Setter methods ---------------------- //
  void SetPosition(const Eigen::Vector3d& position);
  Eigen::Vector3d GetPosition() const;

  void SetOrientationFromRotationMatrix(const Eigen::Matrix3d& world_to_local_rotation);
  void SetOrientationFromAngleAxis(const Eigen::Vector3d& angle_axis);
  Eigen::Matrix3d GetOrientationAsRotationMatrix() const;
  Eigen::Vector3d GetOrientationAsAngleAxis() const;

  void SetFocalLength(const double focal_length);
  double FocalLength() const;

  void SetAspectRatio(const double aspect_ratio);
  double AspectRatio() const;

  void SetSkew(const double skew);
  double Skew() const;

  void SetPrincipalPoint(const double principal_point_x,
                         const double principal_point_y);
  double PrincipalPointX() const;
  double PrincipalPointY() const;

  void SetRadialDistortion(const double radial_distortion_1,
                           const double radial_distortion_2);
  double RadialDistortion1() const;
  double RadialDistortion2() const;

  void SetImageSize(const int image_width, const int image_height);
  int ImageWidth() const { return image_size_[0]; }
  int ImageHeight() const { return image_size_[1]; }

  const double* intrinsics() const {
    return camera_parameters_;
  }
  double* mutable_intrinsics() { return camera_parameters_; }

  const SharedExtrinsics& extrinsics() const { return *shared_extrinsics; }

  SharedExtrinsics& mutable_extrinsics() { return *shared_extrinsics; }

  void SetSharedExtrinsics(std::shared_ptr<SharedExtrinsics> shared_extrinsics) {
    this->shared_extrinsics = shared_extrinsics;
  }

  const Eigen::Matrix3d& GetSharedToLocalTransform() const {
    return shared_to_local_rotation;
  }

  void SetSharedToLocalTransform(const Eigen::Matrix3d& sharedToLocalTransform) {
    this->shared_to_local_rotation = sharedToLocalTransform;
  }

  // Indexing for the location of parameters. Collecting the extrinsics and
  // intrinsics into a single array makes the interface to bundle adjustment
  // with Ceres much simpler.
  enum InternalParametersIndex{
    FOCAL_LENGTH = 0,
    ASPECT_RATIO = 1,
    SKEW = 2,
    PRINCIPAL_POINT_X = 3,
    PRINCIPAL_POINT_Y = 4,
    RADIAL_DISTORTION_1 = 5,
    RADIAL_DISTORTION_2 = 6
  };


  static const int kIntrinsicsSize = 7;

 private:
  // Templated method for disk I/O with cereal. This method tells cereal which
  // data members should be used when reading/writing to/from disk.
  friend class cereal::access;
  template <class Archive>
  void serialize(Archive& ar) {  // NOLINT
    ar(cereal::binary_data(camera_parameters_, sizeof(double) * kIntrinsicsSize),
       shared_extrinsics,
       cereal::binary_data(shared_to_local_rotation.data(), sizeof(double) * 9),
       cereal::binary_data(image_size_, sizeof(int) * 2));
  }

  double camera_parameters_[kIntrinsicsSize];
  std::shared_ptr<SharedExtrinsics> shared_extrinsics;
  Eigen::Matrix3d shared_to_local_rotation;

  // The image size as width then height.
  int image_size_[2];
};

}  // namespace theia

#endif  // THEIA_SFM_CAMERA_CAMERA_H_
