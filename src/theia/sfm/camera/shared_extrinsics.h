#ifndef THEIA_CAMERA_SHARED_EXTRINSICS_H
#define THEIA_CAMERA_SHARED_EXTRINSICS_H

#include <Eigen/Core>
#include <Eigen/Geometry>
#include <cereal/cereal.hpp>
#include <cereal/types/memory.hpp>
#include <cereal/access.hpp>

namespace theia {

class SharedExtrinsics {
public:
  SharedExtrinsics();

  const double* extrinsics() const { return parameters; }
  double* mutable_extrinsics() { return parameters;  }
  enum ExternalParametersIndex {
    POSITION = 0,
    ORIENTATION = 3
  };
  static const int kExtrinsicsSize = 6;
private:
  double parameters[kExtrinsicsSize];

  friend class cereal::access;
  template <class Archive>
  void serialize(Archive& ar) {  // NOLINT
    ar(parameters, sizeof(double) * kExtrinsicsSize);
  }
};


}

#endif //THEIA_CAMERA_SHARED_EXTRINSICS_H
