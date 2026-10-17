#include "../src/station_toggle.h"
#include <cassert>
int main() {
    using namespace stationToggle;
    assert(contains(x,y));
    assert(contains(x+width-1,y+height-1));
    assert(!contains(x-1,y));
    assert(!contains(x,y-1));
    assert(!contains(x+width,y));
    assert(!contains(x,y+height));
    assert(!contains(316,328)); // Face touch never toggles narration.
}
