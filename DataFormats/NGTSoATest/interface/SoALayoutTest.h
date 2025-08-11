#ifndef NGTSoATest_interface_SoALayoutTest_h
#define NGTSoATest_interface_SoALayoutTest_h

#include <Eigen/Core>
#include <Eigen/Dense>

#include "DataFormats/SoATemplate/interface/SoACommon.h"
#include "DataFormats/SoATemplate/interface/SoALayout.h"
#include <iostream>

GENERATE_SOA_LAYOUT(SoATemplate, 
                    SOA_COLUMN(float, x0),
                    SOA_EIGEN_COLUMN(Eigen::Vector3d, x1),
                    SOA_SCALAR(int, x2))

using SoA = SoATemplate<>;
using ConstSoAView = SoA::ConstView;
using SoAView = SoA::View;

#endif // NGTSoATest_interface_SoALayoutTest_h
