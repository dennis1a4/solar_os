#pragma once
#define SPI_MODE0 0
#define SPI_MODE1 1
#define SPI_MODE2 2
#define SPI_MODE3 3
#define MSBFIRST 0
struct SPISettings {SPISettings(unsigned,unsigned,unsigned){}};
struct SPIClass {
    unsigned transfers=0;
    void setMOSI(unsigned){} void setMISO(unsigned){} void setSCK(unsigned){} void begin(){}
    void beginTransaction(SPISettings){} void endTransaction(){}
    uint8_t transfer(uint8_t value){++transfers;return value;}
};
extern SPIClass SPI,SPI1;
