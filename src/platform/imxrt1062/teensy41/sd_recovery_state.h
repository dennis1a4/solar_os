#pragma once
#include <stdint.h>

// No hardware dependencies: shared by the SD adapter and host lifecycle tests.
class SdRecoveryState {
public:
    bool mounted() const { return mounted_; }
    bool ejected() const { return ejected_; }
    bool io_allowed() const { return mounted_ || mounting_; }
    unsigned handles() const { return handles_; }
    uint32_t generation() const { return generation_; }
    bool begin_mount() {
        if (mounted_ || mounting_ || ejected_ || handles_) return false;
        mounting_ = true;
        return true;
    }
    void finish_mount(bool success) {
        mounted_ = mounting_ && success;
        mounting_ = false;
        if (mounted_) ++generation_;
    }
    void lost() {
        if (mounted_ || mounting_) ++generation_;
        mounted_ = mounting_ = false;
    }
    void removed() { lost(); ejected_ = false; }
    bool acquire() {
        if (!mounted_) return false;
        ++handles_;
        return true;
    }
    bool release() {
        if (!handles_) return false;
        --handles_;
        return true;
    }
    bool eject() {
        if (handles_ || mounting_) return false;
        lost();
        ejected_ = true;
        return true;
    }
    void allow_manual_mount() { ejected_ = false; }
private:
    bool mounted_ = false, mounting_ = false, ejected_ = false;
    unsigned handles_ = 0;
    uint32_t generation_ = 0;
};
