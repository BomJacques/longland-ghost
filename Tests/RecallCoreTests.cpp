#include "../Source/Core/SessionAudioStore.h"
#include "../Source/Recall/GhostVoice.h"
#include "../Source/Recall/GhostVoicePool.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>

namespace ghost = longland::ghost;

namespace
{
int failures = 0;

void check(const bool condition, const char* message)
{
    if (! condition)
    {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

template <std::size_t Size>
void writeMono(ghost::SessionAudioStore& store, const std::array<float, Size>& input)
{
    const float* channels[] { input.data() };
    store.write(channels, 1, static_cast<std::uint32_t> (Size));
}

ghost::RecallCommand commandFor(const ghost::MemoryRef& ref)
{
    ghost::RecallCommand command;
    command.commandId = 1;
    command.queryEventId = 2;
    command.memory = ref;
    command.similarity.total = 0.9f;
    command.playbackRate = 1.0f;
    command.gain = 1.0f;
    command.fadeInSeconds = 0.0f;
    command.fadeOutSeconds = 0.0f;
    return command;
}

void testWrapAndInvalidation()
{
    ghost::SessionAudioStore store;
    store.prepare(8, 1);
    writeMono(store, std::array<float, 6> { 0, 1, 2, 3, 4, 5 });

    const ghost::MemoryRef early { 1, 1, 3, store.epoch() };
    check(store.contains(early), "captured event should initially be valid");
    writeMono(store, std::array<float, 4> { 6, 7, 8, 9 });
    check(! store.contains(early), "overwritten event must be invalidated");

    const ghost::MemoryRef wrapped { 2, 6, 4, store.epoch() };
    check(store.contains(wrapped), "event spanning physical wrap should remain valid");
    check(store.readSample(0, wrapped, 0) == 6.0f, "wrapped event first sample");
    check(store.readSample(0, wrapped, 3) == 9.0f, "wrapped event last sample");
}

void testEpochAndIncompleteReferences()
{
    ghost::SessionAudioStore store;
    store.prepare(16, 1);
    writeMono(store, std::array<float, 4> { 1, 2, 3, 4 });
    const ghost::MemoryRef reference { 3, 0, 4, store.epoch() };
    check(store.contains(reference), "complete reference should be present");

    const ghost::MemoryRef future { 4, 3, 4, store.epoch() };
    check(! store.contains(future), "partially captured future reference must be rejected");
    store.clear();
    check(! store.contains(reference), "clear must invalidate old epoch references");
}

void testLeaseMath()
{
    ghost::SessionAudioStore store;
    store.prepare(16, 1);
    writeMono(store, std::array<float, 12> { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 });
    const ghost::MemoryRef reference { 5, 4, 3, store.epoch() };
    // Eight non-destructive future writes remain; the ninth write overwrites
    // the event's first frame.  Lease counts deliberately exclude that write.
    check(store.framesUntilOverwrite(reference) == 8, "overwrite margin should use absolute frames");
    check(store.hasPlaybackLease(reference, 7, 1), "lease exactly at margin should pass");
    check(! store.hasPlaybackLease(reference, 8, 1), "lease beyond margin should fail");
}

void testHermiteAndEventEdges()
{
    ghost::SessionAudioStore store;
    store.prepare(32, 1);
    writeMono(store, std::array<float, 8> { 100, 100, 0, 1, 2, 3, -100, -100 });
    const ghost::MemoryRef reference { 7, 2, 4, store.epoch() };
    check(std::abs(store.readHermite(0, reference, 1.5) - 1.5f) < 0.001f,
          "Hermite interpolation should reproduce an interior ramp");
    check(store.readHermite(0, reference, 0.0) == 0.0f,
          "interpolation must clamp at event start rather than bleed prior audio");
    check(store.readHermite(0, reference, 3.0) == 3.0f,
          "interpolation must clamp at event end rather than bleed later audio");
}

void testForwardAndReverseVoice()
{
    ghost::SessionAudioStore store;
    store.prepare(256, 1);
    writeMono(store, std::array<float, 8> { 0, 1, 2, 3, 4, 5, 6, 7 });
    const ghost::MemoryRef reference { 11, 0, 8, store.epoch() };

    ghost::GhostVoice forward;
    forward.prepare(48000.0);
    auto command = commandFor(reference);
    check(forward.start(command, store), "forward voice should start");
    std::array<float, 8> forwardOutput {};
    float* forwardChannels[] { forwardOutput.data() };
    forward.renderAdditive(forwardChannels, 1, 8, store);
    check(forwardOutput[2] == 2.0f && forwardOutput[7] == 7.0f,
          "forward voice should reproduce source order");

    ghost::GhostVoice reverse;
    reverse.prepare(48000.0);
    command.reverse = true;
    check(reverse.start(command, store), "reverse voice should start");
    std::array<float, 8> reverseOutput {};
    float* reverseChannels[] { reverseOutput.data() };
    reverse.renderAdditive(reverseChannels, 1, 8, store);
    check(reverseOutput[0] == 7.0f && reverseOutput[7] == 0.0f,
          "reverse voice should reproduce source in reverse order");
}

void testPoolLeaseAndCapacity()
{
    ghost::SessionAudioStore store;
    store.prepare(1024, 1);
    std::array<float, 128> input {};
    input.fill(0.25f);
    writeMono(store, input);
    const ghost::MemoryRef reference { 12, 0, 64, store.epoch() };
    auto command = commandFor(reference);

    ghost::GhostVoicePool pool;
    pool.prepare(48000.0);
    for (std::uint64_t index = 0; index < ghost::GhostVoicePool::voiceCount; ++index)
    {
        command.commandId = index + 1;
        check(pool.schedule(command, store) == ghost::GhostVoicePool::ScheduleResult::started,
              "free pool voice should start immediately");
    }
    command.commandId = 99;
    check(pool.schedule(command, store)
              == ghost::GhostVoicePool::ScheduleResult::queuedAfterVoiceRelease,
          "ninth command should be queued behind a click-free release");

    ghost::SessionAudioStore tightStore;
    tightStore.prepare(128, 1);
    writeMono(tightStore, input);
    const ghost::MemoryRef oldest { 13, 0, 64, tightStore.epoch() };
    auto unsafe = commandFor(oldest);
    check(pool.schedule(unsafe, tightStore)
              == ghost::GhostVoicePool::ScheduleResult::rejectedInsufficientLease,
          "voice that cannot finish before overwrite must be rejected");
}
}

int main()
{
    testWrapAndInvalidation();
    testEpochAndIncompleteReferences();
    testLeaseMath();
    testHermiteAndEventEdges();
    testForwardAndReverseVoice();
    testPoolLeaseAndCapacity();

    if (failures == 0)
        std::cout << "Recall core tests passed\n";
    return failures == 0 ? 0 : 1;
}
