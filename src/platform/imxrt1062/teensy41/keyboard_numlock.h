#pragma once
// USBHIDParser reuses its setup packet. Never queue our SET_REPORT while the
// KeyboardController's connection-time SET_IDLE still owns that packet.
// Caller serializes this policy with USB interrupts.
class KeyboardNumLockStartup {
public:
    bool connected() const { return attached; }
    void connect(bool enabled) { attached=true;ready=false;pending=enabled; }
    void initialized() { if(attached)ready=true; }
    void disconnect() { attached=ready=pending=false; }
    void request(bool enabled) { pending=attached && enabled; }
    bool take_request() {
        if(!attached || !ready || !pending)return false;
        pending=false;return true;
    }
private:
    bool attached=false,ready=false,pending=false;
};
