#include "DataFormats/SoATemplate/interface/SoACommon.h"
#include "DataFormats/SoATemplate/interface/SoALayout.h"

GENERATE_SOA_LAYOUT(RSoALayout,
  SOA_COLUMN(float, x),
  SOA_COLUMN(float, y))