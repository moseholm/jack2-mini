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

#include "JackAlsaReconfigWorker.h"
#include "JackError.h"

namespace jackmini {

JackAlsaReconfigWorker::JackAlsaReconfigWorker(JackAlsaRateRequestHandler* handler)
    : fHandler(handler)
    , fRunning(false)
    , fSoundChanged(false)
    , fThread(this)
{
}

JackAlsaReconfigWorker::~JackAlsaReconfigWorker()
{
    Stop();
}

void JackAlsaReconfigWorker::OnRateEvent(int card, JackAlsaRateDirection dir, long value)
{
    {
        std::lock_guard<std::mutex> lock(fMutex);
        fPending[Key(card, (int)dir)] = value;
    }
    fCond.notify_one();
}

void JackAlsaReconfigWorker::OnSoundChanged()
{
    {
        std::lock_guard<std::mutex> lock(fMutex);
        fSoundChanged = true;
    }
    fCond.notify_one();
}

void JackAlsaReconfigWorker::OnCardFailed(int card_id)
{
    {
        std::lock_guard<std::mutex> lock(fMutex);
        fFailures.push_back(card_id);
    }
    fCond.notify_one();
}

bool JackAlsaReconfigWorker::Init()
{
    return true;
}

bool JackAlsaReconfigWorker::Execute()
{
    std::map<Key, long> batch;
    std::vector<int> failures;
    bool sound = false;
    {
        std::unique_lock<std::mutex> lock(fMutex);
        fCond.wait(lock, [this] { return !fPending.empty() || !fFailures.empty() || fSoundChanged || !fRunning; });
        if (!fRunning) {
            return false;
        }
        failures.swap(fFailures);
        sound = fSoundChanged;
        fSoundChanged = false;
        batch.swap(fPending);
    }

    for (size_t i = 0; i < failures.size(); i++) {
        if (fHandler) {
            fHandler->HandleCardFailure(failures[i]);
        }
    }
    if (sound && fHandler) {
        fHandler->HandleSoundChanged();
    }
    for (std::map<Key, long>::const_iterator it = batch.begin(); it != batch.end(); ++it) {
        if (fHandler) {
            fHandler->HandleRateRequest(it->first.first, (JackAlsaRateDirection)it->first.second, it->second);
        }
    }
    return true;
}

int JackAlsaReconfigWorker::Start()
{
    {
        std::lock_guard<std::mutex> lock(fMutex);
        fRunning = true;
    }
    if (fThread.StartSync() < 0) {
        jack_error("rate worker: cannot start thread");
        std::lock_guard<std::mutex> lock(fMutex);
        fRunning = false;
        return -1;
    }
    return 0;
}

int JackAlsaReconfigWorker::Stop()
{
    {
        std::lock_guard<std::mutex> lock(fMutex);
        fRunning = false;
    }
    fCond.notify_all();
    return fThread.Stop();
}

} // namespace jackmini
