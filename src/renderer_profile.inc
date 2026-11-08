//Included after D3D11/ComPtr/check declarations. No queries exist until opted in.
class RendererProfiler {
    static constexpr std::size_t passes = static_cast<std::size_t>(GpuPass::count);

    struct Frame {
        ComPtr<ID3D11Query> disjoint;
        std::array<ComPtr<ID3D11Query>, 2 + passes * 2> times;
        std::array<bool, passes> used{};
        std::uint64_t id{};
        bool pending{};
    };

    std::array<Frame, 16> frames_;
    std::optional<std::size_t> active_;
    std::vector<GpuFrameProfile> completed_;

public:
    void poll(ID3D11DeviceContext* context) {
        for (auto& frame : frames_)
            if (frame.pending) {
                D3D11_QUERY_DATA_TIMESTAMP_DISJOINT info{};
                auto status =
                    context->GetData(frame.disjoint.Get(), &info, sizeof(info), D3D11_ASYNC_GETDATA_DONOTFLUSH);
                check(status, "Read GPU profiling disjoint query");
                if (status != S_OK)
                    continue;
                std::array<UINT64, 2 + passes * 2> times{};
                bool ready = true;
                for (std::size_t i = 0; i < times.size(); ++i)
                    if (i < 2 || frame.used[(i - 2) / 2]) {
                        status = context->GetData(frame.times[i].Get(),
                                                  &times[i],
                                                  sizeof(times[i]),
                                                  D3D11_ASYNC_GETDATA_DONOTFLUSH);
                        check(status, "Read GPU timestamp");
                        if (status != S_OK) {
                            ready = false;
                            break;
                        }
                    }
                if (!ready)
                    continue;
                GpuFrameProfile sample;
                sample.frame = frame.id;
                sample.disjoint = info.Disjoint || !info.Frequency;
                if (!sample.disjoint) {
                    const auto elapsed = [&](std::size_t first, std::size_t last) {
                        return static_cast<double>(times[last] - times[first])
                             * 1000.0
                             / static_cast<double>(info.Frequency);
                    };
                    sample.total_ms = elapsed(0, 1);
                    for (std::size_t i = 0; i < passes; ++i)
                        if (frame.used[i])
                            sample.pass_ms[i] = elapsed(2 + i * 2, 3 + i * 2);
                }
                completed_.push_back(sample);
                frame.pending = false;
            }
    }

    void finish(ID3D11DeviceContext* context) {
        if (!active_)
            return;
        auto& frame = frames_[*active_];
        context->End(frame.times[1].Get());
        context->End(frame.disjoint.Get());
        frame.pending = true;
        active_.reset();
    }

    void begin(ID3D11Device* device, ID3D11DeviceContext* context) {
        finish(context);
        if (!active_frame_profile)
            return;
        poll(context);
        for (std::size_t i = 0; i < frames_.size(); ++i)
            if (!frames_[i].pending) {
                auto& frame = frames_[i];
                if (!frame.disjoint) {
                    D3D11_QUERY_DESC description{ D3D11_QUERY_TIMESTAMP_DISJOINT, 0 };
                    check(device->CreateQuery(&description, frame.disjoint.GetAddressOf()),
                          "Create GPU disjoint query");
                    description.Query = D3D11_QUERY_TIMESTAMP;
                    for (auto& time : frame.times)
                        check(device->CreateQuery(&description, time.GetAddressOf()), "Create GPU timestamp");
                }
                frame.used = {};
                frame.id = active_frame_profile->frame;
                active_ = i;
                context->Begin(frame.disjoint.Get());
                context->End(frame.times[0].Get());
                break;
            }
    }

    void begin_pass(ID3D11DeviceContext* context, GpuPass pass) {
        if (active_) {
            const auto i = static_cast<std::size_t>(pass);
            auto& frame = frames_[*active_];
            frame.used[i] = true;
            context->End(frame.times[2 + i * 2].Get());
        }
    }

    void end_pass(ID3D11DeviceContext* context, GpuPass pass) {
        if (active_)
            context->End(frames_[*active_].times[3 + static_cast<std::size_t>(pass) * 2].Get());
    }

    std::vector<GpuFrameProfile> take(ID3D11DeviceContext* context, bool drain) {
        if (drain) {
            finish(context);
            context->Flush();
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{ 5 };
            while (std::ranges::any_of(frames_, [](const auto& f) {
                return f.pending;
            })) {
                poll(context);
                if (std::chrono::steady_clock::now() >= deadline)
                    throw std::runtime_error("GPU profiling drain timed out");
                Sleep(1);
            }
        } else
            poll(context);
        return std::exchange(completed_, {});
    }
};

inline void profile_draw() {
    if (active_frame_profile)
        ++active_frame_profile->counters.draw_calls;
}

inline void profile_binding() {
    if (active_frame_profile)
        ++active_frame_profile->counters.state_bindings;
}

inline void profile_constants() {
    if (active_frame_profile)
        ++active_frame_profile->counters.constant_uploads;
}
