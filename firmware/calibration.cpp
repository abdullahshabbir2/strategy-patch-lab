#include "strategy.hpp"
namespace lab {
static_assert(sizeof(Calibration)==132,"binary calibration layout changed");
// Separate translation unit and no LTO: the strategy loads this actual binary table.
extern const Calibration calibration_blob __attribute__((used,section(".cal"))) = {
    {1000,3000,6000},{0,50,100},{{
        {{0,80,160, 0,100,200, 0,90,180}},
        {{0,110,220, 0,140,280, 0,125,250}},
        {{0,130,260, 0,175,350, 0,150,300}}
    }}
};
}
