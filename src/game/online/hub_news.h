#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace online {

constexpr size_t kHubNewsBufferSize = 0x6400;

// NOT ORIGINAL (the class; its fields are the network manager's +0x99F4-+0xFE0B): what the online service downloads -
// the news file with the message of the day in it (the HTTP request, when it began, whether it has ended) - and the
// availability check's answer, which shares the last word (a field of the hub's after the class would be padded to
// the next one, and the hub's size counts in the heaps). Written by the service task and the download's callbacks
// (server_list.cpp, service.cpp). Owned by the hub, in its place there.
class NewsDownload {
public:
    // Not original - the fields' offsets checked against the original's at compile time (gamenet_manager_create.cpp).
    static void check_layout();

    // 0x00533360: the buffer at +0xFDF8 (its size in *size, +0xFDFC).
    void* get_pending_data(uint32_t* size);

    std::array<uint8_t, kHubNewsBufferSize>& news_buffer() { return news_buffer_; }
    [[nodiscard]] const std::array<uint8_t, kHubNewsBufferSize>& news_buffer() const { return news_buffer_; }
    [[nodiscard]] const char* motd_text() const { return motd_text_; }
    void set_motd_text(const char* value) { motd_text_ = value; }
    void set_pending_data(uint8_t* value) { pending_data_ = value; }
    void set_pending_data_size(uint32_t value) { pending_data_size_ = value; }
    [[nodiscard]] int request() const { return request_; }
    void set_request(int value) { request_ = value; }
    [[nodiscard]] uint32_t request_started_at() const { return request_started_at_; }
    void set_request_started_at(uint32_t value) { request_started_at_ = value; }
    [[nodiscard]] bool download_done() const { return download_done_; }
    void set_download_done(bool value) { download_done_ = value; }
    [[nodiscard]] bool motd_received() const { return motd_received_; }
    void set_motd_received(bool value) { motd_received_ = value; }
    [[nodiscard]] bool services_available() const { return services_available_; }
    void set_services_available(bool value) { services_available_ = value; }

private:
    std::array<uint8_t, kHubNewsBufferSize> news_buffer_{}; // +0x0000 the news / message of the day downloaded
    const char* motd_text_{};       // +0x6400 the message of the day in news_buffer_
    uint8_t* pending_data_{};       // +0x6404 the rest of the download after it
    uint32_t pending_data_size_{};  // +0x6408
    int request_{};                 // +0x640C the current ghttp request
    uint32_t request_started_at_{}; // +0x6410
    bool download_done_{};          // +0x6414 the news download has ended (or the MOTD was cancelled)
    bool motd_received_{false};     // +0x6415
    bool services_available_{};     // +0x6416 the GameSpy services are available
};

} // namespace online
