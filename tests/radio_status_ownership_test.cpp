#include "models/RadioStatusOwnership.h"

#include <cstdio>

using namespace AetherSDR;
using namespace AetherSDR::RadioStatusOwnership;

namespace {

int failures = 0;

void check(bool condition, const char* name)
{
    if (condition) {
        std::printf("[PASS] %s\n", name);
    } else {
        std::printf("[FAIL] %s\n", name);
        ++failures;
    }
}

void testPanadapterOwnershipDecisions()
{
    constexpr quint32 ours = 0x12345678;
    QMap<QString, QString> noOwner;
    QMap<QString, QString> ourOwner{{QStringLiteral("client_handle"), QStringLiteral("0x12345678")}};
    QMap<QString, QString> otherOwner{{QStringLiteral("client_handle"), QStringLiteral("0x87654321")}};

    check(classifyOwnedStatus(false, noOwner, false, ours) == OwnedStatusAction::Defer,
          "unknown pan without client_handle is deferred");
    check(classifyOwnedStatus(false, ourOwner, false, ours) == OwnedStatusAction::Claim,
          "unknown pan with our client_handle is claimed");
    check(classifyOwnedStatus(false, otherOwner, false, ours) == OwnedStatusAction::Ignore,
          "unknown pan with another client_handle is ignored");
    check(classifyOwnedStatus(true, noOwner, false, ours) == OwnedStatusAction::Apply,
          "known pan can apply legacy status without client_handle");
    check(classifyOwnedStatus(true, otherOwner, false, ours) == OwnedStatusAction::Ignore,
          "known pan ignores conflicting other-client status");
    check(classifyOwnedStatus(false, noOwner, true, ours) == OwnedStatusAction::Remove,
          "pan removal wins over ownership uncertainty");
}

void testRemoteAudioRxTracking()
{
    constexpr quint32 ours = 0x12345678;
    RemoteAudioRxTracking state;

    QMap<QString, QString> otherRemoteAudio{
        {QStringLiteral("type"), QStringLiteral("remote_audio_rx")},
        {QStringLiteral("client_handle"), QStringLiteral("0x87654321")}
    };
    check(applyRemoteAudioRxStatus(state,
                                   QStringLiteral("stream 0x0400000A"),
                                   otherRemoteAudio,
                                   ours,
                                   false) == RemoteAudioRxAction::IgnoredOtherClient,
          "remote_audio_rx for another client is ignored");
    check(state.streamId == 0, "ignored remote_audio_rx does not set stream id");

    QMap<QString, QString> noOwnerRemoteAudio{
        {QStringLiteral("type"), QStringLiteral("remote_audio_rx")}
    };
    check(applyRemoteAudioRxStatus(state,
                                   QStringLiteral("stream 0x0400000A"),
                                   noOwnerRemoteAudio,
                                   ours,
                                   false) == RemoteAudioRxAction::DeferredUnknownOwner,
          "remote_audio_rx without owner is deferred when other clients may exist");
    check(state.streamId == 0, "deferred remote_audio_rx does not set stream id");

    check(applyRemoteAudioRxStatus(state,
                                   QStringLiteral("stream 0x0400000A"),
                                   noOwnerRemoteAudio,
                                   ours,
                                   true) == RemoteAudioRxAction::Adopted,
          "remote_audio_rx without owner is adopted for legacy single-client status");
    check(state.streamId == 0x0400000A, "legacy remote_audio_rx adoption records stream id");

    state = {};
    state.createPending = true;
    QMap<QString, QString> ourRemoteAudio{
        {QStringLiteral("type"), QStringLiteral("remote_audio_rx")},
        {QStringLiteral("client_handle"), QStringLiteral("0x12345678")},
        {QStringLiteral("compression"), QStringLiteral("NONE")}
    };
    check(applyRemoteAudioRxStatus(state,
                                   QStringLiteral("stream 0x0400000B"),
                                   ourRemoteAudio,
                                   ours,
                                   false) == RemoteAudioRxAction::Adopted,
          "owned remote_audio_rx status before create callback is adopted");
    check(state.streamId == 0x0400000B, "owned remote_audio_rx status records stream id");
    check(!state.createPending, "owned remote_audio_rx status clears create pending");
    check(state.clientHandle == ours, "owned remote_audio_rx status records owner");
    check(state.compression == QStringLiteral("NONE"), "owned remote_audio_rx status records compression");

    check(applyRemoteAudioRxStatus(state,
                                   QStringLiteral("stream 0x0400000B"),
                                   QMap<QString, QString>{},
                                   ours,
                                   false) == RemoteAudioRxAction::Updated,
          "known remote_audio_rx can apply later status without type or owner");

    check(applyRemoteAudioRxStatus(state,
                                   QStringLiteral("stream 0x0400000B removed"),
                                   QMap<QString, QString>{},
                                   ours,
                                   false) == RemoteAudioRxAction::Removed,
          "known remote_audio_rx removal clears stream");
    check(state.streamId == 0, "removed remote_audio_rx clears stream id");
}

void testCreateResponseParsing()
{
    check(parseCreateResponseStreamId(QStringLiteral("4000009")) == 0x04000009,
          "numeric-only bare create response stream id parses as hex");
    check(parseCreateResponseStreamId(QStringLiteral("400000A")) == 0x0400000A,
          "alpha bare create response stream id parses as hex");
    check(parseCreateResponseStreamId(QStringLiteral("stream=0x0400000B")) == 0x0400000B,
          "keyed create response stream id parses");
    check(parseCreateResponseStreamId(QStringLiteral("stream=4000009")) == 0x04000009,
          "numeric-only keyed create response stream id parses as hex");
    check(streamCommandId(0x0400000A) == QStringLiteral("0400000a"),
          "stream command id is normalized for stream remove");
}

} // namespace

int main()
{
    std::printf("Radio status ownership tests\n\n");
    testPanadapterOwnershipDecisions();
    testRemoteAudioRxTracking();
    testCreateResponseParsing();

    if (failures) {
        std::printf("\n%d failure(s)\n", failures);
        return 1;
    }

    std::printf("\nAll tests passed\n");
    return 0;
}
