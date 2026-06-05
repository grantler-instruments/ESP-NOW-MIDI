#include "MidiMessageHistory.h"
class Display {
public:
    virtual ~Display() = default;

    virtual bool begin() = 0;
    virtual void update(
        const uint8_t mac[6],
        const char* version,
        int peerCount,
        char usbStatus,
        const MidiMessageHistory* history,
        int historySize,
        int historyHead
    ) = 0;
};
