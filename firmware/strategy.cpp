#include "strategy.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace lab {
extern const Calibration calibration_blob;
Calibration default_calibration() {
    return calibration_blob;
}
bool valid_calibration(const Calibration& c) noexcept {
    for (unsigned i=0;i<3;++i) {
        if (!std::isfinite(c.rpm_axis[i]) || !std::isfinite(c.pedal_axis[i])) return false;
        if (i && (c.rpm_axis[i]<=c.rpm_axis[i-1] || c.pedal_axis[i]<=c.pedal_axis[i-1])) return false;
    }
    if (c.rpm_axis[0]<0 || c.rpm_axis[2]>10000 || c.pedal_axis[0]!=0 || c.pedal_axis[2]!=100) return false;
    for (const auto& map:c.torque_maps)
        for (const auto value:map) if (!std::isfinite(value) || value<0 || value>350) return false;
    return true;
}
float interpolate(const Calibration& c,unsigned map,float rpm,float pedal) noexcept {
    rpm=std::clamp(rpm,c.rpm_axis.front(),c.rpm_axis.back());
    pedal=std::clamp(pedal,c.pedal_axis.front(),c.pedal_axis.back());
    const unsigned r=rpm>c.rpm_axis[1]?1:0,p=pedal>c.pedal_axis[1]?1:0;
    const float rf=(rpm-c.rpm_axis[r])/(c.rpm_axis[r+1]-c.rpm_axis[r]);
    const float pf=(pedal-c.pedal_axis[p])/(c.pedal_axis[p+1]-c.pedal_axis[p]);
    const auto& t=c.torque_maps[std::min(map,2u)];
    const float low=t[r*3+p]+pf*(t[r*3+p+1]-t[r*3+p]);
    const float high=t[(r+1)*3+p]+pf*(t[(r+1)*3+p+1]-t[(r+1)*3+p]);
    return low+rf*(high-low);
}
Strategy::Strategy(uint32_t features,Calibration calibration):features_(features),calibration_(calibration) {
    if (features & ~15u) throw std::invalid_argument("unknown strategy feature bits");
    if (!valid_calibration(calibration)) throw std::invalid_argument("invalid calibration");
}
Outputs Strategy::step(const Inputs& x) {
    const uint32_t dt=started_?uint32_t(x.now_ms-last_ms_):0;
    if (started_ && dt>=0x80000000u) throw std::invalid_argument("nonmonotonic timestamp");
    const bool gap=started_ && dt>500;
    last_ms_=x.now_ms; started_=true;
    const bool valid=std::isfinite(x.rpm)&&std::isfinite(x.pedal_pct)&&std::isfinite(x.speed_kph)
        &&std::isfinite(x.coolant_c)&&std::isfinite(x.measured_torque_nm)
        &&x.rpm>=0&&x.rpm<=10000&&x.pedal_pct>=0&&x.pedal_pct<=100
        &&x.speed_kph>=0&&x.speed_kph<=350&&x.coolant_c>=-40&&x.coolant_c<=180
        &&x.measured_torque_nm>=-100&&x.measured_torque_nm<=1000&&x.requested_map<3;
    if (!valid || gap) {
        dtcs_|=bad_input;
        previous_torque_=0;
        pending_map_=99; launch_running_=shift_running_=false;
        launch_lockout_=true;
        previous_clutch_=x.clutch;
        return {0,0,0,1,ethanol_,map_,dtcs_,true,false,false};
    }
    if ((features_&map_switch) && x.requested_map!=map_ && x.brake && x.pedal_pct<5 && x.speed_kph<1 && x.rpm<1500) {
        if (pending_map_!=x.requested_map) { pending_map_=x.requested_map; pending_since_=x.now_ms; }
        if (uint32_t(x.now_ms-pending_since_)>=200 && uint32_t(x.now_ms-map_changed_)>=500) {
            map_=pending_map_; pending_map_=99; map_changed_=x.now_ms;
        }
    } else pending_map_=99;

    bool sensor_bad=false;
    float multiplier=1;
    if (features_&flex_fuel) {
        sensor_bad=!std::isfinite(x.ethanol_pct)||x.ethanol_pct<0||x.ethanol_pct>85||x.ethanol_age_ms>250;
        if (sensor_bad) dtcs_|=fuel_sensor;
        else {
            if (!ethanol_initialized_) { ethanol_=x.ethanol_pct; ethanol_initialized_=true; }
            const float alpha=float(dt)/(500.0f+float(dt));
            ethanol_+=alpha*(x.ethanol_pct-ethanol_);
        }
        // Retain the last valid estimate if stale, while reducing torque.
        // Density-based mass blending model, not an injector or combustion model.
        const float volume=ethanol_/100.0f;
        const float mass=volume*0.789f/(volume*0.789f+(1-volume)*0.745f);
        const float stoich=14.7f*(1-mass)+9.0f*mass;
        multiplier=std::clamp(14.7f/stoich,1.0f,1.6f);
    }
    float hard_limit=350;
    if (x.coolant_c>105) hard_limit=std::max(0.0f,350.0f*(130-x.coolant_c)/25.0f);
    if (x.coolant_c>=120) dtcs_|=over_temperature;
    if (x.rpm>=6500) { hard_limit=0; dtcs_|=overspeed; }
    const bool limp=sensor_bad||torque_latched_;
    if (limp) hard_limit=std::min(hard_limit,60.0f);

    if (!x.launch_button) { launch_lockout_=false; launch_running_=false; }
    const bool launch_allowed=(features_&launch)&&!limp&&x.coolant_c<110&&x.rpm<6500&&x.launch_button&&x.brake&&x.speed_kph<3&&x.pedal_pct>20;
    if (launch_allowed&&!launch_lockout_&&!launch_running_) { launch_since_=x.now_ms; launch_running_=true; }
    if (launch_running_&&(!launch_allowed||uint32_t(x.now_ms-launch_since_)>=2000)) {
        launch_running_=false; launch_lockout_=true;
    }
    if (launch_running_) hard_limit=std::min(hard_limit,x.rpm>3000?80.0f:150.0f);

    if ((features_&flat_shift)&&!limp&&x.coolant_c<110&&x.rpm<6500&&x.clutch&&!previous_clutch_&&x.pedal_pct>80&&x.speed_kph>10&&x.rpm>2500) {
        shift_running_=true; shift_since_=x.now_ms;
    }
    if (!x.clutch||limp||uint32_t(x.now_ms-shift_since_)>=150) shift_running_=false;
    previous_clutch_=x.clutch;
    if (shift_running_) hard_limit=std::min(hard_limit,60.0f);

    const float requested=interpolate(calibration_,map_,x.rpm,x.pedal_pct);
    float command=std::min({requested,hard_limit,previous_torque_+0.1f*float(dt)});
    // Persistently excessive measured torque latches a diagnostic and limp limit.
    if (x.measured_torque_nm>command+50) torque_fault_ms_=std::min(1000u,torque_fault_ms_+dt);
    else torque_fault_ms_=0;
    if (torque_fault_ms_>=100) {
        torque_latched_=true; dtcs_|=torque_tracking;
        hard_limit=std::min(hard_limit,60.0f); command=std::min(command,hard_limit);
        launch_running_=shift_running_=false;
    }
    previous_torque_=command;
    return {requested,command,hard_limit,multiplier,ethanol_,map_,dtcs_,limp||torque_latched_,launch_running_,shift_running_};
}
}
