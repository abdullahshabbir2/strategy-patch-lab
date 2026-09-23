#include "strategy.hpp"
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
extern "C" uint32_t strategy_features();

double number(const std::string& text) {
    std::size_t used{};
    const double result=std::stod(text,&used);
    if (used!=text.size()) throw std::invalid_argument("invalid numeric field");
    return result;
}
uint32_t integer(const std::string& text,uint32_t maximum) {
    if (text.empty()||text[0]=='-'||text[0]=='+') throw std::invalid_argument("invalid unsigned integer");
    std::size_t used{};
    const auto result=std::stoull(text,&used);
    if (used!=text.size()||result>maximum) throw std::invalid_argument("integer outside range");
    return uint32_t(result);
}
int main(int argc,char** argv) {
    if (argc!=2) { std::cerr<<"Usage: strategy.exe SCENARIO.csv\n"; return 2; }
    try {
        const uint32_t marker=strategy_features();
        if ((marker&0xffff0000u)!=0x5a170000u) throw std::runtime_error("feature gate marker corrupt");
        lab::Strategy strategy(marker&0xffffu);
        std::ifstream file(argv[1]);
        if (!file) throw std::runtime_error("scenario not readable");
        std::string line;
        if (!std::getline(file,line)) throw std::runtime_error("empty scenario");
        if (!line.empty()&&line.back()=='\r') line.pop_back();
        if (line!="ms,rpm,pedal,speed,coolant,ethanol,ethanol_age,measured_torque,map,brake,clutch,launch")
            throw std::runtime_error("invalid scenario header");
        std::cout<<"ms,features,map,requested_nm,commanded_nm,limit_nm,fuel_multiplier,ethanol_filtered,dtcs,limp,launch,shift\n";
        unsigned row=1;
        while (std::getline(file,line)) {
            ++row;
            if (!line.empty()&&line.back()=='\r') line.pop_back();
            try {
                std::istringstream stream(line);
                std::vector<std::string> f;
                std::string value;
                while (std::getline(stream,value,',')) f.push_back(value);
                if (!line.empty()&&line.back()==',') f.emplace_back();
                if (f.size()!=12) throw std::runtime_error("expected 12 fields");
                lab::Inputs x;
                x.now_ms=integer(f[0],0xffffffffu); x.rpm=float(number(f[1])); x.pedal_pct=float(number(f[2]));
                x.speed_kph=float(number(f[3])); x.coolant_c=float(number(f[4])); x.ethanol_pct=float(number(f[5]));
                x.ethanol_age_ms=integer(f[6],0xffffffffu); x.measured_torque_nm=float(number(f[7]));
                x.requested_map=integer(f[8],2); x.brake=integer(f[9],1)!=0;
                x.clutch=integer(f[10],1)!=0; x.launch_button=integer(f[11],1)!=0;
                const auto y=strategy.step(x);
                std::cout<<std::fixed<<std::setprecision(5)<<x.now_ms<<','<<(marker&0xffffu)<<','<<y.active_map
                    <<','<<y.requested_nm<<','<<y.commanded_nm<<','<<y.hard_limit_nm<<','<<y.fuel_multiplier
                    <<','<<y.ethanol_filtered<<','<<y.stored_dtcs<<','<<y.limp<<','<<y.launch_active<<','<<y.shift_active<<'\n';
            } catch(const std::exception& e) { throw std::runtime_error("row "+std::to_string(row)+": "+e.what()); }
        }
        if (file.bad()) throw std::runtime_error("scenario read error");
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 2; }
}
