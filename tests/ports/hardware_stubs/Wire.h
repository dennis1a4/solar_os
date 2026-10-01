#pragma once
struct TwoWire {
    void setSDA(unsigned){} void setSCL(unsigned){} void begin(){} void setClock(unsigned){}
    void beginTransmission(unsigned){} size_t write(const uint8_t *,size_t n){return n;}
    uint8_t endTransmission(bool){return 2;}
    size_t requestFrom(unsigned,uint8_t,uint8_t){return 0;}
    int read(){return -1;}
};
extern TwoWire Wire,Wire1,Wire2;
