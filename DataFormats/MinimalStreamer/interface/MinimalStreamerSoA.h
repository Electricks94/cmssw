#pragma once

#include "DataFormats/SoATemplate/interface/SoALayout.h"

GENERATE_SOA_LAYOUT(MinimalStreamerSoATemplate,
                    SOA_COLUMN(double, x),
                    SOA_COLUMN(double, y),
                    SOA_COLUMN(double, z),
                    SOA_SCALAR(double, s1),
                    SOA_SCALAR(double, s2),
                    SOA_SCALAR(double, s3));

using SoA = MinimalStreamerSoATemplate<>;
