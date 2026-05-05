#pragma once
#include <cstdint>

class MPEChannelManager {
private:
    bool lowerZoneEnabled = false;
    bool upperZoneEnabled = false;
    uint8_t lowerZoneChannels = 0; // Number of member channels
    uint8_t upperZoneChannels = 0;
    bool channelInUse[16] = {false};
    
public:
    void configureLowerZone(uint8_t numChannels) {
        lowerZoneEnabled = (numChannels > 0);
        lowerZoneChannels = numChannels;
    }
    
    void configureUpperZone(uint8_t numChannels) {
        upperZoneEnabled = (numChannels > 0);
        upperZoneChannels = numChannels;
    }
    
    // Allocate a channel for a new note
    int allocateChannel(bool preferLowerZone = true) {
        if (preferLowerZone && lowerZoneEnabled) {
            for (uint8_t i = 2; i <= (1 + lowerZoneChannels) && i <= 9; i++) {
                if (!channelInUse[i-1]) {
                    channelInUse[i-1] = true;
                    return i;
                }
            }
        }
        
        if (upperZoneEnabled) {
            for (uint8_t i = 10; i <= (16 - upperZoneChannels) && i <= 16; i++) {
                if (!channelInUse[i-1]) {
                    channelInUse[i-1] = true;
                    return i;
                }
            }
        }
        
        return -1; // No channels available
    }
    
    void releaseChannel(uint8_t channel) {
        if (channel >= 1 && channel <= 16) {
            channelInUse[channel-1] = false;
        }
    }
};