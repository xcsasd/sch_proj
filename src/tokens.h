#pragma once

#include <random>
#include <string>
#include <sstream>
#include <iomanip>

class TokenGenerator {
public:
    inline std::string GenerateToken() {
        auto num1 = generator1_();
        auto num2 = generator2_();

        std::stringstream ss;
        ss << std::hex << std::setfill('0') 
           << std::setw(16) << num1 
           << std::setw(16) << num2;
           
        return ss.str();
    }

private:
    std::random_device random_device_;
    
    std::mt19937_64 generator1_{random_device_()};
    std::mt19937_64 generator2_{random_device_()};
};
