#pragma once

#include <chrono>
#include <cstdint>
#include <deque>
#include <list>
#include <span>
#include <string>
#include <vector>

// The client side of the datagram carrier of the game server transport (GridMate variant, Soulwarden protocol notes
// 3.2). Plain C++ with no engine header: tools/test-net.sh tests it against the carrier of the backend.
//
//   datagram := flags:u8 version:u8 sequence:u16 message*            big-endian; flags 0x80 (bit 0: LZ4, the backend
//                                                                     sends no compressed datagram), version 0x01
//   message  := flags:u8 size:u16 [channel:u8] [chunks:u16] [sequence:u16] [reliable:u16] data
//               flags: 0x01 reliable, 0x04 chunk count, 0x08 sequence = previous + 1, 0x10 reliable sequence implied,
//                      0x20 channel, 0x80 connecting
//   Channel 3 is the system channel; the last data byte of a system message is its id: 1 connect request,
//   2 connect ack, 3 disconnect, 4 clock sync, 6 acknowledgements. Handshake payload: version u32 = 5.
//
// Channel 0 has the reliable data, channel 1 the unreliable data. The data of one message has 1,024 bytes at most: a
// larger reliable message goes in chunks. A datagram has 1,038 bytes at most (the retail client closes its connection
// when it gets a larger one). The rules of the backend carrier (apps/backend/src/server/carrier/carrier.cpp) apply in
// the two directions: the same datagram writer, the same reader, resend after 250 ms, acknowledgement after 40 ms,
// keep-alive after 100 ms.

namespace swc::net {

using Bytes = std::vector<std::uint8_t>;
using Clock = std::chrono::steady_clock;

inline constexpr std::uint8_t kChannelReliable = 0;
inline constexpr std::uint8_t kChannelUnreliable = 1;
inline constexpr std::uint8_t kChannelSystem = 3;
inline constexpr std::size_t kMaxMessageData = 1024;
inline constexpr std::size_t kMaxDatagram = 4 + 10 + kMaxMessageData;

class Carrier {
public:
    enum class State { idle, connecting, connected, closed };

    /** One message for the game layer: its channel and its data (the chunks of a message put together). */
    struct Delivery {
        std::uint8_t channel{};
        Bytes data;
    };

    Carrier();

    /** Starts the handshake: the connect request goes out at each tick until the connect ack arrives. */
    void connect();
    /** Reads one clear datagram of the server. */
    void receive(std::span<const std::uint8_t> datagram);
    /** Queues one message on channel 0 or 1. A message larger than 1,024 bytes goes reliable, in chunks. */
    void send(std::uint8_t channel, std::span<const std::uint8_t> data, bool reliable);
    /** Queues the carrier disconnect (system message 3, reason 0), as the retail client at a leave of the world. */
    void disconnect();
    /** @return The datagrams to send now: queued messages, acknowledgements, resends, keep-alives. */
    std::vector<Bytes> tick(Clock::time_point now);
    /** @return The messages of the server that are complete and in sequence. */
    std::vector<Delivery> take_delivered();

    [[nodiscard]] State state() const { return m_state; }
    /** Each problem of the reader since the last call (for a log line). */
    std::vector<std::string> take_problems();

    /** Counts for the log line at the end of a session. */
    struct Counts {
        std::uint64_t datagramsIn{0};
        std::uint64_t datagramsOut{0};
        std::uint64_t bytesOut{0};
        std::uint64_t resends{0};
        std::size_t largestDatagramIn{0};
    };
    [[nodiscard]] const Counts& counts() const { return m_counts; }

private:
    static constexpr std::uint8_t kChannelCount = 4;

    struct Message {
        std::uint8_t channel{};
        bool reliable{};
        bool connecting{};
        std::uint16_t chunks{1};
        std::uint16_t sequence{};
        std::uint16_t reliableSequence{};
        Bytes data;
    };

    struct SentDatagram {
        std::uint16_t sequence{};
        Clock::time_point sentAt;
        std::vector<Message> reliable;
    };

    void queue_message(std::uint8_t channel, std::span<const std::uint8_t> data, bool reliable);
    void handle_system(std::span<const std::uint8_t> data);
    void read_acknowledgement(std::span<const std::uint8_t> data);
    void release_received();
    Bytes build_acknowledgement();
    void flush(Clock::time_point now, std::vector<Bytes>& out);

    State m_state{State::idle};
    std::uint16_t m_datagramSequence{0};
    std::uint16_t m_sendSequence[kChannelCount];
    std::uint16_t m_sendReliableSequence[kChannelCount];
    std::uint16_t m_receivedSequence[kChannelCount];
    std::uint16_t m_receivedReliableSequence[kChannelCount];
    std::deque<Message> m_toSend;
    std::list<SentDatagram> m_sent;
    std::list<Message> m_received[kChannelCount];
    std::vector<std::uint16_t> m_toAcknowledge;
    std::vector<Delivery> m_delivered;
    std::vector<std::string> m_problems;
    Clock::time_point m_lastSend{};
    Clock::time_point m_lastAcknowledgement{};
    Clock::time_point m_lastConnectRequest{};
    bool m_connectRequested{false};
    Counts m_counts;
};

} // namespace swc::net
