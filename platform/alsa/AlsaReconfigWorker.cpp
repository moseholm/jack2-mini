#include <cstdint>

// Copyright (C) 2026 JACK contributors
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

#include "AlsaReconfigWorker.h"
#include "Error.h"

namespace jackmini {

AlsaReconfigWorker::AlsaReconfigWorker(AlsaRateRequestHandler* handler)
    : m_handler(handler)
    , m_running(false)
    , m_soundChanged(false)
    , m_thread(this)
{
}

AlsaReconfigWorker::~AlsaReconfigWorker()
{
    stop();
}

void AlsaReconfigWorker::onRateEvent(std::int32_t card, AlsaRateDirection dir, std::int64_t value)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_pending[Key(card, (std::int32_t)dir)] = value;
    }
    m_cond.notify_one();
}

void AlsaReconfigWorker::onSoundChanged()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_soundChanged = true;
    }
    m_cond.notify_one();
}

void AlsaReconfigWorker::onCardFailed(std::int32_t cardId)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_failures.push_back(cardId);
    }
    m_cond.notify_one();
}

bool AlsaReconfigWorker::init()
{
    return true;
}

bool AlsaReconfigWorker::execute()
{
    std::map<Key, std::int64_t> batch;
    std::vector<std::int32_t> failures;
    bool sound = false;
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_cond.wait(lock, [this] { return !m_pending.empty() || !m_failures.empty() || m_soundChanged || !m_running; });
        if (!m_running) {
            return false;
        }
        failures.swap(m_failures);
        sound = m_soundChanged;
        m_soundChanged = false;
        batch.swap(m_pending);
    }

    for (size_t i = 0; i < failures.size(); i++) {
        if (m_handler) {
            m_handler->handleCardFailure(failures[i]);
        }
    }
    if (sound && m_handler) {
        m_handler->handleSoundChanged();
    }
    for (std::map<Key, std::int64_t>::const_iterator it = batch.begin(); it != batch.end(); ++it) {
        if (m_handler) {
            m_handler->handleRateRequest(it->first.first, (AlsaRateDirection)it->first.second, it->second);
        }
    }
    return true;
}

std::int32_t AlsaReconfigWorker::start()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_running = true;
    }
    if (m_thread.startSync() < 0) {
        jack_error("rate worker: cannot start thread");
        std::lock_guard<std::mutex> lock(m_mutex);
        m_running = false;
        return -1;
    }
    return 0;
}

std::int32_t AlsaReconfigWorker::stop()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_running = false;
    }
    m_cond.notify_all();
    return m_thread.stop();
}

} // namespace jackmini
