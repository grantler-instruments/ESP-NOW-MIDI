#pragma once

#include <Arduino.h>

namespace enomik {

class MPEChannelManager {
private:
    bool lowerZoneEnabled = false;
    bool upperZoneEnabled = false;
    byte lowerZoneChannels = 0; // Number of member channels
    byte upperZoneChannels = 0;
    bool channelInUse[16] = {false};
    
public:
    void configureLowerZone(byte numChannels) {
        lowerZoneEnabled = (numChannels > 0);
        lowerZoneChannels = numChannels;
    }
    
    void configureUpperZone(byte numChannels) {
        upperZoneEnabled = (numChannels > 0);
        upperZoneChannels = numChannels;
    }
    
    // Allocate a channel for a new note
    int allocateChannel(bool preferLowerZone = true) {
        if (preferLowerZone && lowerZoneEnabled) {
            for (byte i = 2; i <= (1 + lowerZoneChannels) && i <= 9; i++) {
                if (!channelInUse[i-1]) {
                    channelInUse[i-1] = true;
                    return i;
                }
            }
        }
        
        if (upperZoneEnabled) {
            for (byte i = 10; i <= (16 - upperZoneChannels) && i <= 16; i++) {
                if (!channelInUse[i-1]) {
                    channelInUse[i-1] = true;
                    return i;
                }
            }
        }
        
        return -1; // No channels available
    }
    
    void releaseChannel(byte channel) {
        if (channel >= 1 && channel <= 16) {
            channelInUse[channel-1] = false;
        }
    }
};

} // namespace enomik