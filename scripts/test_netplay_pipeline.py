#!/usr/bin/env python3
"""Exercise actual native/broker lockstep over delayed sockets, without the game.

Checks overlapping packet delivery, mode transitions, unchanged local captures,
identical applied inputs, bounded queues, and delayed divergence rejection.
"""
from pathlib import Path
import os
import queue
import socket
import subprocess
import sys
import tempfile
import threading
import time

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / 'launcher'))
from netplay import OnlineSession, GAME_EXECUTABLE

HARNESS = r'''
#include "netplay/live.h"
#include "netplay/motionplus_report.h"
#include "guest_clock.h"
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>
namespace GuestClock { Pause::Pause(std::chrono::steady_clock::duration) noexcept {} Pause::~Pause() {} }
int main() {
 try {
  using namespace Riisorted::Netplay;
  Live::InitializeFromEnvironment();
  const auto channel = Live::LocalChannel();
  const auto delay = std::strtoul(std::getenv("RESORT_NETPLAY_INPUT_DELAY"), nullptr, 10);
  uint64_t hash = 0;
  for (uint64_t f = 0; f < 120; ++f) {
   auto start = std::chrono::steady_clock::now();
   const std::array<uint8_t,2> modes{uint8_t(f / 20 % 6), uint8_t((f / 20 + 1) % 6)};
   MotionBatch b; b.sequence = f; b.interval = f * 2; b.channel = channel;
   b.motionPlusMode = modes[channel]; b.samples.emplace_back();
   b.samples[0].buttons = uint32_t(1000 * (channel + 1) + f);
   const auto result = Live::Exchange(b, modes, hash);
   if (result.interval != f * 2) throw std::runtime_error("wrong applied frame");
   hash = 0;
   for (uint8_t p = 0; p < 2; ++p) {
    const auto& applied = result.players[p];
    const auto expected = f < delay ? 0 : 1000 * (p + 1) + f - delay;
    if (applied.samples[0].buttons != expected || applied.motionPlusMode != modes[p])
      throw std::runtime_error("wrong delayed input or mode binding");
    hash = hash * 65537 + applied.samples[0].buttons;
   }
   MotionPlusReport report;report.sequence=f;report.frame=f*2;report.modes=modes;
   for(unsigned p=0;p<2;++p) {
    SolverSample s;s.valid=true;s.words[0]=uint32_t(result.players[p].samples[0].buttons);
    report.samples[p].push_back(s);
    report.states[p].assign(kMotionPlusStateBytes,uint8_t(f+p));
   }
   auto expected=EncodeMotionPlusReport(report);
   auto received=channel==0?Live::ShareMotionPlusReport(expected):Live::ShareMotionPlusReport();
   if(received!=expected)throw std::runtime_error("canonical solver bytes differ");
   auto restored=DecodeMotionPlusReport(received);
   if(restored.states!=report.states)throw std::runtime_error("canonical solver state differs");
   std::this_thread::sleep_until(start + std::chrono::microseconds(16667));
  }
  std::cout << "pipeline passed\n";
  return 0;
 } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
'''

class DelayedStream:
    """45 ms one-way delay, allowing multiple packets in flight."""
    def __init__(self, sock, corrupt=False):
        self.sock = sock
        self.pending = queue.Queue(maxsize=64)
        self.corrupt = corrupt
        self.thread = threading.Thread(target=self.drain, daemon=True)
        self.thread.start()
    def sendall(self, data):
        if self.corrupt and data[4:5] == b'I' and int.from_bytes(data[19:27], 'big') == 40:
            data = bytearray(data)
            data[7] ^= 1  # Previous solver hash, not captured input.
            data = bytes(data)
        self.pending.put((time.monotonic() + 0.045, data), timeout=10)
    def drain(self):
        try:
            while True:
                due, data = self.pending.get()
                if data is None: return
                time.sleep(max(0, due - time.monotonic()))
                self.sock.sendall(data)
        except OSError:
            self.sock.close()
    def recv(self, size): return self.sock.recv(size)
    def shutdown(self, how): self.sock.shutdown(how)
    def close(self): self.sock.close()


def run(temp, corrupt):
    sockets = socket.socketpair()
    sessions = []
    failures = []
    completed = threading.Barrier(2)
    for index in range(2):
        folder = temp / f'{corrupt}-{index}'
        folder.mkdir()
        session = OnlineSession(temp, folder, log=lambda line: print(line, flush=True))
        session.run = folder / 'game'
        (session.run / 'UserData').mkdir(parents=True)
        session.role = 'host' if index == 0 else 'join'
        sessions.append(session)
    def play(index):
        try:
            sessions[index].play(DelayedStream(sockets[index], corrupt and index == 1))
        except Exception as error:
            child = sessions[index].child
            log = sessions[index].folder / "native.log"
            if not (child and log.exists() and "pipeline passed" in log.read_text()):
                failures.append(str(error))
        finally:
            if not corrupt:
                try: completed.wait(timeout=5)
                except threading.BrokenBarrierError: pass
            sessions[index].cancel()
            sockets[index].close()
            if sessions[index].child:
                sessions[index].child.wait(timeout=5)
    started = time.monotonic()
    threads = [threading.Thread(target=play, args=(index,), daemon=True) for index in range(2)]
    for t in threads: t.start()
    for t in threads: t.join(timeout=15)
    if any(t.is_alive() for t in threads):
        print('failures', failures)
        for session in sessions:
            print((session.folder / 'native.log').read_text() if (session.folder / 'native.log').exists() else 'no native log')
            session.cancel()
        raise AssertionError('pipeline deadlocked')
    elapsed = time.monotonic() - started
    if corrupt:
        assert any('Synchronization disagreement' in e for e in failures), failures
    else:
        if failures:
            for session in sessions: print((session.folder / "native.log").read_text())
        assert not failures, failures
        for session in sessions:
            assert 'pipeline passed' in (session.folder / 'native.log').read_text()
        assert elapsed < 5, f'120 frames took {elapsed:.2f}s: still latency bound'
        print(f'120 native intervals with 90 ms RTT: {elapsed:.2f}s including latency probes')

with tempfile.TemporaryDirectory(prefix='resort-netplay-pipeline-') as directory:
    temp = Path(directory)
    source = temp / 'harness.cpp'
    source.write_text(HARNESS)
    subprocess.run(['clang++', '-std=c++17', '-O1', '-pthread', '-I', str(ROOT / 'runtime/wsr/include'),
                    str(source), str(ROOT / 'runtime/wsr/src/netplay/live.cpp'),
                    str(ROOT / 'runtime/wsr/src/netplay/session.cpp'), '-o', str(temp / GAME_EXECUTABLE)], check=True)
    run(temp, False)
    run(temp, True)
    print('Delayed input matching, mode transitions and divergence rejection passed.')
