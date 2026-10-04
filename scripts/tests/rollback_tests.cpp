#include "netplay/rollback.h"
#include "netplay/checkpoint.h"
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <cmath>
#include <random>
#include <algorithm>

using namespace Riisorted::Netplay;
using namespace Riisorted::Netplay::Rollback;
void require(bool value, const char* reason) { if (!value) throw std::runtime_error(reason); }
template<class F> void rejected(F fn) {
    bool caught = false; try { fn(); } catch (const std::exception&) { caught = true; }
    require(caught, "Expected rejection");
}
Bytes word(uint64_t value) {
    Bytes result; for (int shift=56;shift>=0;shift-=8) result.push_back(uint8_t(value>>shift)); return result;
}
uint64_t number(const Bytes& value) {
    uint64_t result=0; for(auto byte:value)result=(result<<8)|byte; return result;
}
struct Game {
    uint64_t score=0, random=0x123456789, replayed=0, forwards=0;
    SimulationClock clock;
    std::map<uint64_t,uint64_t> effects;
    std::vector<uint64_t> published;
    Adapter adapter() {
        return {
            [this] {
                Bytes state;
                for(auto value:{score,random,clock.Save().ticks,clock.Save().remainder,uint64_t(clock.Save().rate)}) {
                    auto bytes=word(value);state.insert(state.end(),bytes.begin(),bytes.end());
                }
                return state;
            },
            [this](const Bytes& state) {
                require(state.size()==40,"State size");
                auto at=[&](size_t i){return number(Bytes(state.begin()+i*8,state.begin()+(i+1)*8));};
                score=at(0);random=at(1);clock.Restore({at(2),at(3),uint32_t(at(4))});
            },
            [this](uint64_t frame,const Inputs& inputs,Pass pass) {
                if(pass==Pass::Replay)++replayed;else ++forwards;
                clock.AdvanceInterval(60);
                random=random*6364136223846793005ull+1;
                score=score*65537+number(inputs[0])*13+number(inputs[1])*19+(random>>32)+(clock.Ticks()&65535);
                effects[frame]=score; // A replay replaces this frame's sound/save.
            },
            [](const Bytes& previous){return previous.empty()?word(0):previous;},
            [this](uint64_t frame) {
                require(frame==published.size(),"Effects commit in order exactly once");
                published.push_back(effects.at(frame));effects.erase(frame);
            },
            [this](uint64_t frame){effects.erase(effects.lower_bound(frame),effects.end());}
        };
    }
};
void lateInput() {
    Game baseline, delayed;
    Session expected(baseline.adapter()), actual(delayed.adapter());
    for(uint64_t f=0;f<300;++f) {
        const auto p0=word(f*11),p1=word((f*7)%23);
        expected.Submit(0,f,p0);expected.Submit(1,f,p1);require(expected.Advance(),"Baseline advance");
        actual.Submit(0,f,p0);
        if(f>=5) actual.Submit(1,f-5,word(((f-5)*7)%23));
        require(actual.Advance(),"Bounded delayed advance");
        require(actual.SavedBytes()<=8*40,"Snapshot storage bounded");
    }
    for(uint64_t f=295;f<300;++f)actual.Submit(1,f,word((f*7)%23));
    actual.Reconcile();
    require(delayed.score==baseline.score && delayed.random==baseline.random,"Replayed game state equals known-input baseline");
    require(delayed.published==baseline.published,"Confirmed effects match with no duplicates");
    require(actual.ConfirmedThrough()==300 && actual.SavedBytes()==0,"All frames confirmed; snapshots released");
    require(actual.Rollbacks()>0 && delayed.replayed>0,"Real rewinds occurred");
    actual.Submit(1,299,word((299*7)%23)); // idempotent duplicate
    rejected([&]{actual.Submit(1,299,word(123));});
    rejected([&]{actual.Submit(1,0,word(0));});
    rejected([&]{actual.Submit(1,309,word(0));});
}
void windowAndOrder() {
    Game game; Session session(game.adapter(),{3,1024,64});
    for(uint64_t f=0;f<3;++f) {session.Submit(0,f,word(f));require(session.Advance(),"Predicted advance");}
    const auto state=game.score;
    require(!session.Advance() && game.score==state,"Window exhaustion pauses without mutating simulation");
    session.Submit(1,2,word(4));session.Submit(1,0,word(2));session.Submit(1,1,word(3));
    session.Reconcile();
    require(session.ConfirmedThrough()==3,"Out-of-order arrivals become contiguous confirmation");
    require(session.Advance(),"Window resumes after real input");
    Game limited; Session small(limited.adapter(),{8,39,64});
    rejected([&]{small.Advance();});require(limited.forwards==0,"Budget failure precedes simulation advance");
    rejected([&]{small.Advance();}); // fail closed after adapter/capture failure
    Adapter missing;rejected([&]{Session invalid(missing);});
}
void reorderedPeers() {
    Game a,b,reference; Session sa(a.adapter()),sb(b.adapter()),sr(reference.adapter());
    std::mt19937 random(19);
    struct Arrival {uint64_t due,frame;uint8_t player;Bytes input;};
    std::vector<Arrival> pendingA,pendingB;
    for(uint64_t f=0;f<200;++f) {
        const auto x=word(random()%50),y=word(random()%50);
        sr.Submit(0,f,x);sr.Submit(1,f,y);require(sr.Advance(),"Reference step");
        sa.Submit(0,f,x);sb.Submit(1,f,y);
        pendingA.push_back({f+random()%5,f,1,y});pendingB.push_back({f+random()%5,f,0,x});
        auto deliver=[&](Session& s,std::vector<Arrival>& queue) {
            std::shuffle(queue.begin(),queue.end(),random);
            for(auto it=queue.begin();it!=queue.end();) {
                if(it->due<=f){s.Submit(it->player,it->frame,it->input);it=queue.erase(it);}else ++it;
            }
        };
        deliver(sa,pendingA);deliver(sb,pendingB);
        require(sa.Advance()&&sb.Advance(),"Jitter fits window");
    }
    for(auto& item:pendingA)sa.Submit(item.player,item.frame,item.input);
    for(auto& item:pendingB)sb.Submit(item.player,item.frame,item.input);
    sa.Reconcile();sb.Reconcile();
    require(a.score==reference.score&&b.score==reference.score,"Both corrected peers match full real-input game");
    require(a.published==reference.published&&b.published==reference.published,"Both effect streams match");
}
void memoryPages() {
    std::vector<uint8_t> memory(PageStore::kPageBytes*2+13,7);
    std::vector<MemorySpan> spans{{0x80000000,memory.data(),memory.size()}};
    PageStore store(8*PageStore::kPageBytes);
    auto first=store.Capture(spans);const auto footprint=store.ResidentBytes();
    auto same=store.Capture(spans,&first);require(store.ResidentBytes()==footprint,"Unchanged pages shared");
    memory[3]=99;memory.back()=88;
    auto second=store.Capture(spans,&first);require(second.Digest()!=first.Digest(),"Canonical RAM change detected");
    require(store.ResidentBytes()<2*footprint,"Only changed pages duplicated");
    store.Restore(first,spans);require(memory[3]==7&&memory.back()==7,"Old snapshot immutable and restored");
    store.Restore(second,spans);require(memory[3]==99&&memory.back()==88,"New snapshot retained");
    auto wrong=spans;wrong[0].address++;
    rejected([&]{store.Restore(first,wrong);});require(memory[3]==99,"Invalid layout does not modify RAM");
    PageStore tiny(PageStore::kPageBytes+64);
    rejected([&]{tiny.Capture(spans);});require(tiny.ResidentBytes()==0,"Failed capture releases allocated pages");
    PageStore::Image empty;rejected([&]{store.Capture(spans,&empty);});
    first={};same={};second={};require(store.ResidentBytes()==0,"Last reference releases page memory");
}
void motionAndClock() {
    MotionBatch real;real.channel=1;real.motionPlusMode=5;real.recenter=true;
    real.samples.resize(16);auto& last=real.samples.back();
    last.buttons=17;last.gyro={4,5,6};last.angle={1,2,3};last.acceleration={0,-3,0};last.pointerValid=true;last.pointerX=.3;
    auto predicted=PredictMotion(real,44,45);
    require(predicted.samples.size()==1&&!predicted.recenter,"No repeated edge or sample burst");
    auto& sample=predicted.samples[0];
    require(sample.buttons==17&&sample.pointerX==.3&&sample.pointerValid,"Held buttons/pose/cursor");
    require(sample.gyro==std::array<double,3>{}&&sample.angle==std::array<double,3>{},"No repeated rotation impulse");
    require(sample.acceleration==std::array<double,3>{0,-1,0},"Acceleration force removed");
    SimulationClock clock;clock.AdvanceInterval(59);const auto saved=clock.Save();
    for(unsigned i=0;i<50;++i)clock.AdvanceInterval(59);
    const auto end=clock.Ticks();
    clock.Restore(saved);for(unsigned i=0;i<50;++i)clock.AdvanceInterval(59);
    require(clock.Ticks()==end,"Rational tick remainder restored exactly");
    rejected([&]{clock.Restore({0,60,59});});require(clock.Ticks()==end,"Invalid clock state does not mutate time");
}
int main() {
    try {lateInput();windowAndOrder();reorderedPeers();memoryPages();motionAndClock();
        std::cout<<"Rollback: delayed/reordered input, replayed state/effects, limits, motion prediction, page restore and clock rewind passed.\n";
    } catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;} return 0;
}
