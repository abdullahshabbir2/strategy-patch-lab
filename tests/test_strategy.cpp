#include "strategy.hpp"
#include <cmath>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
unsigned checks{};
void check(bool condition,const char* name) { if(!condition) throw std::runtime_error(name); ++checks; }
lab::Inputs input(uint32_t now=0) {
    lab::Inputs x; x.now_ms=now; x.rpm=1000; x.coolant_c=90; x.ethanol_pct=10; return x;
}
void select_map(lab::Strategy& s,lab::Inputs& x,unsigned map) {
    x.requested_map=map; x.brake=true;
    for (unsigned t=0;t<=600;t+=20) { x.now_ms=t; s.step(x); }
}
int main() {
    try {
        auto calibration=lab::default_calibration();
        check(lab::valid_calibration(calibration),"valid calibration");
        check(lab::interpolate(calibration,0,3000,100)==200,"grid point");
        check(lab::interpolate(calibration,0,2000,25)==45,"bilinear midpoint");
        check(lab::interpolate(calibration,0,0,-10)==0,"axis clamping");
        auto invalid=calibration; invalid.rpm_axis[1]=invalid.rpm_axis[0];
        check(!lab::valid_calibration(invalid),"axis ordering");
        invalid=calibration; invalid.torque_maps[0][1]=351;
        check(!lab::valid_calibration(invalid),"calibration torque bound");
        bool threw=false;
        try { lab::Strategy bad(16); } catch(const std::invalid_argument&) { threw=true; }
        check(threw,"unknown feature bits rejected");

        lab::Strategy map(lab::map_switch); auto x=input(); select_map(map,x,2);
        x.now_ms=620; check(map.step(x).active_map==2,"debounced safe map switch");
        x.requested_map=1; x.speed_kph=30; x.pedal_pct=50;
        for (unsigned t=640;t<1200;t+=20) { x.now_ms=t; check(map.step(x).active_map==2,"moving map request blocked"); }
        lab::Strategy baseline(0); x=input(); select_map(baseline,x,2); x.now_ms=620;
        check(baseline.step(x).active_map==0,"baseline ignores feature request");

        lab::Strategy flex(lab::flex_fuel); x=input(); x.ethanol_pct=85;
        auto y=flex.step(x); check(y.fuel_multiplier>1.45f&&y.fuel_multiplier<1.6f,"ethanol mass compensation");
        x.now_ms=20; x.ethanol_age_ms=251; x.pedal_pct=100;
        y=flex.step(x); check(y.limp&&y.hard_limit_nm<=60&&(y.stored_dtcs&lab::fuel_sensor),"stale fuel input fallback");
        x.now_ms=40; x.ethanol_age_ms=0; x.ethanol_pct=0;
        y=flex.step(x); check(!y.limp&&(y.stored_dtcs&lab::fuel_sensor),"recover active fault but retain DTC");
        check(y.ethanol_filtered>0&&y.ethanol_filtered<85,"fuel estimate filtering");
        x.now_ms=60; x.ethanol_pct=std::numeric_limits<float>::quiet_NaN();
        check(flex.step(x).limp,"NaN ethanol fallback");

        lab::Strategy protections(15); x=input(); x.pedal_pct=100; protections.step(x);
        x.now_ms=20; x.coolant_c=130;
        y=protections.step(x); check(y.commanded_nm==0&&(y.stored_dtcs&lab::over_temperature),"thermal zero-torque override");
        x.now_ms=40; x.coolant_c=90; x.rpm=6500;
        y=protections.step(x); check(y.commanded_nm==0&&(y.stored_dtcs&lab::overspeed),"overspeed override");
        x.now_ms=60; x.rpm=std::numeric_limits<float>::infinity();
        y=protections.step(x); check(y.commanded_nm==0&&y.limp,"nonfinite input fails closed");
        x=input(1000); check(protections.step(x).commanded_nm==0,"scheduler gap fails closed");
        threw=false; x.now_ms=900;
        try { protections.step(x); } catch(const std::invalid_argument&) { threw=true; }
        check(threw,"backward time rejected");

        lab::Strategy monitor(15); x=input(); x.pedal_pct=100; x.measured_torque_nm=300;
        for(unsigned t=0;t<=100;t+=20) { x.now_ms=t; y=monitor.step(x); }
        check(y.limp&&(y.stored_dtcs&lab::torque_tracking)&&y.commanded_nm<=60,"torque monitoring latch");
        x.now_ms=120; x.measured_torque_nm=0;
        check(monitor.step(x).limp,"torque latch retained until new instance/key cycle");

        lab::Strategy launch(lab::launch); x=input(); x.brake=true; x.pedal_pct=100; x.launch_button=true; x.rpm=3500;
        y=launch.step(x); check(y.launch_active&&y.hard_limit_nm<=80,"launch torque request");
        for(unsigned t=20;t<=2000;t+=20) { x.now_ms=t; y=launch.step(x); }
        check(!y.launch_active,"launch duration limit");
        x.now_ms=2020; check(!launch.step(x).launch_active,"launch held-button lockout");
        x.now_ms=2040; x.launch_button=false; launch.step(x);
        x.now_ms=2060; x.launch_button=true; check(launch.step(x).launch_active,"launch release rearms");

        lab::Strategy shift(lab::flat_shift); x=input(); x.speed_kph=40; x.rpm=3500; x.pedal_pct=100;
        shift.step(x); x.now_ms=20; x.clutch=true;
        y=shift.step(x); check(y.shift_active&&y.hard_limit_nm<=60,"flat shift rising edge");
        x.now_ms=170; check(!shift.step(x).shift_active,"flat shift duration limit");
        x.now_ms=190; check(!shift.step(x).shift_active,"held clutch does not retrigger");

        lab::Strategy rollover(0); x=input(0xfffffff0u); rollover.step(x); x.now_ms=4;
        check(!rollover.step(x).limp,"timestamp rollover");

        std::mt19937 rng(20260923);
        for(unsigned mask=0;mask<16;++mask) {
            lab::Strategy random(mask); float previous=0;
            for(unsigned i=0;i<1000;++i) {
                x=input(i*20); x.rpm=float(rng()%8000); x.pedal_pct=float(rng()%101);
                x.coolant_c=float(70+rng()%71); x.speed_kph=float(rng()%101); x.ethanol_pct=float(rng()%86);
                x.ethanol_age_ms=rng()%400; x.requested_map=rng()%3; x.brake=(rng()%2)!=0;
                x.clutch=(rng()%2)!=0; x.launch_button=(rng()%2)!=0;
                y=random.step(x);
                check(std::isfinite(y.commanded_nm)&&y.commanded_nm>=0&&y.commanded_nm<=350,"bounded finite torque");
                check(y.commanded_nm<=y.hard_limit_nm&&y.commanded_nm<=y.requested_nm,"protection arbitration wins");
                check(y.commanded_nm<=previous+2.001f,"positive torque slew bound");
                check(y.fuel_multiplier>=1&&y.fuel_multiplier<=1.6f,"bounded fuel compensation");
                if(x.coolant_c>=130||x.rpm>=6500) check(y.commanded_nm==0,"hard protection invariant");
                previous=y.commanded_nm;
            }
        }
        std::cout<<checks<<" strategy assertions passed; model-only validation.\n";
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
