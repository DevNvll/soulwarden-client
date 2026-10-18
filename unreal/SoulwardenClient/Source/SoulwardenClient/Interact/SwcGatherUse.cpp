#include "Interact/SwcGatherUse.h"

#include <algorithm>

namespace swc::interact {

void GatherWatch::start(double now, const swc::net::GatheringState& state) {
    m_step = Step::waiting;
    m_reference = state.reference;
    m_start = now;
    m_begin = m_end = 0.0;
}

GatherWatch::Step GatherWatch::update(double now, const swc::net::GatheringState& state) {
    if (m_step == Step::done || m_step == Step::failed || m_step == Step::late) {
        return m_step;
    }
    if (state.reference != m_reference) {
        m_reference = state.reference;
        if (state.status == swc::net::kGatherSuccess) {
            // The ring of the prompt runs remaining / speed seconds (live check L3: field 2 is a speed).
            m_step = Step::running;
            m_begin = now;
            m_end = now + static_cast<double>(state.remaining) / std::max(static_cast<double>(state.speed), 1.0e-3);
        } else if (state.status == swc::net::kGatherDone) {
            m_step = Step::done;
        } else if (state.status == swc::net::kGatherFailure) {
            m_step = Step::failed;
        }
    }
    if ((m_step == Step::waiting && now > m_start + kNoStart) || (m_step == Step::running && now > m_end + kSlack)) {
        m_step = Step::late;
    }
    return m_step;
}

float GatherWatch::progress(double now) const {
    if (m_step != Step::running || m_end <= m_begin) {
        return m_step == Step::done ? 1.0F : 0.0F;
    }
    return static_cast<float>(std::clamp((now - m_begin) / (m_end - m_begin), 0.0, 1.0));
}

} // namespace swc::interact
