#pragma once
#include <array>
#include <cstdint>

namespace lab {
enum Feature : uint32_t { map_switch=1, flex_fuel=2, launch=4, flat_shift=8 };
enum Fault : uint32_t { fuel_sensor=1, torque_tracking=2, over_temperature=4, bad_input=8, overspeed=16 };
struct Calibration {
    std::array<float,3> rpm_axis;
    std::array<float,3> pedal_axis;
    std::array<std::array<float,9>,3> torque_maps;
};
Calibration default_calibration();
bool valid_calibration(const Calibration& c) noexcept;
float interpolate(const Calibration& c,unsigned map,float rpm,float pedal) noexcept;
struct Inputs {
    uint32_t now_ms{}, ethanol_age_ms{};
    float rpm{}, pedal_pct{}, speed_kph{}, coolant_c{}, ethanol_pct{}, measured_torque_nm{};
    unsigned requested_map{};
    bool brake{}, clutch{}, launch_button{};
};
struct Outputs {
    float requested_nm{}, commanded_nm{}, hard_limit_nm{}, fuel_multiplier{1}, ethanol_filtered{};
    unsigned active_map{};
    uint32_t stored_dtcs{};
    bool limp{}, launch_active{}, shift_active{};
};
class Strategy {
public:
    explicit Strategy(uint32_t features,Calibration calibration=default_calibration());
    Outputs step(const Inputs& input);
private:
    uint32_t features_, last_ms_{}, pending_since_{}, map_changed_{}, launch_since_{}, shift_since_{};
    uint32_t torque_fault_ms_{}, dtcs_{};
    Calibration calibration_;
    unsigned map_{}, pending_map_{99};
    float ethanol_{}, previous_torque_{};
    bool started_{}, ethanol_initialized_{}, torque_latched_{}, previous_clutch_{};
    bool launch_running_{}, launch_lockout_{}, shift_running_{};
};
}
