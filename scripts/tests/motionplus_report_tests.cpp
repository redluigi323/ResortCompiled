#include "netplay/motionplus_report.h"
#include <cassert>
#include <stdexcept>
using namespace Riisorted::Netplay;
int main() {
    MotionPlusReport report; report.sequence=42; report.frame=43; report.modes={5,4};
    for(unsigned p=0;p<2;++p) {
        report.states[p].resize(kMotionPlusStateBytes);
        for(size_t i=0;i<report.states[p].size();++i)report.states[p][i]=uint8_t(i+p);
        for(unsigned i=0;i<128;++i) {
            SolverSample sample;sample.valid=(i%2)==0;
            for(unsigned word=0;word<15;++word)sample.words[word]=0x80000000u|(p<<24)|(i<<8)|word;
            report.samples[p].push_back(sample);
        }
    }
    const auto encoded=EncodeMotionPlusReport(report);
    assert(encoded.size()<=2*kMaxMotionPacketBytes+64);
    const auto decoded=DecodeMotionPlusReport(encoded);
    assert(EncodeMotionPlusReport(decoded)==encoded);
    const auto invalid=[&](std::vector<uint8_t> packet) {
        try {DecodeMotionPlusReport(packet);assert(false);}catch(const std::invalid_argument&) {}
    };
    auto corrupt=encoded;corrupt[1]=99;invalid(corrupt);
    corrupt=encoded;corrupt[20]=0;invalid(corrupt); // No sample must never become an invented pose.
    corrupt=encoded;corrupt[21]=2;invalid(corrupt);
    corrupt=encoded;corrupt[22]=2;invalid(corrupt);
    corrupt=encoded;corrupt.pop_back();invalid(corrupt);
    corrupt=encoded;corrupt.push_back(0);invalid(corrupt);
}
